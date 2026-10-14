// =============================================================================
//  Copyright (c) 2026 Phanerox. All rights reserved.
// =============================================================================

#include <pluginlib/class_list_macros.hpp>
#include "ec_controller/ControllerBase.h"

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <ec_radio/msg/radio.hpp>
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <system_error>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ec
{

static const std::vector<std::string> kImuInterfaces = {
  "imu_sensor/orientation.x", "imu_sensor/orientation.y",
  "imu_sensor/orientation.z", "imu_sensor/orientation.w",
  "imu_sensor/angular_velocity.x", "imu_sensor/angular_velocity.y",
  "imu_sensor/angular_velocity.z", "imu_sensor/linear_acceleration.x",
  "imu_sensor/linear_acceleration.y", "imu_sensor/linear_acceleration.z",
};

class WalkController : public ControllerBase {
public:
  controller_interface::CallbackReturn on_configure(
    const rclcpp_lifecycle::State & previous_state) override
  {
    auto ret = ControllerBase::on_configure(previous_state);
    if (ret != controller_interface::CallbackReturn::SUCCESS) {return ret;}
    auto node = get_node();
    node->declare_parameter<std::vector<double>>("walk_pos", std::vector<double>{});
    node->declare_parameter<std::vector<double>>("walk_kp", std::vector<double>{});
    node->declare_parameter<std::vector<double>>("walk_kd", std::vector<double>{});
    node->declare_parameter<std::vector<std::string>>("policy_joints", std::vector<std::string>{});
    node->declare_parameter<int>("action.action_size", 25);
    node->declare_parameter<int>("action.observation_size", 84);
    node->declare_parameter<int>("action.num_history", 4);
    node->declare_parameter<int>("action.policy_freq", 50);
    node->declare_parameter<std::vector<double>>("action.action_scale", std::vector<double>{});
    node->declare_parameter<std::string>("policy_path", "");

    default_pos_ = node->get_parameter("walk_pos").as_double_array();
    walk_kp_ = node->get_parameter("walk_kp").as_double_array();
    walk_kd_ = node->get_parameter("walk_kd").as_double_array();
    policy_joints_ = node->get_parameter("policy_joints").as_string_array();
    action_size_ = node->get_parameter("action.action_size").as_int();
    obs_size_ = node->get_parameter("action.observation_size").as_int();
    num_history_ = node->get_parameter("action.num_history").as_int();
    const int policy_freq = node->get_parameter("action.policy_freq").as_int();
    action_scale_ = node->get_parameter("action.action_scale").as_double_array();
    std::string policy_path = node->get_parameter("policy_path").as_string();

    const size_t n = jointCount();
    if (n == 0 || action_size_ != static_cast<int>(n) ||
      obs_size_ != 9 + 3 * static_cast<int>(n) || num_history_ <= 0 || policy_freq <= 0 ||
      default_pos_.size() != n || walk_kp_.size() != n || walk_kd_.size() != n ||
      policy_joints_.size() != n || action_scale_.size() != n)
    {
      RCLCPP_ERROR(logger(), "Inconsistent joint arrays or policy dimensions/frequency");
      return controller_interface::CallbackReturn::ERROR;
    }
    for (size_t i = 0; i < n; ++i) {
      if (!std::isfinite(default_pos_[i]) || !std::isfinite(walk_kp_[i]) ||
        !std::isfinite(walk_kd_[i]) || !std::isfinite(action_scale_[i]) ||
        walk_kp_[i] < 0.0 || walk_kd_[i] < 0.0 || action_scale_[i] <= 0.0)
      {
        RCLCPP_ERROR(logger(), "Invalid PD parameters or action scale at index %zu", i);
        return controller_interface::CallbackReturn::ERROR;
      }
    }
    inference_period_ = 1.0 / policy_freq;

    // Policy order comes from the exported training configuration, never an interleaved table.
    std::unordered_map<std::string, size_t> policy_index;
    for (size_t i = 0; i < n; ++i) {
      if (!policy_index.emplace(policy_joints_[i], i).second) {
        RCLCPP_ERROR(logger(), "Duplicate policy joint: %s", policy_joints_[i].c_str());
        return controller_interface::CallbackReturn::ERROR;
      }
    }
    joint_to_policy_.resize(n);
    policy_to_joint_.resize(n);
    std::vector<bool> matched(n, false);
    for (size_t i = 0; i < n; ++i) {
      auto it = policy_index.find(joint_names_[i]);
      if (it == policy_index.end() || matched[it->second]) {
        RCLCPP_ERROR(logger(), "Unmatched or duplicate controller joint: %s", joint_names_[i].c_str());
        return controller_interface::CallbackReturn::ERROR;
      }
      joint_to_policy_[i] = it->second;
      policy_to_joint_[it->second] = i;
      matched[it->second] = true;
    }

    if (policy_path.empty()) {
      RCLCPP_ERROR(logger(), "policy_path must identify an exported ONNX policy");
      return controller_interface::CallbackReturn::ERROR;
    }
    if (!std::filesystem::path(policy_path).is_absolute()) {
      policy_path = ament_index_cpp::get_package_share_directory("ec_controller") + "/" + policy_path;
    }
    actions_.assign(n, 0.0f);
    obs_.assign(obs_size_, 0.0f);
    obs_history_.assign(num_history_ * obs_size_, 0.0f);
    input_shape_ = {1, static_cast<int64_t>(num_history_) * obs_size_};

    try {
      ort_env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "WalkController");
      Ort::SessionOptions options;
      options.SetIntraOpNumThreads(1);
      ort_session_ = std::make_unique<Ort::Session>(*ort_env_, policy_path.c_str(), options);
      if (ort_session_->GetInputCount() != 1 || ort_session_->GetOutputCount() != 1) {
        throw std::runtime_error("Policy must have one observation input and one action output");
      }
      auto input_type = ort_session_->GetInputTypeInfo(0);
      auto output_type = ort_session_->GetOutputTypeInfo(0);
      auto input_info = input_type.GetTensorTypeAndShapeInfo();
      auto output_info = output_type.GetTensorTypeAndShapeInfo();
      const auto input_dims = input_info.GetShape();
      const auto output_dims = output_info.GetShape();
      const auto has_shape = [](const std::vector<int64_t> & dims, int64_t features) {
          return dims.size() == 2 && (dims[0] == -1 || dims[0] == 1) && dims[1] == features;
        };
      if (input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        !has_shape(input_dims, input_shape_[1]) || !has_shape(output_dims, action_size_))
      {
        throw std::runtime_error("ONNX shapes/types do not match configured float32 observations/actions");
      }
      Ort::AllocatorWithDefaultOptions allocator;
      auto input_name = ort_session_->GetInputNameAllocated(0, allocator);
      auto output_name = ort_session_->GetOutputNameAllocated(0, allocator);
      input_name_ = input_name.get();
      output_name_ = output_name.get();
      input_names_ = {input_name_.c_str()};
      output_names_ = {output_name_.c_str()};
      mem_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(logger(), "Failed to configure ONNX policy: %s", e.what());
      return controller_interface::CallbackReturn::ERROR;
    }

    radio_sub_ = node->create_subscription<ec_radio::msg::Radio>(
      "/radio", 10,
      [this](const ec_radio::msg::Radio::SharedPtr msg) {
        radio_vx_.store(msg->vx);
        radio_vy_.store(msg->vy);
        radio_wz_.store(msg->wz);
      });
    node->declare_parameter<std::string>("log_dir", "/home/milkyway/log/hunter130");
    log_dir_ = node->get_parameter("log_dir").as_string();

    RCLCPP_INFO(logger(), "Loaded policy: %s (frame=%d, actions=%d, history=%d)",
      policy_path.c_str(), obs_size_, action_size_, num_history_);
    return controller_interface::CallbackReturn::SUCCESS;
  }

  controller_interface::CallbackReturn on_activate(
    const rclcpp_lifecycle::State & previous_state) override
  {
    auto ret = ControllerBase::on_activate(previous_state);
    if (ret != controller_interface::CallbackReturn::SUCCESS) {return ret;}
    std::fill(actions_.begin(), actions_.end(), 0.0f);
    history_filled_ = false;
    inference_accumulator_ = 0.0;
    openLogFile();
    RCLCPP_INFO(logger(), "WalkController activated; first observation will initialize all history frames");
    return controller_interface::CallbackReturn::SUCCESS;
  }

  controller_interface::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State & previous_state) override
  {
    history_filled_ = false;
    std::fill(actions_.begin(), actions_.end(), 0.0f);
    closeLogFile();
    return ControllerBase::on_deactivate(previous_state);
  }

  controller_interface::InterfaceConfiguration state_interface_configuration() const override
  {
    auto config = ControllerBase::state_interface_configuration();
    config.names.insert(config.names.end(), kImuInterfaces.begin(), kImuInterfaces.end());
    return config;
  }

protected:
  void onUpdate(const rclcpp::Time & time, const rclcpp::Duration & period) override
  {
    inference_accumulator_ += period.seconds();
    if (!history_filled_ || inference_accumulator_ >= inference_period_) {
      inference_accumulator_ = std::fmod(inference_accumulator_, inference_period_);
      if (buildObservation()) {
        updateHistory();
        try {
          auto input = Ort::Value::CreateTensor<float>(mem_info_, obs_history_.data(),
            obs_history_.size(), input_shape_.data(), input_shape_.size());
          auto output = ort_session_->Run(Ort::RunOptions{nullptr}, input_names_.data(),
            &input, 1, output_names_.data(), 1);
          auto output_info = output.front().GetTensorTypeAndShapeInfo();
          if (output_info.GetShape() != std::vector<int64_t>{1, action_size_}) {
            throw std::runtime_error("Unexpected inference output shape");
          }
          const float * raw_actions = output.front().GetTensorData<float>();
          if (!std::all_of(raw_actions, raw_actions + action_size_,
            [](float value) {return std::isfinite(value);}))
          {
            throw std::runtime_error("Policy returned non-finite actions");
          }
          // The training runner applies no action clipping; normalization is inside ONNX.
          std::copy(raw_actions, raw_actions + action_size_, actions_.begin());
        } catch (const std::exception & e) {
          RCLCPP_ERROR_THROTTLE(logger(), *get_node()->get_clock(), 1000,
            "ONNX inference failed: %s", e.what());
        }
      } else {
        RCLCPP_ERROR_THROTTLE(logger(), *get_node()->get_clock(), 1000,
          "Cannot build policy observation: missing or non-finite IMU/joint/command data");
      }
    }

    // Hold the latest policy targets between 50 Hz inference steps.
    for (size_t i = 0; i < jointCount(); ++i) {
      const size_t policy_index = joint_to_policy_[i];
      auto & cmd = jointCommand(i);
      cmd.position = default_pos_[i] + action_scale_[policy_index] * actions_[policy_index];
      cmd.velocity = 0.0;
      cmd.effort = 0.0;
      cmd.kp = walk_kp_[i];
      cmd.kd = walk_kd_[i];
      cmd.torque_mode = isCoupledJoint(joint_names_[i]) ? 2.0 : 0.0;
    }
    writeLogRow(time);
  }

private:
  // One file per activation, with columns in the configured controller joint order.
  void openLogFile()
  {
    closeLogFile();
    log_file_.clear();
    namespace fs = std::filesystem;
    const fs::path dir = log_dir_.empty() ? fs::path("/home/milkyway/log/hunter130") : fs::path(log_dir_);
    std::error_code error;
    fs::create_directories(dir, error);
    if (error) {
      RCLCPP_WARN(logger(), "Failed to create walk log directory %s: %s",
        dir.c_str(), error.message().c_str());
      return;
    }
    size_t run_id = 1;
    fs::path path;
    do {
      path = dir / ("walk_log_" + std::to_string(run_id++) + ".csv");
    } while (fs::exists(path, error) && !error);
    if (error) {
      RCLCPP_WARN(logger(), "Failed to select walk log file: %s", error.message().c_str());
      return;
    }
    log_path_ = path.string();
    log_file_.open(log_path_, std::ios::out | std::ios::trunc);
    if (!log_file_.is_open()) {
      RCLCPP_WARN(logger(), "Failed to open walk log file: %s", log_path_.c_str());
      return;
    }
    log_file_ << std::setprecision(17) << "time_ns";
    for (const char * field : {"mpos", "mvel", "meff", "cpos", "ckp", "ckd"}) {
      for (size_t i = 0; i < jointCount(); ++i) {
        log_file_ << "," << field << i;
      }
    }
    log_file_ << "\n";
    RCLCPP_INFO(logger(), "Walk log file: %s", log_path_.c_str());
  }

  void closeLogFile()
  {
    if (log_file_.is_open()) {
      log_file_.close();
      RCLCPP_INFO(logger(), "Walk log file closed: %s", log_path_.c_str());
    }
  }

  void writeLogRow(const rclcpp::Time & time)
  {
    if (!log_file_.is_open()) {return;}
    const size_t n = jointCount();
    log_file_ << time.nanoseconds();
    for (size_t i = 0; i < n; ++i) {log_file_ << "," << jointState(i).position;}
    for (size_t i = 0; i < n; ++i) {log_file_ << "," << jointState(i).velocity;}
    for (size_t i = 0; i < n; ++i) {log_file_ << "," << jointState(i).effort;}
    for (size_t i = 0; i < n; ++i) {log_file_ << "," << jointCommand(i).position;}
    for (size_t i = 0; i < n; ++i) {log_file_ << "," << jointCommand(i).kp;}
    for (size_t i = 0; i < n; ++i) {log_file_ << "," << jointCommand(i).kd;}
    // Buffered output; deactivation closes the stream and flushes the remaining rows.
    log_file_ << "\n";
    if (!log_file_) {
      RCLCPP_WARN(logger(), "Walk CSV write failed; logging disabled for this activation: %s",
        log_path_.c_str());
      log_file_.close();
    }
  }

  bool buildObservation()
  {
    // ec_imu_hardware already exports body-frame rad/s and ROS xyzw orientation.
    std::array<double, 7> imu{};
    std::array<bool, 7> found{};
    for (auto & interface : state_interfaces_) {
      if (interface.get_prefix_name() != "imu_sensor") {continue;}
      const auto & name = interface.get_interface_name();
      size_t index;
      if (name == "angular_velocity.x") {index = 0;}
      else if (name == "angular_velocity.y") {index = 1;}
      else if (name == "angular_velocity.z") {index = 2;}
      else if (name == "orientation.x") {index = 3;}
      else if (name == "orientation.y") {index = 4;}
      else if (name == "orientation.z") {index = 5;}
      else if (name == "orientation.w") {index = 6;}
      else {continue;}
      auto value = interface.get_optional();
      if (!value.has_value() || !std::isfinite(value.value())) {return false;}
      imu[index] = value.value();
      found[index] = true;
    }
    if (!std::all_of(found.begin(), found.end(), [](bool value) {return value;})) {return false;}
    const double norm_sq = imu[3] * imu[3] + imu[4] * imu[4] + imu[5] * imu[5] + imu[6] * imu[6];
    if (!std::isfinite(norm_sq) || norm_sq < 1e-12) {return false;}
    const double inv_norm = 1.0 / std::sqrt(norm_sq);
    const double qx = imu[3] * inv_norm, qy = imu[4] * inv_norm;
    const double qz = imu[5] * inv_norm, qw = imu[6] * inv_norm;
    obs_[0] = static_cast<float>(imu[0]);
    obs_[1] = static_cast<float>(imu[1]);
    obs_[2] = static_cast<float>(imu[2]);
    // R_WB^T * [0, 0, -1], a unit vector in pelvis coordinates.
    obs_[3] = static_cast<float>(-2.0 * (qx * qz - qw * qy));
    obs_[4] = static_cast<float>(-2.0 * (qy * qz + qw * qx));
    obs_[5] = static_cast<float>(-(qw * qw - qx * qx - qy * qy + qz * qz));
    obs_[6] = radio_vx_.load();
    obs_[7] = radio_vy_.load();
    obs_[8] = radio_wz_.load();
    const size_t n = policy_joints_.size();
    for (size_t p = 0; p < n; ++p) {
      const size_t joint = policy_to_joint_[p];
      obs_[9 + p] = static_cast<float>(jointState(joint).position - default_pos_[joint]);
      obs_[9 + n + p] = static_cast<float>(jointState(joint).velocity);
      obs_[9 + 2 * n + p] = actions_[p];
    }
    // Raw observations: no extra scaling/clipping before the embedded normalizer.
    return std::all_of(obs_.begin(), obs_.end(), [](float value) {return std::isfinite(value);});
  }

  void updateHistory()
  {
    if (!history_filled_) {
      for (int h = 0; h < num_history_; ++h) {
        std::copy(obs_.begin(), obs_.end(), obs_history_.begin() + h * obs_size_);
      }
      history_filled_ = true;
    } else {
      // Time-major history, oldest frame first and current frame last.
      std::memmove(obs_history_.data(), obs_history_.data() + obs_size_,
        (num_history_ - 1) * obs_size_ * sizeof(float));
      std::copy(obs_.begin(), obs_.end(), obs_history_.end() - obs_size_);
    }
  }

  std::vector<double> default_pos_, walk_kp_, walk_kd_, action_scale_;
  std::vector<std::string> policy_joints_;
  std::vector<size_t> joint_to_policy_, policy_to_joint_;
  int action_size_{25};
  int obs_size_{84};
  int num_history_{4};
  double inference_period_{0.02};
  double inference_accumulator_{0.0};
  std::vector<float> actions_, obs_, obs_history_;
  bool history_filled_{false};
  std::ofstream log_file_;
  std::string log_path_;
  std::string log_dir_;
  std::unique_ptr<Ort::Env> ort_env_;
  std::unique_ptr<Ort::Session> ort_session_;
  std::string input_name_, output_name_;
  std::vector<const char *> input_names_, output_names_;
  Ort::MemoryInfo mem_info_{nullptr};
  std::vector<int64_t> input_shape_;
  rclcpp::Subscription<ec_radio::msg::Radio>::SharedPtr radio_sub_;
  std::atomic<float> radio_vx_{0.0f}, radio_vy_{0.0f}, radio_wz_{0.0f};
};

} // namespace ec

PLUGINLIB_EXPORT_CLASS(ec::WalkController, controller_interface::ControllerInterface)
