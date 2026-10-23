#ifndef EC_CONTROLLER_MIMIC_POLICY_H_
#define EC_CONTROLLER_MIMIC_POLICY_H_

#include <array>
#include <string>
#include <vector>

#include <Eigen/Geometry>
#include <onnxruntime_cxx_api.h>

namespace ec::mimic
{
constexpr size_t kJoints = 25;
using Joints = Eigen::Matrix<double, kJoints, 1>;
using Action = std::array<float, kJoints>;

struct Sample
{
  Joints position{Joints::Zero()}, velocity{Joints::Zero()};
  Eigen::Vector3d angular_velocity{Eigen::Vector3d::Zero()};
  Eigen::Quaterniond orientation{Eigen::Quaterniond::Identity()};
  bool valid() const;
};

struct Frame
{
  Eigen::Vector3d root_position;
  Eigen::Quaterniond orientation;
  Joints position;
};

struct Metadata
{
  std::vector<std::string> joints;
  Joints default_position, kp, kd, scale, lower, upper;
  double control_dt{0.02}, actuator_dt{0.005}, alpha{0.9};
  int delay{2};
};

Eigen::Matrix3d projectedYaw(const Eigen::Quaterniond & orientation);

// No ROS or hardware access: the same engine is exercised by offline replay.
class Policy
{
public:
  void load(const std::string & model, const std::string & metadata, const std::string & motion);
  void reset(const Sample & sample, bool align_heading = true);
  Action infer(const Sample & sample, size_t frame);
  void advanceControl(double dt);
  Joints target() const;
  const Metadata & metadata() const {return metadata_;}
  const Frame & frame(size_t index) const {return frames_.at(index);}
  size_t frameCount() const {return frames_.size();}
  const std::array<float, 467> & observations() const {return observations_;}
  const std::array<float, 272> & command() const {return command_;}

private:
  void buildObservation(const Sample & sample, size_t frame);
  void filterSubstep();
  Metadata metadata_;
  std::vector<Frame> frames_;
  std::array<Sample, 17> history_;
  std::array<Action, 3> actions_{}, actuation_history_{};
  Eigen::Matrix3d heading_offset_{Eigen::Matrix3d::Identity()};
  Joints applied_{Joints::Zero()};
  std::array<float, 467> observations_{};
  std::array<float, 272> command_{};
  double actuator_elapsed_{0.0};
  int substep_{0};
  bool reset_{false};
  Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "Hunter130MimicLite"};
  Ort::Session session_{nullptr};
  Ort::MemoryInfo memory_{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
};
}  // namespace ec::mimic
#endif  // EC_CONTROLLER_MIMIC_POLICY_H_
