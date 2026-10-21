## ADDED Requirements

### Requirement: 控制器切换后的首帧命令门

`EcJointHardware` MUST 保存一个实例级的「控制器切换后的首帧命令门」（下称门），默认关闭。
`perform_command_mode_switch()` MUST 在写入任何关节的新模式之前开门，使 `write()` 一旦能看到新模式就必然
已经能看到门状态；`prepare_command_mode_switch()` 通过校验后 SHOULD 开门（只为提前静默，MUST NOT 因此修改
任何模式）。

门 MUST 只取决于"本实例已认领的关节是否都已写入命令"，MUST NOT 取决于控制模式是否发生变化：同一关节的
控制器换手而认领集合不变时同样 MUST 适用。`write()` MUST 在逐关节派发之前判断门：存在任何一个已绑定模式
（其模式不是 `ModeDefinition::None`）的关节在本周期没有任何命令槽被写入时，MUST 整批跳过本周期——不调用任何
SDK 控制接口、不记录非法命令错误日志、不合成保持命令或上一周期命令；只有全部已认领关节都至少写入一个命令槽
时，MUST 关门并在同一周期照常派发。命令槽每周期结束时被重置为 `NaN`，因此"任一槽非 `NaN`"等价于"该关节
本周期被写入命令"。未绑定模式的关节 MUST NOT 参与该判定。

门 MUST NOT 设置超时：控制器持续不写命令时该实例持续处于门内静默，硬件 MUST NOT 因此记录周期性错误日志。
`on_configure()` MUST 清除门状态。

#### Scenario: 切换后首帧命令到达前静默

- **WHEN** 一次切换把某关节的模式由 `Position` 改为 `Pvt`，且切换后本实例仍无任何命令槽被写入
- **THEN** 本周期 MUST 整批跳过：MUST NOT 调用任何 SDK 控制接口，MUST NOT 记录非法命令错误日志

#### Scenario: 只有部分已认领关节写入时继续跳过

- **WHEN** 门内某周期本实例的部分已认领关节被写入命令，其余已认领关节仍未写入
- **THEN** 本周期 MUST 继续整批跳过，已写入命令的关节 MUST NOT 派发

#### Scenario: 全部已认领关节写入即恢复派发

- **WHEN** 门内某周期本实例每个已认领关节都至少写入一个命令槽
- **THEN** `write()` MUST 关门并在该周期按模式派发；此后未写命令的关节重新适用「逐关节命令校验与错误日志」

#### Scenario: 未绑定模式的关节不参与判定

- **WHEN** 本实例存在未被任何控制器认领的关节
- **THEN** 这些关节 MUST NOT 阻塞关门；门关闭后它们 MUST 仍按未绑定模式的既有派发语义处理

#### Scenario: 控制器持续不写命令时持续等待

- **WHEN** 门开启后本实例始终没有命令槽被写入
- **THEN** 该实例 MUST 持续处于门内静默，硬件 MUST NOT 记录周期性非法命令错误日志，也 MUST NOT 超时上报

#### Scenario: 重建清空门状态

- **WHEN** `on_configure()` 重建本实例的关节运行时时
- **THEN** 门 MUST 处于关闭状态

## MODIFIED Requirements

### Requirement: 认领集合与模式参数映射

每次 active `write()` 中，插件 MUST 按 `perform_command_mode_switch()` 记录的每关节
模式派发控制。关节的所有命令存储在每周期 `write()` 结束时 MUST 重置为 `NaN`。

已绑定模式的关节必填接口全部为 `NaN` 表示控制器本周期未写命令——控制器已绑定却未写入
即视为非法命令，适用逐关节校验语义（跳过该关节并记录错误日志）；但处于「控制器切换后的
首帧命令门」内的 `write()` 周期 MUST NOT 记录该错误日志。必填接口中任一（但非
全部）为 `NaN`、无穷或不满足整数/枚举语义时，同样适用逐关节校验语义。

各模式必填参数语义：连续量接口 MUST 为有限值；`torque_mode` MUST 为 `0`（MotorPVT）、
`1`（HostTorque：SDK 完整 PD 加前馈解算）或 `2`（HostKpMotorKd：SDK Kp 加前馈、电机本地 Kd）的精确值；`stop_current` 的值即停止时施加的制动
电流。停止固定使用 SDK 的 `MotorStopMode::FullBrake`，带反馈参数的
SDK 调用 MUST 固定以 `feedback=1` 下发。同一模式下未列入认领集合的 interface 不参与
该周期的调用。

controller 在任一 active 控制周期需要控制某 joint 时，MUST 写入其模式全部必填
参数；controller 停用后不会遗留旧目标。

#### Scenario: PVT 模式直接转发

- **WHEN** 某关节已绑定 PVT 模式，且 `position`、`velocity`、`effort`、`kp`、`kd`、
  `torque_mode` 均为有限值，且 `torque_mode` 为精确的 `0`、`1` 或 `2`
- **THEN** 插件 MUST 以同一组值调用该 joint 的 `PVTControl()`，并传递
  对应的 `encos::PVTControlMode` 枚举值，不得转换为布尔值或合并模式 1 与模式 2

#### Scenario: 本周期未写命令的关节

- **WHEN** 某关节已绑定模式，但其全部必填接口在本周期均为 `NaN`（控制器未写命令），
  且本次 `write()` 不处于「控制器切换后的首帧命令门」内
- **THEN** 该关节命令被跳过并记录日志；持续未写命令将每周期产生错误日志，提示
  控制器已绑定但未写入命令

#### Scenario: 扭矩模式值越界

- **WHEN** 已绑定 PVT 模式的关节的 `torque_mode` 不属于精确的 `0`、`1`、`2`
- **THEN** 插件 MUST 判定为非法必填值，记录日志并跳过该关节；其他关节继续派发，
  `write()` 返回 `OK`（仅当 SDK 调用或 `Commit()` 抛出异常时返回 `ERROR`）

### Requirement: 逐关节命令校验与错误日志

插件 MUST 在逐关节处理中校验本周期每一个绑定模式的 joint：其绑定模式的必填 command interface MUST 均为有限值（`torque_mode`
MUST 为 `0`、`1` 或 `2` 的精确值），其余连续量接口 MUST 为有限值。任一必填接口为 `NaN`、无穷或非法时，该关节本周期命令被跳过，插件
MUST 记录一次包含关节名、模式与缺失/非法接口名的错误日志。其余关节的命令 MUST 正常派发；`write()` MUST 在
循环结束后恰好调用一次 `manager_.Commit()`，且仅在 SDK 控制调用或 `Commit()` 抛出异常时返回 `ERROR`。
处于「控制器切换后的首帧命令门」内的 `write()` 周期 MUST NOT 记录该错误日志、MUST NOT 视为校验失败，
也 MUST NOT 因此产生 SDK 控制调用。

`write()` MUST NOT 因单个关节的校验失败而阻断其他关节的派发与提交。每个有效命令关节 MUST 按其绑定模式调用
一次对应 SDK 控制接口；`write()` MUST 每周期恰好调用一次 `manager_.Commit()`；sync 模式下 `Commit()` 是命令到达总线的
唯一边界。

#### Scenario: 非法命令的关节被跳过并记录日志

- **WHEN** 已绑定模式的某 joint 的必填接口为 `NaN`、无穷或不满足整数语义（如 `torque_mode` 非 0/1/2）
- **THEN** `write()` MUST 记录一次包含该关节名、模式与缺失/非法接口名的错误日志，跳过该关节，其余关节照常派发；
  `write()` 返回 `OK`（仅当 SDK 调用或 `Commit()` 抛出异常时才返回 `ERROR`）

#### Scenario: 非法命令不阻断其他关节

- **WHEN** 多关节批次中至少一个 joint 的命令非法
- **THEN** 该关节被跳过并记录日志，其余关节的合法命令 MUST 正常派发并 `Commit()`

#### Scenario: 控制器停写即视为非法命令

- **WHEN** 已绑定模式的关节必填接口全为 `NaN`（控制器本周期未写命令），且本次
  `write()` 不处于「控制器切换后的首帧命令门」内
- **THEN** 该关节命令被跳过并记录日志；持续未写命令将每周期产生错误日志，
  提示控制器已绑定但未写入命令

#### Scenario: PVT 调用失败触发全刹路径

- **WHEN** 一个参数完整的 PVTControl 调用抛出 `std::exception`
- **THEN** `write()` MUST 记录 joint、模式和异常并返回 `ERROR`；硬件层 MUST NOT 自动
  全刹、重试或提交清理批次

### Requirement: 工作区集成、文档与测试

`robot_descriptions/robot_description/xacro/ros2_control/ec_joint_hardware.xacro` 中的 `ec_joint_hardware_joint_interfaces` 宏 MUST 生成本规范定义的 8 个 command interface
与 6 个 state interface。所有使用该宏的机械臂与灵巧手描述 MUST 自动继承该集合。
机器人描述 MUST 保持按独立设备组声明多个 `<ros2_control>` / `EcJointHardware` 实例，
每个实例只传入自己的 `config_file`。机器人描述与插件 MUST NOT 声明 `soft_sync` 或
`ec_joint_hardware_soft_sync` 参数。

所有使用 `ec_joint_hardware` 的 controller MUST 按其控制模式认领对应的固定接口集合，
并在需要控制的每个周期写入该模式的全部必填接口。controller MAY 独立启动或停止；
模式随控制器激活绑定，切换控制器即切换模式，由硬件在命令模式切换回调中校验。

README MUST 以本规范的接口表、认领集合-模式表、同步提交保证、不完整命令 `ERROR`
语义和「控制器切换后的首帧命令门」（实例级、门内不派发、不记录错误、无超时等待）面向 controller
开发者编写，并 MUST 说明门没有超时带来的代价。

软件包测试 MUST 覆盖：

- nlohmann/json 根层反序列化后将完整 joint JSON 委托给 SDK；
- 每个实例只加载其 `config_file`，并且 adapter 创建不经过硬件层冲突或复用管理；
- 每关节完整的 8 command / 6 state 导出与 Xacro 一致；
- 六种认领集合对应的模式在切换回调中被正确接受、记录；
- 不完整、跨模式混合或多控制器冲突的认领在 `prepare_command_mode_switch()` 被拒绝；
- PVT、位置、速度、电流、扭矩和停止六种模式的正确 SDK 派发，停止固定为 FullBrake，
  带反馈参数的调用固定 `feedback=1`；
- 单一硬件实例内成功的多关节写入只调用一次 `Commit()`，且不承诺跨实例同步；
- 本周期未写命令的关节被跳过，且已停止 controller 不会留下可被后续周期重复使用的
  command 值；
- 「控制器切换后的首帧命令门」内 MUST 整批跳过且不记录错误，全部已认领关节写入首帧命令后 MUST 恢复派发，
  且未绑定模式的关节 MUST NOT 阻塞关门；
- 必填参数部分为 `NaN` 或非法时，仅记录错误并返回 `ERROR`，且没有控制、停止或
  提交调用；
- SDK `JointStatus` 的六项状态完整发布；
- active `read()` 在 SDK 反馈缺失（含看门狗过期）时返回 `ERROR`；
- `on_activate()` 仅在所有已配置 joint 取得完整有效初始反馈后成功，超时或 SDK 异常时
  返回 `ERROR`，且每一实际发起反馈请求的轮次只提交一次；
- 停用后没有 SDK 控制或提交调用。

提交前 MUST 运行仓库适用的 clang-format、clang-tidy、构建和 `ctest`，并展开相关
Xacro 以验证硬件导出接口与 controller 认领接口完全一致。

#### Scenario: 机器人描述不携带提交开关

- **WHEN** 生成任一 ec joint `<ros2_control>` 硬件块
- **THEN** 它 MUST 只传入 `config_file`，且 MUST NOT 声明 `soft_sync` 或
  `ec_joint_hardware_soft_sync`

#### Scenario: 机器人描述接口集合与实现一致

- **WHEN** 展开任一使用 `ec_joint_hardware_joint_interfaces` 宏的机器人描述
- **THEN** 每个关节 MUST 声明恰好 8 个 command interface 与 6 个 state interface，
  且不含 `control_mode`、`stop_mode` 或 `feedback`
