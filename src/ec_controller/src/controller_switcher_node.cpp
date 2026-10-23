// =============================================================================
//  Copyright (c) 2026 Phanerox. All rights reserved.
// =============================================================================

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/string.hpp>
#include "ec_controller/ControllerModes.h"
#include <ec_radio/msg/radio.hpp>
#include <controller_manager_msgs/srv/switch_controller.hpp>

#include <memory>
#include <string>
#include <vector>
#include <utility>

using namespace std::chrono_literals;

namespace ec
{

/**
 * @brief Controller Switcher Node
 *
 * 监听遥控器输出，自动切换控制器
 * 模式: 阻尼(default)、站立(stand)、AMP 行走(walk)、参考动作(mimic)。
 * Mimic 退出到行走或站立时，先等待控制器完成平滑回站。
 */
class ControllerSwitcherNode : public rclcpp::Node {
public:
  explicit ControllerSwitcherNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : Node("controller_switcher", options)
  {
    // 声明参数
    this->declare_parameter<std::string>("controller_manager_ns", "/controller_manager");
    this->declare_parameter<std::string>("initial_controller", "default_controller");

    // 获取参数
    controller_manager_ns_ = this->get_parameter("controller_manager_ns").as_string();
    initial_controller_ = this->get_parameter("initial_controller").as_string();
    active_controller_ = initial_controller_;

    // 创建服务客户端
    switch_controller_client_ = this->create_client<controller_manager_msgs::srv::SwitchController>(
      controller_manager_ns_ + "/switch_controller");

    // 订阅遥控器话题
    radio_sub_ = this->create_subscription<ec_radio::msg::Radio>(
      "/radio", 10,
      std::bind(&ControllerSwitcherNode::radioCallback, this, std::placeholders::_1));

    // 订阅控制器切换话题（手动切换）
    default_sub_ = this->create_subscription<std_msgs::msg::Float32>(
      "/controller_switch/default_controller", 10,
      [this](const std_msgs::msg::Float32::SharedPtr msg) {
        if (msg->data > 0.5f) {switchController("default_controller");}
      });

    stand_sub_ = this->create_subscription<std_msgs::msg::Float32>(
      "/controller_switch/stand_controller", 10,
      [this](const std_msgs::msg::Float32::SharedPtr msg) {
        if (msg->data > 0.5f) {switchController("stand_controller");}
      });

    walk_sub_ = this->create_subscription<std_msgs::msg::Float32>(
      "/controller_switch/walk_controller", 10,
      [this](const std_msgs::msg::Float32::SharedPtr msg) {
        if (msg->data > 0.5f) {switchController("walk_controller");}
      });

    mimic_sub_ = this->create_subscription<std_msgs::msg::Float32>(
      "/controller_switch/mimic_controller", 10,
      [this](const std_msgs::msg::Float32::SharedPtr msg) {
        if (msg->data > .5f) {switchController("mimic_controller");}
      });
    mimic_stop_pub_ = this->create_publisher<std_msgs::msg::Bool>("/mimic/stop", 10);
    mimic_state_sub_ = this->create_subscription<std_msgs::msg::String>(
      "/mimic/state", 10, [this](std_msgs::msg::String::ConstSharedPtr msg) {
        if (!return_pending_ || active_controller_ != "mimic_controller") {return;}
        if (msg->data == "returning") {return_observed_ = true;}
        if (msg->data == "standing_after" && return_observed_) {
          return_ready_ = true;
          switchController(return_controller_);
        }
      });

    // 等待服务可用
    RCLCPP_INFO(this->get_logger(), "Waiting for switch_controller service...");
    switch_controller_client_->wait_for_service(30s);

    RCLCPP_INFO(this->get_logger(), "Controller Switcher Node started");
    RCLCPP_INFO(this->get_logger(), "Modes: 1 damping, 2 stand, 3 AMP walk, 4 MimicLite");
  }

private:
  void radioCallback(const ec_radio::msg::Radio::SharedPtr msg)
  {
    if (msg->mode != last_mode_) {
      last_mode_ = msg->mode;

      switchController(std::string(controllerForMode(msg->mode)));
    }
  }

  bool isTransitionAllowed(const std::string & from, const std::string & to)
  {
    return transitionAllowed(from, to);
  }

  void switchController(const std::string & controller_name)
  {
    if (controller_name == "default_controller") {
      return_pending_ = return_observed_ = return_ready_ = false;
      return_controller_.clear();
    }
    if (switch_in_flight_) {pending_controller_ = controller_name; return;}
    if (active_controller_ == controller_name) {
      if (controller_name == "mimic_controller") {
        return_pending_ = return_observed_ = return_ready_ = false;
        return_controller_.clear();
      }
      return;
    }

    // 如果还没有记录活跃控制器，直接设置（可能是spawner已经激活了）
    if (active_controller_.empty()) {
      active_controller_ = controller_name;
      RCLCPP_INFO(this->get_logger(), "Assuming '%s' is already active.", controller_name.c_str());
      return;
    }

    // 检查转换是否合法
    if (!isTransitionAllowed(active_controller_, controller_name)) {
      RCLCPP_WARN(this->get_logger(), "Transition not allowed: %s -> %s",
        active_controller_.c_str(), controller_name.c_str());
      return;
    }

    if (active_controller_ == "mimic_controller" &&
      (controller_name == "walk_controller" || controller_name == "stand_controller") && !return_ready_)
    {
      return_controller_ = controller_name;
      if (!return_pending_) {
        return_pending_ = true;
        return_observed_ = false;
        std_msgs::msg::Bool stop;
        stop.data = true;
        mimic_stop_pub_->publish(stop);
        RCLCPP_INFO(get_logger(), "Returning MimicLite to standing before changing controller");
      }
      return;
    }

    auto request = std::make_shared<controller_manager_msgs::srv::SwitchController::Request>();

    // 停用当前控制器
    request->deactivate_controllers.push_back(active_controller_);

    // 激活新控制器
    request->activate_controllers.push_back(controller_name);
    request->strictness = controller_manager_msgs::srv::SwitchController::Request::STRICT;
    request->timeout = rclcpp::Duration::from_seconds(5.0);

    RCLCPP_INFO(this->get_logger(), "Switching to: %s", controller_name.c_str());

    switch_in_flight_ = true;
    auto future = switch_controller_client_->async_send_request(request,
        [this,
        controller_name](rclcpp::Client<controller_manager_msgs::srv::SwitchController>::
        SharedFuture result) {
          try {
            if (result.get()->ok) {
              active_controller_ = controller_name;
              RCLCPP_INFO(this->get_logger(), "Switched to: %s", controller_name.c_str());
            } else {
              RCLCPP_ERROR(this->get_logger(), "Failed to switch to: %s", controller_name.c_str());
            }
          } catch (const std::exception & e) {
            RCLCPP_ERROR(this->get_logger(), "Service call failed: %s", e.what());
          }
          switch_in_flight_ = false;
          if (active_controller_ != "mimic_controller") {
            return_pending_ = return_observed_ = return_ready_ = false;
            return_controller_.clear();
          }
          if (!pending_controller_.empty()) {
            const auto pending = std::exchange(pending_controller_, std::string{});
            switchController(pending);
          }
      });
  }

  std::string controller_manager_ns_;
  std::string initial_controller_;

  std::string active_controller_;
  uint8_t last_mode_{0};
  bool switch_in_flight_{false}, return_pending_{false}, return_observed_{false}, return_ready_{false};
  std::string pending_controller_;
  std::string return_controller_;

  rclcpp::Client<controller_manager_msgs::srv::SwitchController>::SharedPtr
    switch_controller_client_;

  rclcpp::Subscription<ec_radio::msg::Radio>::SharedPtr radio_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr default_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr stand_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr walk_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr mimic_sub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr mimic_stop_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr mimic_state_sub_;

};

} // namespace ec

#ifndef EC_CONTROLLER_SWITCHER_TEST
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ec::ControllerSwitcherNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
#endif
