## MODIFIED Requirements

### Requirement: 使用 nlohmann/json 与 joint_sdk 的原生反序列化路径

配置加载 MUST 使用 nlohmann/json 的受检访问与类型转换，保留完整 adapter 和 joint JSON。
解析编号占位符后，插件 MUST 按声明顺序调用 `CreateAdapter(adapter_json)`，MUST 保留
`extend` 的公共总线参数及 `buses` 条目；MUST NOT 将适配器缩减为只有 type/name 的 DTO。
SDK MUST 负责适配器字段、扩展配置、创建与复用规则。

每个关节 JSON MUST 原样传给 `AddJoint(json)`，保留数字/字符串 `adapterId`、`slaveId`、
逐电机 `canfd` 及其他 SDK 字段。硬件 MUST NOT 重复实现拓扑、限位或运动学校验。
配置解析或 SDK 调用异常 MUST 在生命周期边界记录并返回 ERROR，不重试 AddJoint。

#### Scenario: 完整传递适配器扩展

- **WHEN** adapter JSON 包含 `extend.bitrate/sample_point/dbitrate/dsample_point` 及 `extend.buses`
- **THEN** 硬件 MUST 保留这些字段和值并通过 JSON 重载传给 SDK

#### Scenario: 扩展配置缺省或非法

- **WHEN** `extend` 缺省、为 null 或为错误类型
- **THEN** 硬件 MUST 沿用 SDK 的接受或拒绝规则；SDK 拒绝时配置 MUST 返回 ERROR

#### Scenario: SDK 解释关节配置

- **WHEN** 单电机或双耦合关节使用索引或名称引用适配器，并携带逐电机配置
- **THEN** 硬件 MUST 将完整关节 JSON 传给 SDK，不改写引用、缺省值或电机参数

#### Scenario: SDK 处理 joint 的 JSON 定义

- **WHEN** 配置包含受 DefaultJointManager 支持的 joint JSON
- **THEN** 硬件 MUST 将完整 JSON 交给 AddJoint，由 SDK 解释关节类型及配置，硬件 MUST NOT 读取电机参数或配对键建立抱闸分组

#### Scenario: SDK 管理 adapter 创建与保护

- **WHEN** JSON 声明一个或多个 adapter
- **THEN** 硬件 MUST 按声明顺序调用 CreateAdapter(adapter_json)，MUST NOT 因本包或其他实例的适配器状态拒绝、合并或改写该调用

#### Scenario: SDK 拒绝无效的电机拓扑

- **WHEN** joint JSON 的电机地址、配对或限值不被 SDK 接受
- **THEN** on_configure MUST 记录 SDK 错误并返回 ERROR，MUST NOT 先执行本包重复的拓扑或限值校验

#### Scenario: 手工 JSON 拓扑校验拒绝冲突

- **WHEN** 两个 joint 的电机地址或双电机配对无效
- **THEN** 硬件 MUST 将原始 JSON 交给 SDK 决定接受或拒绝，MUST NOT 维护手工拓扑校验器

#### Scenario: 瞬态电机参数超时重试

- **WHEN** AddJoint 抛出 Get motor parameter timeout 异常
- **THEN** on_configure MUST 记录异常并返回 ERROR，MUST NOT 重试 AddJoint

### Requirement: Numbered adapter hardware parameters

Hardware parameters `adapter_type_N/interface_name_N` MUST remain supported as complete,
non-empty pairs with valid decimal IDs. An empty selection map MUST be accepted during
initialization. Exact JSON markers `plugin-replace-me-N/enx-replace-me-N` MUST resolve
before SDK resource creation; an unresolved marker MUST cause configuration ERROR.
Literal adapter fields MUST remain unchanged by unused numbered selections.

#### Scenario: 固定适配器不要求编号参数

- **WHEN** JSON 包含固定 adapter 类型和名称，且没有编号占位符
- **THEN** 初始化和配置 MUST 允许完全不传编号 hardware 参数

#### Scenario: 完整模板替换

- **WHEN** JSON 递归嵌套包含完整编号占位符且选择表包含相应完整参数对
- **THEN** 所有完整字符串占位符 MUST 被替换，数值字段 MUST 保持类型和值

#### Scenario: 编号参数不完整或模板无法解析

- **WHEN** 提供的参数对不完整、ID 非法、值为空，或 JSON 引用了不存在的选择项
- **THEN** 硬件 MUST 在创建 SDK 资源前报告配置错误；MUST NOT 猜测适配器配置

### Requirement: CANFD hardware parameter

The legacy `canfd_enable` hardware parameter MUST NOT select or override frame mode.
The hardware MUST NOT read, parse, or emit warnings for this parameter. Motor JSON
MUST be passed to JointSDK for frame-mode decisions, validation, and defaults.
The hardware MUST NOT retain a global CAN FD selection member.

#### Scenario: 忽略旧参数

- **WHEN** hardware 参数包含任意 `canfd_enable` 值
- **THEN** 硬件 MUST NOT 读取、解析或针对该参数告警，MUST NOT 因该参数的值拒绝初始化

### Requirement: CANFD configuration through JointSDK

Configuration MUST NOT call `EnableJointMotorCanfd()` or `DisableJointMotorCanfd()`.
Each motor MUST follow its own JSON `canfd`; omission MUST use the SDK default false.
The same rule MUST apply on reconfiguration and to both motors of coupled joints.
Invalid JSON field types MUST remain SDK configuration errors.

#### Scenario: 缺省使用普通 CAN

- **WHEN** 某电机没有 JSON `canfd` 字段，即使旧 hardware 参数为 true
- **THEN** 该电机 MUST 使用 SDK 默认的普通 CAN 配置

#### Scenario: 同一实例混合帧类型

- **WHEN** 同一实例或同一双耦合组的不同电机分别配置 `canfd=true` 和 `canfd=false`
- **THEN** 实际发送帧 MUST 分别遵循各自配置，MUST NOT 被全局开关统一覆盖

#### Scenario: 重新配置刷新帧类型

- **WHEN** JSON 中的逐电机设置改变并重新配置硬件
- **THEN** 新建关节 MUST 使用新的 JSON 设置，MUST NOT 继承旧全局选择

### Requirement: 控制器切换后的命令派发

硬件 MUST 删除实例首帧命令门及 awaiting_first_command_。write MUST 逐关节沿用原有
模式 if/else 校验；合法命令 MUST 独立派发，无效或缺失命令 MUST 记录错误并跳过该关节。
实际模式切换 MUST 保留命令槽，MUST NOT 等待全部已认领关节都写入首帧。

#### Scenario: 只有部分关节写入有效命令

- **WHEN** 一个已认领关节命令完整有效，另一个关节未写命令
- **THEN** 硬件 MUST 派发有效关节命令，MUST NOT 因其他关节未就绪而整批跳过

#### Scenario: 无效命令不触发首次松闸

- **WHEN** 尚未松闸的关节命令不完整或非法
- **THEN** 该关节 MUST 不松闸、不派发命令，其他合法关节独立派发

## ADDED Requirements

### Requirement: 关节绑定的抱闸能力缓存

每个 `JointRuntime` MUST 仅用 `brake_state` 枚举与关节名称和 SDK 句柄一起存储抱闸能力及状态，
MUST NOT 再使用 has_brake/brake_released 两个布尔字段。每次配置 MUST 在全部关节创建、绑定和配对完成后，对每个
关节主动调用一次 `HasBrake()` 并缓存结果。后续硬件生命周期、模式切换和实时循环
MUST 直接读取缓存，MUST NOT 主动重复查询。SDK 抱闸方法内部检查不受此约束。

能力缓存 MUST 在重建开始时清除，成功创建新实例后重新生成；配置失败 MUST 清除
本次句柄和状态并保持未配置。枚举 MUST 表示 SDK 操作确认状态，不代替物理状态检测。

#### Scenario: 配对完成后查询一次

- **WHEN** 全部 AddJoint 调用和运行时绑定成功，包括双耦合的主从配对
- **THEN** 硬件 MUST 对每个关节主动查询一次能力并与该关节保存，MUST NOT 在配对前查询从轴

#### Scenario: 生命周期复用能力

- **WHEN** 同一配置实例多次激活、停用、切换控制器或执行 read/write
- **THEN** 硬件 MUST 使用缓存的能力，MUST NOT 增加主动 HasBrake 查询次数

#### Scenario: 重配与配置失败

- **WHEN** 重新配置或配置过程中发生异常
- **THEN** 旧能力 MUST 失效；仅新配置全部成功时才能供后续生命周期使用，失败 MUST 留在未配置状态

#### Scenario: 配置时初始抱闸

- **WHEN** 全部关节创建、配对完成
- **THEN** on_configure MUST 在同一个循环中逐关节查询并缓存 HasBrake，对支持关节立即调用 Brake(true)，跳过不支持关节
- **AND** 任一初始抱闸失败 MUST 立即终止配置，清除本次绑定和缓存并返回 ERROR，MUST NOT 允许激活

### Requirement: 以 SDK 关节为抱闸边界

硬件 MUST 仅依据每个 SDK 关节缓存的 HasBrake 结果执行抱闸操作，MUST NOT 感知电机数量、
解析电机参数、建立配对键或选取代表关节。双电机协调 MUST 交由 SDK 处理。
模式切换 MUST 沿用逐关节接口完整性校验，MUST NOT 增加双轴共同认领或同模式限制。

#### Scenario: 每个支持关节调用 SDK 抱闸接口

- **WHEN** 多个 SDK 关节均缓存为支持抱闸
- **THEN** 生命周期 MUST 分别调用各关节的 Brake，由 SDK 处理底层电机协调

#### Scenario: 各关节独立认领模式

- **WHEN** 仅部分关节被认领，或不同关节认领不同模式，但每个关节的接口集合均合法
- **THEN** 硬件 MUST 接受模式切换，不执行额外的双轴校验

### Requirement: 激活不松闸

激活 MUST 沿用所有关节完整有限反馈准入，MUST NOT 释放抱闸或发送位置保持命令。
硬件 MUST NOT 为抱闸维护保持位置、安装保持反馈观察回调或等待额外的新反馈。

#### Scenario: 激活完成后等待控制器

- **WHEN** 所有关节反馈满足激活条件
- **THEN** 硬件 MUST 进入 active，MUST NOT 调用 Brake(false) 或额外 PosControl

### Requirement: 切换控制器不操作抱闸

prepare MUST 只校验接口。perform MUST 在合法性校验后更新模式、保留命令缓存，
MUST NOT 调用 Brake 或改变 brake_state，包括同模式控制器交接及控制器退出。

#### Scenario: 已松闸关节切换控制器

- **WHEN** 已松闸关节完成合法的控制器切换
- **THEN** 硬件 MUST 保留松闸记录，新有效命令 MUST 直接派发，不重复保持或松闸

#### Scenario: 控制器退出

- **WHEN** 切换后关节不再被控制器认领
- **THEN** 抱闸关节 MUST 在后续 write 的 None 分支抱紧，MUST NOT 派发控制命令或位置保持

### Requirement: 有效首帧松闸后执行控制命令

write 在关节命令完整合法时，MUST 对 Engaged 关节先读取
当前 SDK 位置反馈并调用 PosControl 保持，再调用该关节 Brake(false)，成功后
进入 Released，立即执行该关节同一帧控制命令，然后处理下一关节。后续周期 MUST NOT 重复保持或松闸。
状态 MUST 遵守下述枚举转换要求，MUST NOT 当作物理状态查询结果。
无抱闸关节在 None 模式 MUST 使用 CurControl(0.0F, 1)，其余模式保留现有派发行为；
双电机协调 MUST 委托 SDK。

#### Scenario: 首帧顺序

- **WHEN** 尚未松闸的关节收到完整有效命令
- **THEN** 对应支持关节 MUST 先用当前反馈位置调用 PosControl，再成功松闸，最后执行
  控制器命令；保持及松闸 MUST 直接发送，成功后恢复控制器命令的同步提交

#### Scenario: 无法取得保持位置或保持失败

- **WHEN** SDK 位置反馈缺失、非有限或 PosControl 抛异常
- **THEN** write MUST 返回 ERROR，MUST NOT 对失败关节执行松闸或派发控制器命令；此前关节已经执行的操作不撤销

#### Scenario: 后续正常命令

- **WHEN** 同一控制周期序列已经成功松闸
- **THEN** 后续有效命令 MUST 直接派发，控制器切换后也不重复松闸，直到生命周期抱紧或重新配置清除记录

#### Scenario: 松闸失败

- **WHEN** Brake(false) 返回 false 或抛异常
- **THEN** write MUST 返回 ERROR，MUST NOT 派发该关节控制命令或记录松闸成功

#### Scenario: 控制调用或提交失败

- **WHEN** 松闸已成功，但控制调用或 Commit 抛异常
- **THEN** write MUST 返回 ERROR，MUST NOT 伪造已经抱紧；on_error MUST 尝试实际抱紧

### Requirement: 生命周期退出与抱闸调用边界

停用、错误和关闭 MUST 停止正常控制，仅对 Released 关节尝试 Brake(true)，失败 MUST 继续其他关节
并最终返回 ERROR。未配置、空句柄及 Unsupported/Engaged 关节 MUST 不发送抱闸命令。
配置、None 分支及退出回调 MUST 通过 EngageBrake 执行抱闸，由函数内部管理同步模式：
实际抱闸前 MUST 切换直接发送，成功后 MUST 恢复同步；无需动作的返回路径 MUST 不改变模式。
read/write MUST NOT 主动重新查询 HasBrake 或 GetBrakeStatus。write MUST 允许在有效首帧
前同步调用 Brake；稳态周期 MUST NOT 重复调用。SDK 等待确认的耗时包含在该次 write 中。
机械 Brake 与电气 Stop(FullBrake) MUST 保持独立；正常命令批次 MUST 沿用一次 Commit。

#### Scenario: 生命周期退出尽力抱紧

- **WHEN** 已配置硬件进入停用、错误或关闭回调
- **THEN** 硬件 MUST 尝试所有 Released 关节，记录失败并保持 inactive

#### Scenario: 稳态读写

- **WHEN** 关节已经松闸且控制器继续发送正常命令
- **THEN** 硬件 MUST 直接派发，MUST NOT 重复机械抱闸操作或轮询抱闸状态及能力

### Requirement: 新接口兼容与软件验收范围

插件 MUST 保持现有身份、八个命令接口 `position/velocity/effort/current/kp/kd/torque_mode/stop_current`
及六个状态接口 `position/velocity/effort/mos_temperature/motor_temperature/motor_error`。
本变更 MUST NOT 增加抱闸 ROS 服务或实时接口，也 MUST NOT 恢复历史十接口方案。
构建 MUST 直接使用已安装的 JointSDK 1.4.5，MUST NOT 增加临时 SDK 安装、路径锁定或
额外版本保护。自动验收 MUST 使用 Mock/Fake，真实整机 JSON MUST 只用于无硬件解析验证。

#### Scenario: 现有 controller 接口保持兼容

- **WHEN** controller 使用现有八命令、六状态接口及完整认领模式
- **THEN** 插件 MUST 保持这些公开接口，各关节沿用原有认领规则，抱闸关节按自身有效首帧完成交接

#### Scenario: 实际帧与能力缓存验收

- **WHEN** 执行本变更的软件测试
- **THEN** 测试 MUST 检查 Fake 发送帧标志及 Mock 主动能力查询计数，不能只检查配置变量

#### Scenario: 实机能力前置条件

- **WHEN** 驱动报告不支持抱闸或固件拒绝抱闸操作
- **THEN** 硬件 MUST 不强行覆盖能力或伪造操作成功，软件测试 MUST NOT 被表述为物理抱闸安全证明

## REMOVED Requirements

### Requirement: 控制器切换后的首帧命令门

**Reason**: 控制器切换不再操作抱闸，无需实例首帧命令门。
**Migration**: 各关节使用原有模式校验独立派发，切换时仍保留命令缓存。

### Requirement: 单一抱闸状态机

BrakeState MUST 包含 Unsupported=0、Engaged=1、Released=2、Engaging=3、Releasing=4。
运行期 0 MUST 不转换，正常动作 MUST 按 1→4→2→3→1 转换；保持位置缺失、非有限或 Brake(false) 返回 false 时 MUST 允许 4→1 回退，Brake(true) 返回 false 时 MUST 允许 3→2 回退。
新绑定初始为 0；HasBrake 为 true 时 MUST 以 2 为初始请求起点，并通过 EngageBrake
完成初始抱闸。配置成功时所有支持关节 MUST 为 1，失败 MUST 清除绑定及状态。
配置初始化和销毁不属于既有绑定的运行期转换；状态 MUST NOT 代替物理状态检测。

#### Scenario: 同步动作尚未返回

- **WHEN** EngageBrake 从 Released 发起抱闸，或 ReleaseBrake 从 Engaged 发起松闸
- **THEN** MUST 在 SDK 动作前分别设置 Engaging 或 Releasing，成功返回才进入 Engaged 或 Released
- **AND** ReleaseBrake MUST 在确认状态为 Engaged 后立即进入 Releasing，再读取位置、发送保持命令及松闸

#### Scenario: 重复已完成的动作

- **WHEN** Released 收到松闸请求，或 Engaged 收到抱闸请求
- **THEN** MUST 返回当前 BrakeState 且不再次调用 SDK；已松闸和已抱闸 MUST 均静默跳过

#### Scenario: 未完成的动作和失败

- **WHEN** 状态为 Engaging 或 Releasing 时收到任一抱闸/松闸请求
- **THEN** ReleaseBrake 和 EngageBrake MUST 静默返回当前 BrakeState；MUST NOT 再次调用 SDK 或派发该关节控制命令
- **AND** Brake(false) 返回 false 后 MUST 回退 Engaged 并允许重试；Brake(true) 返回 false 后 MUST 回退 Released 并允许重试；SDK 抛异常后 MUST 保留过渡状态，重新配置才能重新初始化

#### Scenario: 无抱闸能力

- **WHEN** Unsupported 收到任一抱闸/松闸请求
- **THEN** MUST 保持状态 0 并返回 BrakeState::Unsupported，MUST NOT 调用 Brake

#### Scenario: 高频 write 遇到过渡状态

- **WHEN** write 遍历到 Engaging 或 Releasing 关节
- **THEN** MUST 静默跳过该关节的命令校验、保持、抱闸和控制调用，MUST NOT 仅因过渡状态返回 ERROR
- **AND** 其他关节 MUST 继续正常派发，write 周期末仍按原有规则清除命令缓存
- **AND** 发起松闸的当次 write 若位置无效、SDK 返回 false 或抛异常，MUST 返回 ERROR；位置无效或 Brake(false) 返回 false MUST 回退 Engaged，SDK 异常 MUST 保留 Releasing

### Requirement: 抱闸辅助函数返回枚举状态

EngageBrake 和 ReleaseBrake MUST 返回 BrakeState 而非 bool，所有正常返回路径 MUST
返回关节当前枚举状态。保持位置缺失、非有限或 Brake(false) 返回 false 时 MUST 回退并返回 Engaged；Brake(true) 返回 false MUST 回退并返回 Released，SDK 异常
MUST 沿用外层生命周期/write 异常处理。

#### Scenario: 调用处判断完成状态

- **WHEN** write 对可执行关节调用 ReleaseBrake
- **THEN** MUST 仅在返回 Unsupported 或 Released 时派发控制命令，其他返回状态 MUST 导致当次 write 返回 ERROR
- **AND** write 进入时已为 Engaging/Releasing 的关节仍 MUST 静默跳过

#### Scenario: 配置与退出判断抱闸状态

- **WHEN** 配置或生命周期退出调用 EngageBrake
- **THEN** 支持关节的初始配置 MUST 要求返回 Engaged；退出回调 MUST 仅将 Unsupported/Engaged 视为完成

#### Scenario: 保持位置恢复后重试

- **WHEN** ReleaseBrake 取得空反馈或非有限位置
- **THEN** MUST 回退并返回 Engaged，MUST NOT 发出位置保持或松闸命令
- **AND** 反馈恢复后再次调用 MUST 可以正常保持并松闸，无需重新配置

#### Scenario: 松闸返回 false 后重试

- **WHEN** SDK Brake(false) 返回 false
- **THEN** ReleaseBrake MUST 回退并返回 Engaged，write MUST 返回 ERROR 且不派发该关节命令
- **AND** 后续调用 MUST 能够再次发起松闸，成功后进入 Released

#### Scenario: 抱闸返回 false 后重试

- **WHEN** SDK Brake(true) 返回 false
- **THEN** EngageBrake MUST 回退并返回 Released，退出回调 MUST 返回 ERROR 并继续处理其他关节
- **AND** 后续调用 MUST 能够再次发起抱闸，成功后进入 Engaged

### Requirement: None 模式抱紧支持关节

write 的 None 分支 MUST 对 Released 关节调用 EngageBrake，Engaged MUST 静默跳过，
Engaging/Releasing MUST 沿用周期开始的静默跳过。Unsupported MUST 继续 CurControl(0.0F, 1)。
该分支 MUST 仅调用 EngageBrake 并检查返回状态，MUST NOT 在分支中切换同步模式；
EngageBrake MUST 在实际抱闸前切换直接发送，成功后恢复同步，与 ReleaseBrake 一致。
支持关节 MUST NOT 在 None 分支发送零电流、位置保持或其他控制命令。

#### Scenario: 控制器释放接口后抱闸

- **WHEN** 已松闸关节的模式变为 None
- **THEN** 下一次 write MUST 调用 Brake(true)，成功后进入 Engaged，后续周期 MUST NOT 重复抱闸
- **AND** 即使上次控制周期启用了同步发送，抱闸请求 MUST 发出并获得 SDK 确认

#### Scenario: None 抱闸失败

- **WHEN** None 分支的 Brake(true) 返回 false
- **THEN** 状态 MUST 回退 Released，当次 write MUST 返回 ERROR 且不向该关节发送控制命令
- **AND** 后续 write MUST 能够再次尝试抱闸

#### Scenario: 松闸前反馈缺失重试

- **WHEN** ReleaseBrake 读取保持位置时 GetStatus(true, 0) 返回空值
- **THEN** MUST 最多查询 5 次，首次立即查询，仅在后续重试前等待 1 ms
- **AND** 有反馈后 MUST 停止重试；耗尽次数仍无反馈时 MUST 回退并返回 Engaged
