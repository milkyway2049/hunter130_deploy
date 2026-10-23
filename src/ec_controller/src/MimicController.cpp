#include "ec_controller/ControllerBase.h"
#include "ec_controller/MimicPolicy.h"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/string.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace ec
{
class MimicController : public ControllerBase
{
public:
  controller_interface::CallbackReturn on_configure(
    const rclcpp_lifecycle::State & previous_state) override
  {
    if (ControllerBase::on_configure(previous_state) != controller_interface::CallbackReturn::SUCCESS) {
      return controller_interface::CallbackReturn::ERROR;
    }
    auto node = get_node();
    node->declare_parameter<std::string>("policy_path", "config/policy/hunter130_mimic_50000.onnx");
    node->declare_parameter<std::string>("metadata_path", "config/policy/hunter130_mimic_50000.json");
    node->declare_parameter<std::string>("motion_path", "config/motions/love_you.csv");
    node->declare_parameter<double>("preparation_duration", 3.0);
    node->declare_parameter<double>("stand_before_duration", 1.0);
    node->declare_parameter<double>("return_duration", 3.0);
    node->declare_parameter<std::vector<double>>("stand_pos", std::vector<double>{});
    node->declare_parameter<double>("start_position_tolerance", 0.35);
    node->declare_parameter<std::string>("log_dir", "/home/milkyway/log/hunter130");
    preparation_duration_ = node->get_parameter("preparation_duration").as_double();
    stand_before_duration_ = node->get_parameter("stand_before_duration").as_double();
    return_duration_ = node->get_parameter("return_duration").as_double();
    start_tolerance_ = node->get_parameter("start_position_tolerance").as_double();
    log_dir_ = node->get_parameter("log_dir").as_string();
    const auto resolve = [](const std::string & path) {
        return std::filesystem::path(path).is_absolute() ? path :
               ament_index_cpp::get_package_share_directory("ec_controller") + "/" + path;
      };
    try {
      if (!std::isfinite(preparation_duration_) || preparation_duration_ < .5 ||
        !std::isfinite(stand_before_duration_) || stand_before_duration_ < .5 ||
        !std::isfinite(return_duration_) || return_duration_ < .5 ||
        !std::isfinite(start_tolerance_) || start_tolerance_ <= 0 || start_tolerance_ > .5)
      {
        throw std::runtime_error("Transitions must be >=0.5s; start tolerance must be (0,0.5]rad");
      }
      policy_ = std::make_unique<mimic::Policy>();
      policy_->load(resolve(node->get_parameter("policy_path").as_string()),
        resolve(node->get_parameter("metadata_path").as_string()),
        resolve(node->get_parameter("motion_path").as_string()));
      const auto & names = policy_->metadata().joints;
      if (jointCount() != mimic::kJoints) {throw std::runtime_error("MimicLite requires 25 joints");}
      std::vector<bool> matched(mimic::kJoints, false);
      for (size_t i = 0; i < jointCount(); ++i) {
        const auto found = std::find(names.begin(), names.end(), joint_names_[i]);
        if (found == names.end() || matched[std::distance(names.begin(), found)]) {
          throw std::runtime_error("Duplicate or unknown controller joint: " + joint_names_[i]);
        }
        joint_to_policy_[i] = static_cast<size_t>(std::distance(names.begin(), found));
        matched[joint_to_policy_[i]] = true;
      }
      stand_position_ = policy_->metadata().default_position;
      const auto stand = node->get_parameter("stand_pos").as_double_array();
      if (!stand.empty()) {
        if (stand.size() != mimic::kJoints) {throw std::runtime_error("stand_pos requires 25 values");}
        for (size_t j = 0; j < mimic::kJoints; ++j) {stand_position_[joint_to_policy_[j]] = stand[j];}
      }
      if (!stand_position_.allFinite() ||
        (stand_position_.array() < policy_->metadata().lower.array()).any() ||
        (stand_position_.array() > policy_->metadata().upper.array()).any())
      {
        throw std::runtime_error("Standing pose exceeds robot limits");
      }
    } catch (const std::exception & error) {
      RCLCPP_ERROR(logger(), "MimicLite configuration failed: %s", error.what());
      return controller_interface::CallbackReturn::ERROR;
    }
    stop_subscription_ = node->create_subscription<std_msgs::msg::Bool>(
      "/mimic/stop", 10, [this](std_msgs::msg::Bool::ConstSharedPtr message) {
        if (message->data && active_.load()) {stop_requested_.store(true);}
      });
    state_publisher_ = node->create_publisher<std_msgs::msg::String>("/mimic/state", 10);
    state_timer_ = node->create_wall_timer(std::chrono::milliseconds(50), [this]() {
        std_msgs::msg::String state;
        if (!active_.load()) {state.data = "inactive";}
        else {
          switch (phase_.load()) {
            case Phase::StandingBefore: state.data = "standing_before"; break;
            case Phase::Preparing: state.data = "preparing"; break;
            case Phase::Tracking: state.data = "tracking"; break;
            case Phase::Returning: state.data = "returning"; break;
            case Phase::StandingAfter: state.data = "standing_after"; break;
            case Phase::Fault: state.data = "fault"; break;
          }
        }
        state_publisher_->publish(state);
      });
    RCLCPP_INFO(logger(), "Loaded Hunter130 MimicLite PPO: 467+272 inputs, 25 actions, %zu motion frames",
      policy_->frameCount());
    return controller_interface::CallbackReturn::SUCCESS;
  }

  controller_interface::InterfaceConfiguration state_interface_configuration() const override
  {
    auto interfaces = ControllerBase::state_interface_configuration();
    for (const auto & name : imu_names_) {interfaces.names.push_back("imu_sensor/" + name);}
    return interfaces;
  }

  controller_interface::CallbackReturn on_activate(
    const rclcpp_lifecycle::State & previous_state) override
  {
    if (ControllerBase::on_activate(previous_state) != controller_interface::CallbackReturn::SUCCESS) {
      return controller_interface::CallbackReturn::ERROR;
    }
    std::unordered_map<std::string, size_t> indices;
    for (size_t i = 0; i < state_interfaces_.size(); ++i) {indices[state_interfaces_[i].get_name()] = i;}
    try {
      for (size_t i = 0; i < mimic::kJoints; ++i) {
        const auto & name = policy_->metadata().joints[i];
        position_indices_[i] = indices.at(name + "/position");
        velocity_indices_[i] = indices.at(name + "/velocity");
      }
      for (size_t i = 0; i < imu_names_.size(); ++i) {
        imu_indices_[i] = indices.at("imu_sensor/" + imu_names_[i]);
      }
    } catch (const std::exception & error) {
      RCLCPP_ERROR(logger(), "Missing MimicLite feedback interface: %s", error.what());
      return controller_interface::CallbackReturn::ERROR;
    }
    phase_ = Phase::StandingBefore;
    stop_requested_ = false;
    active_ = true;
    first_update_ = true;
    phase_elapsed_ = tracking_elapsed_ = inference_elapsed_ = 0;
    if (!log_dir_.empty()) {
      std::error_code error;
      std::filesystem::create_directories(log_dir_, error);
      if (!error) {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        log_.open(std::filesystem::path(log_dir_) / ("mimic_log_" + std::to_string(stamp) + ".csv"));
        if (log_) {
          log_ << std::setprecision(17) << "time_ns,motion_time_s,frame";
          for (const char * field : {"position", "velocity", "action", "target"}) {
            for (const auto & name : policy_->metadata().joints) {log_ << ',' << field << '_' << name;}
          }
          log_ << '\n';
        }
      }
      if (!log_) {RCLCPP_WARN(logger(), "Could not open MimicLite CSV log in %s", log_dir_.c_str());}
    }
    RCLCPP_INFO(logger(), "MimicLite: stand %.2fs -> prepare %.2fs -> play -> return %.2fs -> stand",
      stand_before_duration_, preparation_duration_, return_duration_);
    return controller_interface::CallbackReturn::SUCCESS;
  }

  controller_interface::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State & previous_state) override
  {
    log_.close();
    log_.clear();
    active_ = false;
    stop_requested_ = false;
    phase_ = Phase::StandingBefore;
    return ControllerBase::on_deactivate(previous_state);
  }

protected:
  void onUpdate(const rclcpp::Time & time, const rclcpp::Duration & period) override
  {
    mimic::Sample sample;
    if (phase_ == Phase::Fault) {damping(); return;}
    try {
      const auto read = [this](size_t index) {
          const auto value = state_interfaces_[index].get_optional();
          if (!value || !std::isfinite(*value)) {throw std::runtime_error("Invalid joint/IMU feedback");}
          return *value;
        };
      for (size_t j = 0; j < mimic::kJoints; ++j) {
        sample.position[j] = read(position_indices_[j]);
        sample.velocity[j] = read(velocity_indices_[j]);
      }
      sample.orientation = Eigen::Quaterniond(read(imu_indices_[0]), read(imu_indices_[1]),
          read(imu_indices_[2]), read(imu_indices_[3]));
      for (size_t i = 0; i < 3; ++i) {sample.angular_velocity[i] = read(imu_indices_[i + 4]);}
      const double dt = period.seconds();
      if (!sample.valid() || !std::isfinite(dt) || dt <= 0 || dt > .1) {
        throw std::runtime_error("Invalid orientation or control interval");
      }
      sample.orientation.normalize();
      if (first_update_) {
        initial_position_ = last_target_ = sample.position;
        first_update_ = false;
      }
      if (stop_requested_.exchange(false) && phase_ != Phase::Returning) {beginReturn(last_target_);}
      mimic::Joints target;
      bool inferred = false;
      mimic::Action action{};
      size_t frame_index = 0;
      if (phase_ == Phase::StandingBefore) {
        phase_elapsed_ += dt;
        target = blendTo(stand_position_, stand_before_duration_);
        if (phase_elapsed_ >= stand_before_duration_ &&
          (sample.position - stand_position_).cwiseAbs().maxCoeff() <= start_tolerance_)
        {
          initial_position_ = target;
          phase_elapsed_ = 0;
          phase_ = Phase::Preparing;
        }
      } else if (phase_ == Phase::Preparing) {
        phase_elapsed_ += dt;
        target = blendTo(policy_->frame(0).position, preparation_duration_);
        if (phase_elapsed_ >= preparation_duration_) {
          if ((sample.position - policy_->frame(0).position).cwiseAbs().maxCoeff() <= start_tolerance_) {
            policy_->reset(sample);
            action = policy_->infer(sample, 0);
            target = policy_->target();
            phase_ = Phase::Tracking;
            inferred = true;
            RCLCPP_INFO(logger(), "MimicLite motion started at 50Hz");
          } else {
            RCLCPP_WARN_THROTTLE(logger(), *get_node()->get_clock(), 2000,
              "Waiting for reference start pose (maximum joint error %.3frad)",
              (sample.position - policy_->frame(0).position).cwiseAbs().maxCoeff());
          }
        }
      } else if (phase_ == Phase::Tracking) {
        tracking_elapsed_ += dt;
        inference_elapsed_ += dt;
        const auto last = policy_->frameCount() - 1;
        frame_index = std::min(last, static_cast<size_t>(
            std::floor((tracking_elapsed_ + 1e-9) / policy_->metadata().control_dt)));
        if (inference_elapsed_ + 1e-9 >= policy_->metadata().control_dt) {
          inference_elapsed_ = std::max(0., inference_elapsed_ - policy_->metadata().control_dt);
          action = policy_->infer(sample, frame_index);
          inferred = true;
        } else {
          policy_->advanceControl(dt);
        }
        target = policy_->target();
        if (frame_index == last) {
          beginReturn(target);
          RCLCPP_INFO(logger(), "Motion complete; returning to standing pose");
        }
      } else if (phase_ == Phase::Returning) {
        phase_elapsed_ += dt;
        target = blendTo(stand_position_, return_duration_);
        if (phase_elapsed_ >= return_duration_ &&
          (sample.position - stand_position_).cwiseAbs().maxCoeff() <= start_tolerance_)
        {
          phase_ = Phase::StandingAfter;
          RCLCPP_INFO(logger(), "MimicLite finished; holding standing pose until deactivation");
        }
      } else {
        target = stand_position_;
      }
      const auto & config = policy_->metadata();
      target = target.cwiseMax(config.lower).cwiseMin(config.upper);
      last_target_ = target;
      for (size_t j = 0; j < jointCount(); ++j) {
        const auto p = joint_to_policy_[j];
        auto & command = jointCommand(j);
        command.position = target[p];
        command.kp = config.kp[p];
        command.kd = config.kd[p];
        command.torque_mode = isCoupledJoint(joint_names_[j]) ? 2. : 0.;
      }
      if (inferred && log_) {
        log_ << time.nanoseconds() << ',' << tracking_elapsed_ << ',' << frame_index;
        for (double value : sample.position) {log_ << ',' << value;}
        for (double value : sample.velocity) {log_ << ',' << value;}
        for (float value : action) {log_ << ',' << value;}
        for (double value : target) {log_ << ',' << value;}
        log_ << '\n';
      }
    } catch (const std::exception & error) {
      phase_ = Phase::Fault;
      RCLCPP_ERROR(logger(), "MimicLite fault latched; switch to damping before reactivation: %s", error.what());
      damping();
    }
  }

private:
  mimic::Joints blendTo(const mimic::Joints & destination, double duration) const
  {
    double blend = std::min(1., phase_elapsed_ / duration);
    blend = blend * blend * (3. - 2. * blend);
    return initial_position_ + blend * (destination - initial_position_);
  }
  void beginReturn(const mimic::Joints & from)
  {
    initial_position_ = from;
    phase_elapsed_ = 0;
    phase_ = Phase::Returning;
  }
  void damping()
  {
    for (size_t j = 0; j < jointCount(); ++j) {
      auto & command = jointCommand(j);
      command.position = std::isfinite(jointState(j).position) ? jointState(j).position : 0.;
      command.kd = .5;
      command.torque_mode = isCoupledJoint(joint_names_[j]) ? 2. : 0.;
    }
  }
  enum class Phase {StandingBefore, Preparing, Tracking, Returning, StandingAfter, Fault};
  std::atomic<Phase> phase_{Phase::StandingBefore};
  std::atomic<bool> active_{false}, stop_requested_{false};
  bool first_update_{true};
  double preparation_duration_{3.0}, start_tolerance_{.35};
  double stand_before_duration_{1.0}, return_duration_{3.0};
  double phase_elapsed_{0.}, tracking_elapsed_{0.}, inference_elapsed_{0.};
  mimic::Joints initial_position_{mimic::Joints::Zero()};
  mimic::Joints stand_position_{mimic::Joints::Zero()}, last_target_{mimic::Joints::Zero()};
  std::array<size_t, 25> joint_to_policy_{}, position_indices_{}, velocity_indices_{};
  const std::array<std::string, 7> imu_names_{"orientation.w", "orientation.x", "orientation.y",
    "orientation.z", "angular_velocity.x", "angular_velocity.y", "angular_velocity.z"};
  std::array<size_t, 7> imu_indices_{};
  std::unique_ptr<mimic::Policy> policy_;
  std::string log_dir_;
  std::ofstream log_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr stop_subscription_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr state_publisher_;
  rclcpp::TimerBase::SharedPtr state_timer_;
};
}  // namespace ec

PLUGINLIB_EXPORT_CLASS(ec::MimicController, controller_interface::ControllerInterface)
