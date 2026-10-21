## ADDED Requirements

### Requirement: 类型安全的模式解析与单关节运行时职责

插件内部 MUST 以 `ModeDefinition` 枚举表示未绑定状态和六种控制模式，并以
`std::map<ModeDefinition, std::vector<std::string>>` 作为模式到命令接口集合的唯一映射。
空接口集合 MUST 解析为 `ModeDefinition::None`，无法精确匹配合法集合的输入 MUST 解析为
`std::nullopt`。`JointRuntime` MUST 封装单关节的模式解析、命令复位和状态存储；硬件插件
MUST 继续以 `std::vector<JointRuntime>` 保存 HardwareInfo/URDF 关节顺序。

增量模式解析 MUST 从当前模式的接口集合开始，只处理属于目标 joint 的全局接口键，先应用
停止集合、再应用启动集合并去重，然后复用完整集合解析。`prepare_command_mode_switch()`
MUST NOT 修改任何关节模式；`perform_command_mode_switch()` MUST 先成功解析全部关节，再
统一写入模式，任一关节非法时 MUST NOT 部分更新。实现 MUST NOT 保留功能重复的
`ComputeCandidateInterfaces` 或 `MatchMode` 路径。

#### Scenario: 空认领与非法认领可区分

- **WHEN** 完整接口集合为空，或包含无法组成任一合法模式的接口组合
- **THEN** 空集合 MUST 返回 `ModeDefinition::None`，非法组合 MUST 返回 `std::nullopt`

#### Scenario: 增量认领只影响目标关节

- **WHEN** 一个 joint 的增量解析输入同时含有自身和其他 joint 的全局接口键
- **THEN** `JointRuntime` MUST 只应用自身键，并从当前模式按先停止后启动的顺序解析最终模式

#### Scenario: 模式切换执行保持原子性

- **WHEN** `perform_command_mode_switch()` 的多个 joint 中任一最终认领集合非法
- **THEN** 调用 MUST 返回 `ERROR`，所有 joint 的原模式 MUST 保持不变

#### Scenario: 重复接口不是合法模式

- **WHEN** 候选接口与某合法模式名称相同但含有重复项，导致多重性或长度不同
- **THEN** 精确接口比较 MUST 拒绝该候选集合

### Requirement: 统一辅助函数与头文件 Doxygen

无状态的接口键拆分、接口集合比较、模式名称和电机错误名称辅助函数 MUST 由 `utils`
提供，硬件实现和 `JointRuntime` MUST 复用这些函数。包内所有头文件中的类型、函数、参数、
返回值和数据成员声明 MUST 具有适用的中文 Doxygen 注释，并 MUST 能通过仓库严格 Doxygen
配置而不产生未文档化声明警告。

#### Scenario: 严格文档检查

- **WHEN** 对包内头文件运行仓库配置的严格 Doxygen 检查
- **THEN** 所有声明 MUST 有完整注释，检查 MUST 不因缺失文档失败

#### Scenario: 无状态辅助逻辑只有一个实现

- **WHEN** 硬件插件或 `JointRuntime` 需要拆分接口键、比较集合或格式化模式/错误名称
- **THEN** 调用方 MUST 使用 `utils` 中的共享函数，而不是保留文件内重复实现

## MODIFIED Requirements

### Requirement: 最小生命周期、反馈看门狗与直接状态发布

`on_configure()` MUST 清空先前的本地 joint 引用，重新加载该实例唯一的 JSON、创建
adapters，并按 JSON 创建 SDK joint 后关联到 ros2_control joint。它 MUST NOT 提取电机
诊断地址、重试初始化、跨实例管理 adapter 或构造反馈/错误的影子状态。

`on_activate()` MUST 在进入 active 前调用一次 `manager.Commit()` 使总线进入软同步
模式；此后本实例的 SDK 控制调用排队等待 `Commit()`，`Commit()` 是命令到达总线的
唯一边界，从首个写周期起成立。

插件 MUST 支持可选 hardware parameter `status_max_life_cycle`。参数缺失时 MUST 使用
默认值 `3`；参数存在时 MUST 是 `int` 范围内的正整数字符串，空值、零、负数、小数、
非数字、尾随字符或溢出 MUST 使 `on_init()` 返回 `ERROR`。`on_configure()` MUST 在创建
adapters 后通过 `DefaultJointManager::SetAdaptersStatusMaxLifeCycle()` 应用解析后的值，
以启用反馈看门狗：电机反馈每到达一帧刷新为满值，每个 active `read()` 周期扣减 1；
连续配置数量个 `read()` 周期无新反馈时缓存状态过期，后续 `GetStatus()` 返回空值。
实际时长由配置值和 controller_manager 的 `update_rate` 共同决定。

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

#### Scenario: 默认反馈看门狗过期

- **WHEN** 未配置 `status_max_life_cycle`，且某 joint 连续 3 个 `read()` 周期没有新的电机反馈帧
- **THEN** 该 joint 的缓存状态 MUST 过期，`GetStatus()` 返回空值，`read()` MUST 记录一次
  错误并返回 `ERROR`

#### Scenario: 自定义反馈看门狗生命周期

- **WHEN** `status_max_life_cycle` 配置为正整数 `N`
- **THEN** `on_configure()` MUST 将 `N` 传给 SDK，反馈缓存 MUST 以 `N` 个控制周期为生命周期

#### Scenario: 非法反馈看门狗配置

- **WHEN** `status_max_life_cycle` 不是 `int` 范围内的正整数字符串
- **THEN** `on_init()` MUST 返回 `ERROR`，硬件实例 MUST NOT 进入配置阶段

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
