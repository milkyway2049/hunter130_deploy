# ec_joint_hardware Project

## Project Context

`ec_joint_hardware` 是 ROS 2 Jazzy 的 `hardware_interface::SystemInterface` 插件，
以 `joint_sdk` 的 `DefaultJointManager` 管理关节和总线。插件库为
`ec_joint_hardware_plugins`，对外 pluginlib 名称必须保持为
`ec_joint_hardware/EcJointHardware`。

软件包使用 C++17、ament_cmake、pluginlib、rclcpp_lifecycle、nlohmann/json 和已安装的
`joint_sdk`。代码位于 `include/ec_joint_hardware/` 与 `src/`；Xacro 和 JSON 配置位于
工作区的 `robot_descriptions/robot_description/`。

本子仓库只有 `ec-joint-hardware` 一个能力域，因此不维护独立的长期主规格。活跃 change
中的规格是已批准、尚未实现的目标行为；在实现和验证前不得把它当成当前代码行为。

## Architecture

```text
ros2_control controller
        │ command/state interfaces
        ▼
EcJointHardware instance (one config_file)
        │ BaseJoint realtime APIs / GetStatus
        ▼
DefaultJointManager
        │ CreateAdapter / AddJoint / Commit
        ▼
joint_sdk adapter → Bus → motor transport
```

一个硬件实例只负责一个 JSON 配置文件。不同设备组可使用独立实例和独立的 controller
生命周期；`Commit()` 的同步边界只在该实例的 `DefaultJointManager` 所管理总线内成立，
不代表跨实例或跨物理总线的原子同步。

## Change Capability

- `ec-joint-hardware`：插件身份、JSON 边界、ros2_control 接口、控制下发、同步提交、
  生命周期和机器人描述集成。

## Technology and Dependencies

- ROS 2 Jazzy：`hardware_interface`、`pluginlib`、`rclcpp`、`rclcpp_lifecycle`。
- C++17；构建系统为 ament_cmake。
- `joint_sdk` 提供 `BaseJoint`、`DefaultJointManager`、`JointStatus` 与 adapter/bus。
- `nlohmann/json` 仅用于 JSON 文件和包级 DTO；joint 类型配置由 joint_sdk 解释。

## Build and Verification

从工作区根目录构建和测试本包：

```bash
colcon build --packages-select ec_joint_hardware --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select ec_joint_hardware
colcon test-result --verbose
```

实现变更还必须运行项目适用的 clang-format、clang-tidy、相关 GTest、Python 测试和
Xacro 展开检查。提交前运行 `openspec validate --all --strict`。

## Coding and Documentation Conventions

- 公开插件身份、`config_file` JSON 格式和现有关节名称是兼容性边界。
- 不在硬件层复制 joint_sdk 的 joint 类型、限位、电机地址、adapter 复用或诊断保护。
- 对包级 JSON DTO 使用 nlohmann/json 的 `at()`、`get_to()` 和异常；不要以
  `operator[]` 悄然插入缺失字段。
- 生命周期回调和 `read()`/`write()` 不允许让 SDK 异常越过 ROS 2 控制边界。
- 所有实时控制值来自 controller；不得以缓存值、零值或推断模式填补不完整命令。

## Global Constraints and Limitations

- SDK 的 `Commit()` 仅将本批次交给 adapter，不等待物理链路发送或设备响应；物理同步
  及硬实时保证为 **UNKNOWN**。
- `RJoint`、双耦合 joint 与 Bus 操作路径可抛出 `std::exception` 派生异常。
- 多硬件实例可独立生命周期，但对不同物理总线不存在由本插件提供的全局同步保证。
- 真实 EtherCAT、CAN 和心跳失能依赖外部设备、主站、权限与固件，不能由单元测试完全
  证明。
