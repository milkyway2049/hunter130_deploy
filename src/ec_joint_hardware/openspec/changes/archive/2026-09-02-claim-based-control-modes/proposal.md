## Why

当前插件用 `control_mode` 命令接口逐周期切换控制模式，把模式正确性压到每个控制
周期的运行时校验上；但控制模式本质是控制器级决策——运行中途切换模式没有正当场景，
反而引入模式抖动、参数半套切换等风险面。模式应随控制器生命周期固定，控制器切换走
ros2_control 官方的 `prepare_command_mode_switch` 机制，由硬件在切换点一次性
校验认领集合的合法性。

同时精简两个已无意义的接口：新电机固件只保留全刹停止，`stop_mode` 的三种制动选择
不再存在；电机反馈固定请求完整状态，`feedback` 的反馈类型选择也不需要暴露给
controller。

## What Changes

- **BREAKING**：删除 `control_mode` 命令接口。不再支持运行中逐周期切换控制模式；
  模式由控制器认领的命令接口集合唯一确定，并在该控制器激活期间固定。
- **BREAKING**：删除 `stop_mode` 命令接口。新电机固件只有全刹模式，停止固定映射
  SDK 的 `MotorStopMode::FullBrake`。
- **BREAKING**：删除 `feedback` 命令接口。电机反馈固定请求完整状态，SDK 控制调用
  一律以 `feedback=1` 下发，controller 不再选择反馈类型。
- 新增 `stop_current` 命令接口：停止控制器认领 `{stop_current}`，写入制动电流值即
  请求全刹停止。
- 控制模式与认领集合的映射固定为：PVT `{position, velocity, effort, kp, kd,
  torque_mode}`、位置 `{position, velocity, current}`、速度 `{velocity, current}`、
  电流 `{current}`、扭矩 `{effort}`、停止 `{stop_current}`。
- 实现 `prepare_command_mode_switch()` / `perform_command_mode_switch()`：任一关节
  的认领集合不完整、不匹配任一合法模式或与其他控制器冲突时，拒绝本次控制器切换。
- `write()` 按切换时记录的每关节模式校验必填值并派发对应 SDK 调用；校验与派发合并为
  单循环，整批失败不提交、成功恰好一次 `Commit()`、每周期命令复位 NaN 的语义保留。
- `on_configure()` 创建 adapters 后启用其 sync 模式（软同步）：SDK 控制调用排队等待
  `Commit()`，`Commit()` 成为命令到达总线的唯一边界，从首个写周期起成立。
- `read()`、反馈看门狗、激活准入、电机错误边沿日志、停用语义均不变。
- 更新 Xacro 宏、README、测试与本 change 的 OpenSpec 规格。

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `ec-joint-hardware`：以认领集合确定模式的契约替换 `control_mode` 运行时切换；
  删除 `stop_mode` 与 `feedback`，停止固定全刹、反馈固定完整状态；新增控制器切换
  校验需求。

## Impact

- 修改 `include/ec_joint_hardware/`（接口名集合、每关节模式记录）、
  `src/ec_joint_hardware.cpp`（模式切换回调、write 派发）、Xacro
  `ec_joint_hardware_joint_interfaces` 宏、README 与全部相关测试。
- 所有使用该硬件的 controller 必须改为按模式认领固定接口集合并每周期写全部必填值；
  现有 MIT controller 认领 PVT 集合，不再需要写 `control_mode` 或选择反馈类型。
- JSON 配置格式、插件加载名、生命周期与状态接口不变；不新增第三方依赖。
