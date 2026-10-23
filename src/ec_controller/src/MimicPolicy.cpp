#include "ec_controller/MimicPolicy.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace ec::mimic
{
namespace
{
constexpr std::array<int, 7> kHistory{0, 1, 2, 3, 4, 8, 16};
constexpr std::array<int, 8> kFuture{-8, -4, -2, 0, 1, 2, 3, 4};

std::vector<std::string> split(const std::string & line)
{
  std::istringstream stream(line);
  std::vector<std::string> result;
  std::string cell;
  while (std::getline(stream, cell, ',')) {result.push_back(cell);}
  return result;
}

Joints jointVector(const nlohmann::json & json, const std::string & name)
{
  const auto values = json.at(name).get<std::vector<double>>();
  if (values.size() != kJoints) {throw std::runtime_error(name + " must have 25 values");}
  Joints result;
  std::copy(values.begin(), values.end(), result.data());
  if (!result.allFinite()) {throw std::runtime_error(name + " contains non-finite values");}
  return result;
}
}  // namespace

bool Sample::valid() const
{
  return position.allFinite() && velocity.allFinite() && angular_velocity.allFinite() &&
         orientation.coeffs().allFinite() && orientation.squaredNorm() > 1e-12;
}

Eigen::Matrix3d projectedYaw(const Eigen::Quaterniond & orientation)
{
  const Eigen::Matrix3d rotation = orientation.normalized().toRotationMatrix();
  const Eigen::Vector3d x = rotation.col(0), z = rotation.col(2);
  Eigen::Vector2d heading = x.head<2>();
  if (heading.norm() <= 0.1) {heading = (x.z() < 0 ? 1.0 : -1.0) * z.head<2>();}
  return Eigen::AngleAxisd(std::atan2(heading.y(), heading.x()), Eigen::Vector3d::UnitZ())
         .toRotationMatrix();
}

void Policy::load(
  const std::string & model, const std::string & metadata_path, const std::string & motion)
{
  std::ifstream stream(metadata_path);
  nlohmann::json json;
  if (!stream || !(stream >> json)) {throw std::runtime_error("Cannot load MimicLite metadata");}
  metadata_.joints = json.at("joint_names").get<std::vector<std::string>>();
  if (metadata_.joints.size() != kJoints ||
    std::set<std::string>(metadata_.joints.begin(), metadata_.joints.end()).size() != kJoints ||
    json.at("history_steps").get<std::vector<int>>() != std::vector<int>(kHistory.begin(), kHistory.end()) ||
    json.at("future_steps").get<std::vector<int>>() != std::vector<int>(kFuture.begin(), kFuture.end()))
  {
    throw std::runtime_error("Unsupported MimicLite joint/history/future layout");
  }
  metadata_.default_position = jointVector(json, "default_joint_pos");
  metadata_.kp = jointVector(json, "kp");
  metadata_.kd = jointVector(json, "kd");
  metadata_.scale = jointVector(json, "action_scale");
  metadata_.lower = jointVector(json, "joint_lower");
  metadata_.upper = jointVector(json, "joint_upper");
  metadata_.control_dt = json.at("control_dt");
  metadata_.actuator_dt = json.at("actuator_dt");
  metadata_.alpha = json.at("action_alpha");
  metadata_.delay = json.at("action_delay_substeps");
  if (std::abs(metadata_.control_dt - .02) > 1e-9 ||
    std::abs(metadata_.actuator_dt - .005) > 1e-9 || !std::isfinite(metadata_.alpha) ||
    metadata_.alpha <= 0 || metadata_.alpha > 1 || metadata_.delay < 0 || metadata_.delay > 3 ||
    (metadata_.kp.array() < 0).any() || (metadata_.kd.array() < 0).any() ||
    (metadata_.scale.array() <= 0).any() ||
    (metadata_.lower.array() >= metadata_.upper.array()).any() ||
    (metadata_.default_position.array() < metadata_.lower.array()).any() ||
    (metadata_.default_position.array() > metadata_.upper.array()).any())
  {
    throw std::runtime_error("Invalid MimicLite timing, limits or control parameters");
  }

  std::ifstream csv(motion);
  std::string line;
  if (!csv || !std::getline(csv, line)) {throw std::runtime_error("Cannot load motion CSV: " + motion);}
  auto header = split(line);
  std::vector<std::string> expected{"time", "root_tx", "root_ty", "root_tz",
    "root_qw", "root_qx", "root_qy", "root_qz"};
  expected.insert(expected.end(), metadata_.joints.begin(), metadata_.joints.end());
  if (header != expected) {throw std::runtime_error("Motion CSV header/joint order mismatch");}
  frames_.clear();
  while (std::getline(csv, line)) {
    const auto cells = split(line);
    if (cells.size() != expected.size()) {throw std::runtime_error("Motion CSV row width mismatch");}
    std::array<double, 33> values{};
    for (size_t i = 0; i < cells.size(); ++i) {
      size_t consumed = 0;
      values[i] = std::stod(cells[i], &consumed);
      if (consumed != cells[i].size() || !std::isfinite(values[i])) {
        throw std::runtime_error("Invalid motion CSV number");
      }
    }
    if (std::abs(values[0] - frames_.size() * metadata_.control_dt) > 1e-6) {
      throw std::runtime_error("Motion must be uniformly sampled at 50Hz starting at time zero");
    }
    Frame frame;
    frame.root_position = Eigen::Vector3d(values[1], values[2], values[3]);
    frame.orientation = Eigen::Quaterniond(values[4], values[5], values[6], values[7]);
    if (std::abs(frame.orientation.norm() - 1.) > .001) {
      throw std::runtime_error("Motion quaternion is not unit length");
    }
    frame.orientation.normalize();
    std::copy(values.begin() + 8, values.end(), frame.position.data());
    if ((frame.position.array() < metadata_.lower.array() - 1e-5).any() ||
      (frame.position.array() > metadata_.upper.array() + 1e-5).any())
    {
      throw std::runtime_error("Reference joint position exceeds robot limits");
    }
    frames_.push_back(frame);
  }
  if (frames_.empty()) {throw std::runtime_error("Motion has no frames");}

  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(1);
  options.SetInterOpNumThreads(1);
  session_ = Ort::Session(env_, model.c_str(), options);
  if (session_.GetInputCount() != 2 || session_.GetOutputCount() != 1) {
    throw std::runtime_error("Expected two MimicLite inputs and one output");
  }
  Ort::AllocatorWithDefaultOptions allocator;
  for (size_t i = 0; i < 3; ++i) {
    const bool output = i == 2;
    const auto name = output ? session_.GetOutputNameAllocated(0, allocator) :
      session_.GetInputNameAllocated(i, allocator);
    auto type = output ? session_.GetOutputTypeInfo(0) : session_.GetInputTypeInfo(i);
    const auto info = type.GetTensorTypeAndShapeInfo();
    const auto dims = info.GetShape();
    const std::string key(name.get());
    const int width = key == "policy" ? 467 : key == "command" ? 272 :
      (output && key == "action" ? 25 : -1);
    if (width < 0 || (output && key != "action") || (!output && key == "action") ||
      dims.size() != 2 || (dims[0] != -1 && dims[0] != 1) || dims[1] != width ||
      info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
    {
      throw std::runtime_error("ONNX names, shapes or types do not match Hunter130 MimicLite");
    }
  }
  reset_ = false;
}

void Policy::reset(const Sample & sample, bool align_heading)
{
  if (!sample.valid() || frames_.empty()) {throw std::runtime_error("Invalid initial robot state");}
  history_.fill(sample);
  actions_.fill(Action{});
  heading_offset_ = Eigen::Matrix3d::Identity();
  if (align_heading) {
    heading_offset_ = projectedYaw(frames_.front().orientation) * projectedYaw(sample.orientation).transpose();
  }
  // Start actuation at the prepared reference pose to avoid a setpoint jump.
  // Raw action observation history still starts at zero, as in training.
  applied_ = (frames_.front().position - metadata_.default_position).cwiseQuotient(metadata_.scale);
  for (auto & action : actuation_history_) {
    for (size_t j = 0; j < kJoints; ++j) {action[j] = static_cast<float>(applied_[j]);}
  }
  actuator_elapsed_ = 0;
  substep_ = 0;
  reset_ = true;
}

void Policy::buildObservation(const Sample & sample, size_t frame_index)
{
  for (size_t h = history_.size() - 1; h > 0; --h) {history_[h] = history_[h - 1];}
  history_[0] = sample;
  size_t cursor = 0;
  for (int h : kHistory) {
    for (int j = 0; j < 3; ++j) {observations_[cursor++] = static_cast<float>(history_[h].angular_velocity[j]);}
  }
  for (int h : kHistory) {
    const Eigen::Vector3d gravity = history_[h].orientation.normalized().conjugate() * Eigen::Vector3d(0, 0, -1);
    for (int j = 0; j < 3; ++j) {observations_[cursor++] = static_cast<float>(gravity[j]);}
  }
  for (int h : kHistory) {
    for (size_t j = 0; j < kJoints; ++j) {observations_[cursor++] = static_cast<float>(history_[h].position[j]);}
  }
  for (int h : kHistory) {
    for (size_t j = 0; j < kJoints; ++j) {observations_[cursor++] = static_cast<float>(history_[h].velocity[j]);}
  }
  for (const auto & action : actions_) {
    for (float value : action) {observations_[cursor++] = value;}
  }

  const auto boundedFrame = [this, frame_index](int offset) -> const Frame & {
      const auto index = std::clamp<int64_t>(static_cast<int64_t>(frame_index) + offset,
          0, static_cast<int64_t>(frames_.size()) - 1);
      return frames_[static_cast<size_t>(index)];
    };
  const Frame & current = frames_[std::min(frame_index, frames_.size() - 1)];
  Eigen::Vector3d origin = current.root_position;
  origin.z() = 0;
  const Eigen::Matrix3d yaw_inverse = projectedYaw(current.orientation).transpose();
  cursor = 0;
  for (int offset : kFuture) {
    const Eigen::Vector3d local = yaw_inverse * (boundedFrame(offset).root_position - origin);
    for (int j = 0; j < 3; ++j) {command_[cursor++] = static_cast<float>(local[j]);}
  }
  const Eigen::Matrix3d robot_inverse = (heading_offset_ * sample.orientation.normalized().toRotationMatrix()).transpose();
  for (int offset : kFuture) {
    const Eigen::Matrix3d relative = robot_inverse * boundedFrame(offset).orientation.toRotationMatrix();
    for (int row = 0; row < 2; ++row) {
      for (int col = 0; col < 3; ++col) {command_[cursor++] = static_cast<float>(relative(row, col));}
    }
  }
  for (int offset : kFuture) {
    for (size_t j = 0; j < kJoints; ++j) {command_[cursor++] = static_cast<float>(boundedFrame(offset).position[j]);}
  }
}

Action Policy::infer(const Sample & sample, size_t frame_index)
{
  if (!reset_ || !sample.valid()) {throw std::runtime_error("Missing reset or invalid robot state");}
  buildObservation(sample, frame_index);
  if (!std::all_of(observations_.begin(), observations_.end(), [](float x) {return std::isfinite(x);}) ||
    !std::all_of(command_.begin(), command_.end(), [](float x) {return std::isfinite(x);}))
  {
    throw std::runtime_error("Non-finite MimicLite observation");
  }
  const std::array<int64_t, 2> policy_shape{1, 467}, command_shape{1, 272};
  std::array<Ort::Value, 2> inputs{
    Ort::Value::CreateTensor<float>(memory_, observations_.data(), observations_.size(), policy_shape.data(), 2),
    Ort::Value::CreateTensor<float>(memory_, command_.data(), command_.size(), command_shape.data(), 2)};
  constexpr std::array<const char *, 2> names{"policy", "command"};
  constexpr std::array<const char *, 1> outputs{"action"};
  auto result = session_.Run(Ort::RunOptions{nullptr}, names.data(), inputs.data(), 2, outputs.data(), 1);
  if (result[0].GetTensorTypeAndShapeInfo().GetShape() != std::vector<int64_t>{1, 25}) {
    throw std::runtime_error("Invalid MimicLite action shape");
  }
  Action action;
  std::copy_n(result[0].GetTensorData<float>(), kJoints, action.begin());
  if (!std::all_of(action.begin(), action.end(), [](float x) {return std::isfinite(x);})) {
    throw std::runtime_error("Non-finite MimicLite action");
  }
  actions_[2] = actions_[1];
  actions_[1] = actions_[0];
  actions_[0] = action;
  actuation_history_[2] = actuation_history_[1];
  actuation_history_[1] = actuation_history_[0];
  actuation_history_[0] = action;
  substep_ = 0;
  actuator_elapsed_ = 0;
  filterSubstep();
  return action;
}

void Policy::filterSubstep()
{
  const int delay_index = std::clamp((metadata_.delay - substep_ + 3) / 4, 0, 2);
  for (size_t j = 0; j < kJoints; ++j) {
    applied_[j] += metadata_.alpha * (actuation_history_[delay_index][j] - applied_[j]);
  }
  ++substep_;
}

void Policy::advanceControl(double dt)
{
  if (!reset_ || !std::isfinite(dt) || dt < 0 || dt > .1) {
    throw std::runtime_error("Invalid MimicLite control interval");
  }
  actuator_elapsed_ += dt;
  while (actuator_elapsed_ + 1e-12 >= metadata_.actuator_dt) {
    actuator_elapsed_ -= metadata_.actuator_dt;
    filterSubstep();
  }
}

Joints Policy::target() const
{
  return (metadata_.default_position + metadata_.scale.cwiseProduct(applied_))
         .cwiseMax(metadata_.lower).cwiseMin(metadata_.upper);
}
}  // namespace ec::mimic
