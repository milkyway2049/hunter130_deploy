## Context

本设计基于硬件 `develop` 提交 `2de4928`、JointSDK 源码提交 `d67be77` 及已安装的
JointSDK 1.4.5。当前 `AdapterConfig` 丢弃 `extend`，`on_configure()` 调用全局
CAN FD 开关，`on_deactivate()` 只清除 active 标志。`write()` 对未认领关节发送
低阻尼 PVT，首帧命令门则整批跳过。新增抱闸行为必须同时处理这些入口。

SDK 的 `MotorParams::canfd` 默认是 `false`；显式调用全局 CAN FD 开关会覆盖逐电机
配置。SDK 提供 `HasBrake()`、`Brake(bool)` 和 `GetBrakeStatus()`。
`Brake(true)` 表示抱紧，`Brake(false)` 表示释放，返回值表示操作结果。
`GetBrakeStatus()` 可能阻塞，并会尝试补齐双电机不一致的抱闸状态，不用于实时轮询。

当前主规格仍存在 10 个命令接口、50 ms 激活等待等历史条款，与代码及其他 change
不完全一致。本变更以代码和本次已确认需求为依据描述差量，保留历史文件。归档时需要
显式协调相关 change，不能将旧规格中的历史接口或全局 CAN FD 行为重新引入实现。

## Goals / Non-Goals

**Goals:**

- 完整传递新版适配器、总线和逐电机配置，沿用 SDK 的解析及缺省值。
- 关节与抱闸能力一起存储，每次成功配置中每关节主动查询一次。
- 激活不松闸；切换控制器时抱紧；有效首帧到达时松闸并执行命令；停用时尝试抱紧。
- 通过 Mock/Fake 验证调用顺序、能力缓存、帧标志、切换和失败路径。

**Non-Goals:**

- 修改 SDK/驱动、补充电机型号表、更新固件、修改其他子仓库或部署整机 JSON。
- 新增手动抱闸服务、实时抱闸接口、实时抱闸状态轮询或保持专用速度/电流参数。
- 重新实现 SDK 的电机地址、配对、运动学、限位、适配器复用或 CAN 参数校验。
- 执行真实电机动作、提交或推送代码。

## Decisions

### 1. 使用已安装 SDK，完整转交配置

沿用现有 `find_package(joint_sdk REQUIRED CONFIG)` 和 `joint_sdk::joint_sdk`。
不临时构建/安装 SDK，不强制指定 `joint_sdk_DIR`，不增加额外版本保护。
系统安装已确认具备适配器 JSON 重载及机械抱闸接口。

配置加载后保存完整 adapter JSON，按声明顺序调用 `CreateAdapter(adapter_json)`。
先执行已有的递归编号占位符替换，再交给 SDK；`extend` 的缺省、null、对象类型以及
公共参数和 `buses` 的具体规则由 SDK/驱动解释。硬件只保留其配置文件、根数组和
关节名称对应关系的校验边界，不建立第二套适配器注册或拓扑校验。

`ParseAdapterSelections()` 接受空选择表；已提供的编号参数仍必须 ID 合法、成对且非空。
引用缺失选择项的完整占位符在创建 SDK 资源前报错。固定 JSON 字面值不被编号参数覆盖。
逐电机关节配置继续原样传入 `AddJoint(json)`，包括整数/字符串 `adapterId`、`slaveId`、
`motorParams` 或 `motorParams1/2`，不注入 CAN FD 默认字段。

删除硬件 `canfd_enable_`、布尔解析和 manager 的 enable/disable 调用。硬件不再读取、
检查或告警 `canfd_enable`；其值不能影响 JSON 或使初始化失败。逐电机 `canfd` 原样交给
JointSDK 判断，类型错误及缺省值均由 SDK 处理。

### 2. 能力与关节绑定，配置完成后一次性缓存

在 `JointRuntime` 中用单个 `BrakeState brake_state` 替换能力和松闸两个布尔标志。
枚举值固定为 0 Unsupported、1 Engaged、2 Released、3 Engaging、4 Releasing。
运行期 0 不转换，正常动作按 1→4→2→3→1 转换；位置缺失、非有限或 Brake(false) 返回 false 时允许 4→1 回退，不增加 ROS 状态接口。
动作开始前设置 3/4，成功返回才进入目标稳定状态；松闸返回 false 时回退 Engaged，
抱闸返回 false 时回退 Released（3→2），可重试；SDK 异常仍保留过渡状态，重配可重新初始化。
状态记录 SDK 操作是否确认，不代替物理状态检测，也不引入异步线程。

每次 `on_configure()` 清除旧句柄、能力和松闸记录。全部 `AddJoint()` 完成、运行时句柄
绑定及双耦合配对完成后，再逐关节调用一次 `HasBrake()` 并缓存。不能在首次创建从轴
后立即查询，因为尚未绑定的 `SlaveJoint::HasBrake()` 会失败。

配置未全部成功时，清除本次句柄/缓存并保持未配置状态。重复 activate/deactivate、
模式切换及 read/write 不主动调用 `HasBrake()`；重新配置时对新实例各查询一次。
SDK `Brake()` 内部已有的能力检查不修改，也不计作硬件层重复探测。

全部关节创建和配对完成后，在同一个循环中逐关节查询 HasBrake。
新绑定初始为 Unsupported；HasBrake 为 true 时以 Released 初始化请求起点，立即调用
EngageBrake，执行 Released→Engaging→Engaged。此初始化起点不是物理松闸检测结果。
初始化和销毁绑定不属于既有绑定的运行期状态转换。
不支持抱闸的关节跳过；任一失败立即走既有配置异常路径清除绑定和缓存、返回 ERROR。
仅初始抱闸全部成功后才允许配置成功。

硬件只处理 SDK 关节及其 `brake_state`，不识别电机数量、关节具体类型或配对键，
不建立抱闸分组，不选取代表关节。每个支持抱闸的关节分别调用 `Brake()`，物理电机和
双电机协调全部由 SDK 处理。Unsupported 和 Released 在 ReleaseBrake 中直接返回；
两个函数均返回 BrakeState，Released 重复松闸及 Engaged 重复抱闸均静默返回当前状态。
只有 Engaged 可以发起松闸，只有 Released 可以发起抱闸。
状态 3/4 在 write 中静默跳过该关节，不影响其他关节；直接调用任一函数都静默返回
当前枚举值，不重复调用 SDK。两个函数放在生命周期方法之后。

### 3. 激活不释放抱闸

`on_activate()` 沿用完整有限反馈准入，最多 20 轮、轮间 2 ms；满足条件后置 active。
激活不调用 `Brake(false)`，不发送 `PosControl()` 保持命令，也不安装临时反馈观察回调。
删除保持目标、保持状态、保持快照函数及 `PrepareAndReleaseBrakes()`。有限值检查仅剩
激活一处使用，直接写在激活流程中，不再保留 `ValidFeedback()` 辅助函数。

### 4. 切换不操作抱闸，逐关节派发有效命令

prepare 仅校验接口集合；perform 完成全部模式校验后记录新模式、保留命令缓存。
模式切换回调不调用 Brake，不改变 brake_state；控制器退出造成 None 时由后续 write 抱紧。
删除 awaiting_first_command_ 及实例首帧命令门；每个关节独立派发，不等待其他关节。

命令校验保留各模式原有的 if/else 条件和接口错误日志；不抽取 HasValidCommand。
每个校验成功分支在控制调用前调用 ReleaseBrake(joint)。该函数内部对无抱闸或已经
松闸的关节直接返回 Unsupported/Released，不切换总线模式、不读取反馈、不发送保持或抱闸命令。
调用处仅在返回 Unsupported/Released 时派发控制命令，否则返回 ERROR。

仅处于 Engaged 的关节，进入 ReleaseBrake 后立即设置 Releasing，再读取 SDK 有效当前位置并调用
PosControl(position, 0.0F, 0.0F, 1)，再调用 Brake(false)。成功后进入 Released，
立即执行该关节控制命令，然后处理下一关节。此阶段临时使用直接发送，成功后恢复同步
发送；普通命令仍由 write 末尾的 Commit 提交。位置缺失或非有限时，尚未发保持或松闸命令，
ReleaseBrake 回退并返回 Engaged，write 返回 ERROR；反馈恢复后可再次尝试。
Brake(false) 返回 false 时同样回退并返回 Engaged，后续可以重试；SDK 异常仍保留 Releasing。
当次 write 返回 ERROR，不派发失败关节的控制命令；此前已完成操作不撤销。
已处于过渡状态时 write 只跳过该关节，不因该状态返回 ERROR；SDK 当次实际失败仍返回 ERROR。

再次切换只保留命令缓存，已松闸关节不会重新保持或松闸。不保存保持目标、不补齐半条命令，
读取保持位置时最多查询 5 次，反馈缺失时在两次查询之间等待 1 ms，首次立即查询；
不增加观察器或工作线程。无效命令按原有分支记录错误并跳过该关节。
None 模式下，Released 关节在 write 中调用 EngageBrake 抱紧；Engaged 静默跳过，
Engaging/Releasing 沿用周期开始的静默跳过，无抱闸关节使用 CurControl(0.0F, 1)。
None 分支仅调用 EngageBrake 并检查是否返回 Engaged；发送模式在 EngageBrake 内管理，
实际抱闸前关闭同步，成功后恢复同步，与 ReleaseBrake 一致；
未完成返回 ERROR，成功后不重复抱紧。此关节不发送控制命令或位置保持。

SDK Brake 等待确认的耗时仍计入首次松闸的 write；模式切换不再等待抱闸确认。

### 5. 停用、错误和关闭

`on_deactivate()` 停止普通命令派发，清空命令，在回调内
逐关节调用 EngageBrake：仅 Released 可以先进入 Engaging，再由函数内部切换直接发送并调用 Brake(true)，
成功后恢复同步模式；无需动作的返回路径不切换模式。
成功后进入 Engaged，所有正常返回路径返回当前 BrakeState；退出回调仅将
Unsupported/Engaged 视为完成，3/4 则记为错误并继续处理其他关节。
Brake(true) 返回 false 时回退并返回 Released，允许后续重试；SDK 异常仍保留 Engaging。
一个失败不阻止其他关节的尝试，最终返回 ERROR。
`on_error()`、`on_shutdown()` 复用停用逻辑。未配置、空句柄及不支持抱闸的关节不发送抱闸命令。
机械 `Brake(true)` 与 `Stop(FullBrake)` 电气停止语义独立，不互相替换。

### 6. 历史规格与接口兼容

本变更明确替代以下规则，但不修改其历史文件：

- `add-canfd-enable-parameter`：全局 hardware 参数不再决定 CAN FD。
- `dynamic-joint-adapter-template`：至少一组编号参数改为仅使用占位符时需要。
- `add-controller-switch-first-command-gate`：移除实例首帧命令门，切换时仍保留命令缓存。
- 旧生命周期规则：激活不松闸，切换不抱闸，首次有效命令触发松闸，生命周期退出时抱紧。

现有八个命令接口为 `position/velocity/effort/current/kp/kd/torque_mode/stop_current`，
六个状态接口为 `position/velocity/effort/mos_temperature/motor_temperature/motor_error`。
不恢复历史的 `control_mode/feedback/stop_mode` 接口。SDK 测试替身需要删除新版已移除
的 `SetZero/GotoLimit/GotoZero/Calibrate` 覆盖，并增加能力、抱闸结果与操作顺序记录。

## Risks / Trade-offs

- 外部旧 JSON 缺少 `canfd` 时会使用普通 CAN，旧启动参数不再覆盖；本次不代改这些文件。
- 当前检查的驱动源码型号表全部为 `brake=false`。即使机械装置有抱闸，插件也不会推断
  为可控制；正确型号标记及固件命令支持是实机验收前置条件。
- 释放或回抱可能部分失败，尤其双电机不是物理原子操作。日志和 ERROR 必须保留该事实，
  不能以成功的软件测试承诺失电、通信中断或固件异常下的物理安全。
- SDK/驱动可能共享底层适配器，直接/同步模式切换仍遵循现有 SDK 所有权语义；本变更
  不新增跨硬件实例协调或物理同步保证。
- PosControl 必须受关节类型支持；速度、电流默认策略和实际承载效果需要实机验证。
  SDK 会暂存耦合关节的首个位置命令；逐关节保持、松闸、控制不能保证两轴保持先于
  松闸下发，即使两轴命令均有效。硬件不解析拓扑或补齐另一轴命令。

## Verification and Delivery

使用已安装 SDK 单任务构建和运行全部包内 C++/Python 测试，检查 clang-format、clang-tidy、
编译诊断及 OpenSpec 全量严格校验。Mock 覆盖激活不松闸、切换无抱闸操作、首次松闸与命令顺序、
无效命令、切换保留命令缓存、重复切换、无控制器认领及操作失败。Fake 验证真实 SDK 双耦合关节
及逐电机帧标志、提交异常后生命周期抱紧。删除已不适用的位置保持和临时反馈回调测试。

整机 `control_v2.json` 仅做无硬件解析验证；不执行实机动作，不修改 SDK 安装或其他子仓库。
软件测试通过不代表实机抱闸验收，历史结果保留在 verification.md 并由最新记录说明替代关系。
