## ADDED Requirements

### Requirement: 控制器切换与模式绑定

插件 MUST 通过 ros2_control 标准命令模式切换机制绑定控制模式：实现
`prepare_command_mode_switch()` 与 `perform_command_mode_switch()`。每个关节被任一
控制器认领的命令接口集合 MUST 恰好等于下列合法模式集合之一，或为空（关节不被任何
控制器控制）：

| 模式 | 认领的命令接口集合 | 下发的 SDK 调用 |
| --- | --- | --- |
| PVT | `position`、`velocity`、`effort`、`kp`、`kd`、`torque_mode` | `PVTControl(kp, kd, position, velocity, effort, torque_mode)` |
| 位置 | `position`、`velocity`、`current` | `PosControl(position, velocity, current, 1)` |
| 速度 | `velocity`、`current` | `SpdControl(velocity, current, 1)` |
| 电流 | `current` | `CurControl(current, 1)` |
| 扭矩 | `effort` | `TorControl(effort, 1)` |
| 停止 | `stop_current` | `Stop(FullBrake, stop_current, 1)` |

所有带反馈参数的 SDK 调用 MUST 以 `feedback=1`（请求完整状态）下发，controller 不
参与反馈类型选择。

`prepare_command_mode_switch()` MUST 在应用起停列表后的最终认领状态上校验：任一关节
的认领集合不是合法模式集合（不完整、跨模式混合或来自多个控制器的冲突认领）时，
MUST 拒绝本次切换。`perform_command_mode_switch()` MUST 记录每关节由此确定的模式；
模式在该控制器保持激活期间 MUST NOT 改变，运行中不存在逐周期切换模式的途径。

#### Scenario: 合法认领的控制器切换被接受

- **WHEN** 一个位置控制器激活并恰好认领某关节的 `position`、`velocity`、`current`
- **THEN** `prepare_command_mode_switch()` MUST 接受该切换，该关节模式被记录为位置模式

#### Scenario: 不完整认领被拒绝

- **WHEN** 一个控制器只认领某关节的 `position`、`velocity` 而缺少 `current`
- **THEN** `prepare_command_mode_switch()` MUST 拒绝该切换，关节模式保持不变

#### Scenario: 跨模式混合认领被拒绝

- **WHEN** 同一关节被认领的接口集合混合了两个模式的成员（如 `kp` 与 `current`）
- **THEN** `prepare_command_mode_switch()` MUST 拒绝该切换

#### Scenario: 同一关节的多控制器冲突被拒绝

- **WHEN** 两个控制器分别认领同一关节的不同接口（如一个认领 `effort`，另一个认领
  `current`）
- **THEN** `prepare_command_mode_switch()` MUST 拒绝该切换，防止一个关节被两个控制器
  同时驱动

#### Scenario: 停止模式与电流模式可区分

- **WHEN** 控制器认领 `{stop_current}` 而非 `{current}`
- **THEN** 该关节 MUST 被记录为停止模式而非电流模式

#### Scenario: 未被认领的关节允许存在

- **WHEN** 某关节不被任何控制器认领
- **THEN** 切换 MUST 被接受，该关节不参与控制派发

## MODIFIED Requirements

### Requirement: ros2_control 接口全集

每个 ros2_control joint MUST 声明且硬件 MUST 导出下列 8 个唯一 command interface：

- `position`
- `velocity`
- `effort`
- `current`
- `kp`
- `kd`
- `torque_mode`
- `stop_current`

每个 joint MUST 同时声明并导出下列 6 个唯一 state interface：

- `position`：`JointStatus::pos`，单位 rad。
- `velocity`：`JointStatus::spd`，单位 rad/s。
- `effort`：`JointStatus::tor`，单位 N·m。
- `mos_temperature`：`JointStatus::mosTemperature`，单位 °C。
- `motor_temperature`：`JointStatus::motorTemperature`，单位 °C。
- `motor_error`：`JointStatus::error` 的数值枚举。

`on_init()` MUST 拒绝缺失、重复或额外替代上述必需接口集合的硬件描述。Xacro
`ec_joint_hardware_joint_interfaces` 宏 MUST 使用相同的完整集合，所有复用该宏的
机器人描述 MUST 因此声明相同接口。

#### Scenario: 导出完整实时控制与状态接口

- **WHEN** 硬件描述含有 N 个正确声明的 joint
- **THEN** 插件 MUST 导出 `8 * N` 个 command interface 和 `6 * N` 个 state
  interface，名称与 Xacro 声明逐一一致

#### Scenario: 拒绝旧的 MIT 专用接口集合

- **WHEN** 任一 joint 仅声明旧的 `position`、`velocity`、`effort`、`kp`、`kd`
  command interface
- **THEN** `on_init()` MUST 返回 `ERROR`

#### Scenario: 旧 MIT/PVT interface 集合

- **WHEN** 一个 joint 只声明旧版的五个 command interface 与三个 state interface
- **THEN** `on_init()` MUST 返回 `ERROR`，因为完整的 8 command / 6 state 集合是必需的

#### Scenario: 声明接口全集之外的接口

- **WHEN** 任一 joint 声明了接口全集之外的接口（如已删除的 `control_mode`、
  `stop_mode`、`feedback`，或任何未定义名称）
- **THEN** `on_init()` MUST 返回 `ERROR`，因为每个 joint MUST 恰好声明接口全集中的
  接口，不存在旧接口的兼容声明

#### Scenario: 基础 interface 缺失

- **WHEN** 一个 joint 缺少完整集合中的任一必需 command 或 state interface
- **THEN** `on_init()` MUST 返回 `ERROR`

## ADDED Requirements

### Requirement: 认领集合与模式参数映射

每次 active `write()` 中，插件 MUST 按 `perform_command_mode_switch()` 记录的每关节
模式派发控制。关节的所有命令存储在每周期 `write()` 结束时 MUST 重置为 `NaN`。

已绑定模式的关节必填接口全部为 `NaN` 表示控制器本周期未写命令——控制器已绑定却未写入
即视为非法命令，适用逐关节校验语义（跳过该关节并记录错误日志）。必填接口中任一（但非
全部）为 `NaN`、无穷或不满足整数/枚举语义时，同样适用逐关节校验语义。

各模式必填参数语义：连续量接口 MUST 为有限值；`torque_mode` MUST 为 `0`（普通 PVT）
或 `1`（SDK 上位机扭矩模式）的精确值；`stop_current` 的值即停止时施加的制动
电流。停止固定使用 SDK 的 `MotorStopMode::FullBrake`，带反馈参数的
SDK 调用 MUST 固定以 `feedback=1` 下发。同一模式下未列入认领集合的 interface 不参与
该周期的调用。

controller 在任一 active 控制周期需要控制某 joint 时，MUST 写入其模式全部必填
参数；controller 停用后不会遗留旧目标。

#### Scenario: PVT 模式直接转发

- **WHEN** 某关节已绑定 PVT 模式，且 `position`、`velocity`、`effort`、`kp`、`kd`、
  `torque_mode` 均为有限值
- **THEN** 插件 MUST 以同一组值调用该 joint 的 `PVTControl()`，并传递
  `torque_mode` 的布尔语义

#### Scenario: 本周期未写命令的关节

- **WHEN** 某关节已绑定模式，但其全部必填接口在本周期均为 `NaN`（控制器未写命令）
- **THEN** 该关节命令被跳过并记录日志；持续未写命令将每周期产生错误日志，提示
  控制器已绑定但未写入命令

#### Scenario: 扭矩模式值越界

- **WHEN** 已绑定 PVT 模式的关节的 `torque_mode` 为 `2` 或非整数值
- **THEN** 预检 MUST 判定为非法必填值并返回 `ERROR`

### Requirement: 逐关节命令校验与错误日志

插件 MUST 在逐关节处理中校验本周期每一个绑定模式的 joint：其绑定模式的必填 command interface MUST 均为有限值（`torque_mode`
MUST 为 `0` 或 `1` 的精确值，其余连续量接口 MUST 为有限值。任一必填接口为 `NaN`、无穷或非法时，该关节本周期命令被跳过，插件
MUST 记录一次包含关节名、模式与缺失/非法接口名的错误日志。其余关节的命令 MUST 正常派发；`write()` MUST 在
循环结束后恰好调用一次 `manager_.Commit()`，且仅在 SDK 控制调用或 `Commit()` 抛出异常时返回 `ERROR`。

`write()` MUST NOT 因单个关节的校验失败而阻断其他关节的派发与提交。每个有效命令关节 MUST 按其绑定模式调用
一次对应 SDK 控制接口；`write()` MUST 每周期恰好调用一次 `manager_.Commit()`；sync 模式下 `Commit()` 是命令到达总线的
唯一边界。

#### Scenario: 非法命令的关节被跳过并记录日志

- **WHEN** 已绑定模式的某 joint 的必填接口为 `NaN`、无穷或不满足整数语义（如 `torque_mode` 非 0/1）
- **THEN** `write()` MUST 记录一次包含该关节名、模式与缺失/非法接口名的错误日志，跳过该关节，其余关节照常派发；
  `write()` 返回 `OK`（仅当 SDK 调用或 `Commit()` 抛出异常时才返回 `ERROR`）

#### Scenario: 非法命令不阻断其他关节

- **WHEN** 多关节批次中至少一个 joint 的命令非法
- **THEN** 该关节被跳过并记录日志，其余关节的合法命令 MUST 正常派发并 `Commit()`

#### Scenario: 控制器停写即视为非法命令

- **WHEN** 已绑定模式的关节必填接口全为 `NaN`（控制器本周期未写命令）
- **THEN** 该关节命令被跳过并记录日志；持续未写命令将每周期产生错误日志，
  提示控制器已绑定但未写入命令

#### Scenario: PVT 调用失败触发全刹路径

- **WHEN** 一个参数完整的 PVTControl 调用抛出 `std::exception`
- **THEN** `write()` MUST 记录 joint、模式和异常并返回 `ERROR`；硬件层 MUST NOT 自动
  全刹、重试或提交清理批次

## MODIFIED Requirements

### Requirement: 统一同步提交与直接 SDK 错误传播

每个 active `write()` 循环结束后，插件 MUST 恰好调用一次 `DefaultJointManager::Commit()`，该实例中被管理器追踪的每条总线 MUST 因而由 SDK 在
同一提交边界发送本周期排队的控制；硬件包 MUST 不再提供关闭或分批提交的选项。此边界
仅适用于该 `DefaultJointManager` 管理的总线，MUST NOT 被解释为跨
`EcJointHardware` 实例或跨物理总线的同步保证。

这不是推测性的防御要求：当前 `joint_sdk` 中 `RJoint` 的 PVT、位置、速度、电流和
扭矩控制会在输入非有限时抛出 `std::invalid_argument`；双耦合 joint 的运行时求解或
未配对转发会抛出派生自 `std::runtime_error` 的 `JointException`；
`DefaultJointManager::Commit()` 调用的 `Bus::Commit()` 在总线操作对象不可用或保护容量
耗尽时也会抛出 `std::runtime_error`。因此，若任一 SDK 控制调用或 `Commit()` 抛出
`std::exception`，插件 MUST 记录一次包含 joint 名称（如适用）、模式与异常消息的错误并
返回 `ERROR`。它 MUST NOT 重试、二次调用 `Commit()`、构造诊断电机地址、自动全刹或把
异常转换为缓存状态。

#### Scenario: 同一硬件实例中已选择关节的控制只提交一次

- **WHEN** 本周期所有有命令的 joint 均具有完整有效命令
- **THEN** 插件 MUST 对每个 joint 恰好调用一次该模式的 SDK API，并在全部调用后
  恰好调用一次 `manager.Commit()`

#### Scenario: PVT 批次只提交一次

- **WHEN** 任一 selected joint 成功调用 PVTControl
- **THEN** `write()` MUST 在所有 selected joint 调用成功后恰好 Commit 一次

#### Scenario: 无参数化提交开关

- **WHEN** 一个或多个 joint 有完整有效控制命令
- **THEN** 插件 MUST 在该实例的控制批次后恰好 Commit 一次，该行为不受任何插件参数控制

#### Scenario: 两个硬件实例不构成全局同步域

- **WHEN** 两个 `EcJointHardware` 实例分别管理不同设备组或不同物理总线
- **THEN** 每个实例 MUST 仅提交自己的 `DefaultJointManager`；规范 MUST NOT 声称
  两个 `Commit()` 调用能够形成跨实例或跨总线的原子同步

#### Scenario: SDK 控制异常

- **WHEN** 某 joint 的 SDK 控制方法抛出异常
- **THEN** `write()` MUST 记录该 joint、模式与异常，返回 `ERROR`，且不得重试、
  自动停止或额外提交

### Requirement: 最小生命周期、反馈看门狗与直接状态发布

`on_configure()` MUST 清空先前的本地 joint 引用，重新加载该实例唯一的 JSON、创建
adapters，并按 JSON 创建 SDK joint 后关联到 ros2_control joint。它 MUST NOT 提取电机
诊断地址、重试初始化、跨实例管理 adapter 或构造反馈/错误的影子状态。

`on_activate()` MUST 在进入 active 前调用一次 `manager.Commit()` 使总线进入软同步
模式；此后本实例的 SDK 控制调用排队等待 `Commit()`，`Commit()` 是命令到达总线的
唯一边界，从首个写周期起成立。

`on_configure()` MUST 在创建 adapters 后通过
`DefaultJointManager::SetAdaptersStatusMaxLifeCycle(3)` 启用反馈看门狗：电机反馈
每到达一帧刷新为满值，每个 active `read()` 周期扣减 1；连续 3 个 `read()` 周期无
新反馈时缓存状态过期，后续 `GetStatus()` 返回空值。3 个周期的实际时长由
controller_manager 的 `update_rate` 决定。

`on_activate()` MUST 在进入 active 前取得每个已配置 joint 的初始反馈。一次有效的初始
反馈 MUST 是非空的 SDK `JointStatus`，且 `position`、`velocity`、`effort`、
`mos_temperature` 和 `motor_temperature` 均为有限值；`motor_error` MUST 作为 SDK
`MotorError` 的数值状态一并发布。插件 MUST 对本实例所有尚无有效反馈的 joint 调用
`GetStatus(true, 0)` 请求反馈；只要该轮发起了至少一个反馈请求，就 MUST 在该轮结束后
恰好调用一次 `manager.Commit()`，以提交该轮已排队的反馈请求。它 MUST 最多尝试 20 轮、
每轮间隔 50 ms：只有所有 joint 均取得有效初始反馈时才可置为 active；到期、
`GetStatus()` 或所需的 `Commit()` 抛出异常时，MUST 记录一次错误并返回 `ERROR`。激活
失败时 MUST NOT 置为 active、自动停止、重试配置、构造诊断电机地址、维护反馈缓存或执行
额外的清理提交。

`on_activate()` 写入初始状态时，若任一 joint 的初始反馈已带有非 `NoError` 的
`motor_error`，MUST 记录一次包含 joint 名与错误码的错误日志；激活流程不因电机错误码
而失败。

每个 active `read()` MUST 对每个 joint 调用一次 SDK `GetStatus(false, 1)`，并在有状态时直接
写入所有六个 state interface。它 MUST 不对状态进行有限性筛选、缓存恢复、连续缺失
计数、状态去重或电机地址诊断。任一 `GetStatus()` 返回空值（包括看门狗过期）或抛出
异常时，`read()` MUST 记录一次错误并返回 `ERROR`；它 MUST 不伪造状态或发出控制命令。

`read()` MUST 对电机错误码做边沿触发日志：错误码相对上一周期发生变化且新值非
`NoError` 时记录一次包含 joint 名与错误码的错误日志；错误码恢复为 `NoError` 时记录
一次警告日志；错误码持续不变时 MUST NOT 每周期重复记录。状态接口的发布 MUST 不受
该日志逻辑影响。

`on_deactivate()` MUST 仅将插件切换为 inactive，使后续 `read()` 和 `write()` 成为
无副作用的 `OK` 返回。它 MUST NOT 调用 SDK `Stop()` 或 `Commit()`；电机失能由
底层心跳保护负责。实时主动停止只能由绑定停止模式的控制器写入 `stop_current` 命令请求。

#### Scenario: 所有关节获得完整有效反馈后才激活

- **WHEN** 每个已配置 joint 都返回具有五个有限浮点字段的 `JointStatus`
- **THEN** `on_activate()` MUST 将六项状态写入对应 state interface 并进入 active；若此前
  或本轮发起了反馈请求，每一请求轮 MUST 恰好提交一次

#### Scenario: 任一关节缺失或无效反馈时拒绝激活

- **WHEN** 在 20 轮反馈请求后，任一已配置 joint 仍返回空状态或任一浮点状态为非有限值
- **THEN** `on_activate()` MUST 记录该 joint 的一次错误并返回 `ERROR`，且 MUST NOT
  进入 active、自动停止或额外提交

#### Scenario: 激活时已存在电机错误

- **WHEN** 激活准入的初始反馈中某 joint 的 `motor_error` 非 `NoError`
- **THEN** `on_activate()` MUST 记录一次包含该 joint 名与错误码的错误日志，并继续
  完成激活

#### Scenario: 完整状态直通

- **WHEN** SDK 为某 joint 返回 `JointStatus`
- **THEN** `read()` MUST 将 position、velocity、effort、MOS 温度、电机温度和错误码
  全部写入对应 state interface

#### Scenario: 反馈看门狗过期

- **WHEN** 某 joint 连续 3 个 `read()` 周期没有新的电机反馈帧
- **THEN** 该 joint 的缓存状态过期，`GetStatus()` 返回空值，`read()` MUST 记录一次
  错误并返回 `ERROR`

#### Scenario: 电机错误边沿日志

- **WHEN** `read()` 读到某 joint 的 `motor_error` 由 `NoError` 变为非 `NoError`，或由
  一个错误码变为另一个错误码
- **THEN** `read()` MUST 记录一次包含 joint 名与错误码的错误日志；该错误码持续期间
  MUST NOT 重复记录

#### Scenario: 电机错误恢复日志

- **WHEN** `read()` 读到某 joint 的 `motor_error` 由非 `NoError` 恢复为 `NoError`
- **THEN** `read()` MUST 记录一次警告日志

#### Scenario: 停用不下发制动

- **WHEN** active 插件接收 `on_deactivate()`
- **THEN** 它 MUST 不调用 `Stop()` 或 `Commit()`；之后的 `read()` 与 `write()` MUST
  无副作用地返回 `OK`

#### Scenario: 完整首帧反馈后激活

- **WHEN** 全部配置 joint 在激活窗口内各自返回完整有效的 JointStatus
- **THEN** 插件 MUST 仅在此条件满足后进入 active

#### Scenario: 读到缺失反馈时保留状态

- **WHEN** active `read()` 中任一 joint 的 GetStatus 返回空值
- **THEN** `read()` MUST 记录一次错误并返回 `ERROR`，且 MUST NOT 以缓存的旧 state 替代该
  joint 的状态

#### Scenario: 停用全刹

- **WHEN** active 插件接收 `on_deactivate()`
- **THEN** 插件 MUST NOT 对任何 joint 调用 FullBrake 或其他 Stop 模式

#### Scenario: 激活后进入软同步模式

- **WHEN** `on_activate()` 成功完成
- **THEN** 总线 MUST 已进入软同步模式；此后任何 SDK 控制调用只进入待发送队列，
  直到 `Commit()` 才被发送到总线

### Requirement: 工作区集成、文档与测试

`robot_descriptions/robot_description/xacro/ros2_control/ec_joint_hardware.xacro` 中的 `ec_joint_hardware_joint_interfaces` 宏 MUST 生成本规范定义的 8 个 command interface
与 6 个 state interface。所有使用该宏的机械臂与灵巧手描述 MUST 自动继承该集合。
机器人描述 MUST 保持按独立设备组声明多个 `<ros2_control>` / `EcJointHardware` 实例，
每个实例只传入自己的 `config_file`。机器人描述与插件 MUST NOT 声明 `soft_sync` 或
`ec_joint_hardware_soft_sync` 参数。

所有使用 `ec_joint_hardware` 的 controller MUST 按其控制模式认领对应的固定接口集合，
并在需要控制的每个周期写入该模式的全部必填接口。controller MAY 独立启动或停止；
模式随控制器激活绑定，切换控制器即切换模式，由硬件在命令模式切换回调中校验。

README MUST 以本规范的接口表、认领集合-模式表、同步提交保证和不完整命令 `ERROR`
语义面向 controller 开发者编写。

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

## REMOVED Requirements

### Requirement: 显式控制模式与参数映射

**Reason**：运行中通过 `control_mode` 逐周期切换模式没有正当场景，且把模式正确性
压到每周期运行时校验；模式改为控制器激活时由认领集合唯一绑定，切换走
`prepare_command_mode_switch()` 标准机制。新电机固件只保留全刹，`stop_mode` 的制动
选择随之删除；电机反馈固定请求完整状态，`feedback` 的类型选择不再暴露。

**Migration**：controller 删除 `control_mode`、`stop_mode`、`feedback` 的接口认领与
写入，改为按「认领集合与模式参数映射」认领其模式的固定接口集合；需要停止的控制器
认领 `{stop_current}` 并写入制动电流值，停止方式固定为全刹。

### Requirement: 不完整 controller 命令的批次失败语义

**Reason**：批次级预检会让单个关节的非法命令阻断其余关节，且与逐关节记录错误并继续
提交合法命令的新语义冲突。

**Migration**：改用「逐关节命令校验与错误日志」要求；controller 仍须每周期写入其模式
的全部必填接口，但单个关节的无效值只跳过该关节，不再取消同一批次中的其他合法命令。
