## Context

见 proposal.md 的 Why。实现层面的现状约束：

- Jazzy 的 `hardware_interface::HardwareComponentInterface` 提供
  `prepare_command_mode_switch(start_interfaces, stop_interfaces)` 与
  `perform_command_mode_switch(...)` 虚函数（hardware_component_interface.hpp:246/260），
  参数为 `"joint_name/interface_name"` 字符串列表。官方头文件注释明确了两者的分工：
  prepare 返回 `OK` 表示新的命令接口组合可以被接受，返回 `ERROR` 则拒绝本次切换；
  perform 在实时 update loop 中执行，必须快速。ResourceManager 在任何控制器
  激活/停用导致认领变化时调用这对回调。
- 当前实现用 `control_mode` double 逐周期解码模式（`ParseControlMode`），
  `PendingCommand` 在 `write()` 内即时构造；关节运行时状态只有命令/状态存储，
  没有「模式」记录。
- `stop_mode` 当前映射 SDK 的 `MotorStopMode::{Full,Dynamic,Regenerative}Brake`；
  新固件只有全刹，`Stop(FullBrake, current, feedback)` 是唯一合法调用。
- `feedback` 当前透传 SDK 的反馈类型选择（0/1/2/3）；实际使用只需要完整状态，
  固定为 1。

## Goals / Non-Goals

**Goals:**

- 模式在控制器切换点一次性绑定，运行期零模式解码、零模式切换路径。
- 非法认领（不完整、跨模式混合、多控制器冲突）在切换点被拒绝，而不是在 write()
  的实时循环里报错。
- `write()` 的热路径保持简单：查表得模式 → 校验必填值 → 派发。
- 接口集合收敛到语义最小集：8 个命令接口，无模式选择、无制动选择、无反馈类型选择。

**Non-Goals:**

- 不改变 read()、看门狗、激活准入、错误边沿日志、生命周期语义。
- 不引入跨实例或跨总线的同步承诺。
- 不修改 JSON 配置格式与插件加载名。
- 不在本 change 内重写任何 controller；controller 侧的适配只定义契约（README/spec）。

## Decisions

### D1：模式来源 = 切换回调中记录，匹配用字符串名直接比较

模式定义收敛为一张静态表：每行含模式名（日志用）与认领接口名集合。

```cpp
struct ModeDefinition {
  const char* name;
  std::array<const char*, 6> interfaces;
  std::size_t interface_count;
};
```

`perform_command_mode_switch()` 对每个关节应用起停列表得到最终认领的名字集合
（`std::vector<std::string_view>` + `std::find`/`std::remove`，不引入自定义集合类型），
与表中六行逐一做「数量相等且全部名字命中」的精确匹配，把命中行的指针
（`const ModeDefinition*`）存入关节运行时状态；`nullptr` 表示未绑定。模式的唯一
表示就是这个表指针——不引入枚举或位掩码中间层，`write()` 用具名常量引用
（如 `kPvtMode`）做指针身份比较派发。切换回调仅做字符串比较与小 vector 操作：
切换是稀有事件（controller 激活/停用触发，非每周期路径），偶发的小额分配可接受，
ros2_control 自身的切换路径同样分配。

备选方案：a) write() 每周期比较认领接口集合。放弃——ros2_control 不提供「当前认领
集合」的查询接口，切换回调是唯一权威来源。b) 位掩码表示集合。放弃——掩码需要
「位序与接口名数组一致」的隐式约定，不如名字集合直白。c) 模式枚举。放弃——枚举与
集合之间需要双向映射函数，是纯往返的中间表示。

### D2：`prepare_command_mode_switch()` 在「应用起停后的最终状态」上校验

对每个关节，先取当前记录的认领集合（由模式表指针还原），应用 stop 列表移除、
start 列表加入，再校验结果集合：空集（允许，关节无控制器）或恰好匹配表中一行。
任一关节非法即返回 `ERROR`，整个切换被 ResourceManager 中止。

prepare 官方定位是**非实时评估**（hardware_component_interface.hpp 注释：
"non-realtime evaluation... call to start preparing data structures"），此处允许分配；perform 是实时路径但切换为稀有事件，
偶发小额分配可接受。两者共用同一套 vector 实现。

备选方案：只对 start 列表增量校验。放弃——无法发现「新认领与既有认领拼成跨模式
混合」（如已有 `effort`，新增 `current`）。

### D3：停止用 `stop_current` 单接口，值即制动电流

停止模式认领 `{stop_current}`：接口名与电流模式的 `current` 天然不同，认领集合
直接可区分；写入的值即 `Stop(FullBrake, stop_current, 1)` 的制动电流，无标记位、
无语义空转的参数。

备选方案：a) 标记接口 `stop` + 复用 `current`——多一个数值不承载语义的接口，
且停止 `{stop, current}` 与电流 `{current}` 的区分依赖标记位，放弃；
b) 保留 `stop_mode` 但只允许 `2`——接口名误导（暗示有选择），且新固件没有该概念。

### D4：单关节非法不阻断整批，逐关节独立校验 + 错误日志；未写命令 = 非法（有意的日志风暴）

**批次失败语义改为逐关节独立校验**（与最初设计相比的重大语义变更，用户拍板）：单个关节命令非法只影响
该关节本周期跳过并记录一次错误日志（含关节名、模式、缺失/非法接口名），其余关节照常派发并 Commit；`write()` 仅在 SDK 异常
时返回 ERROR。

必填接口全 NaN 不再表示「本周期无命令跳过不报错」，而是视为**非法命令**（控制器已绑定却未写入即错误），记录
错误日志并跳过该关节——控制器一旦绑定就必须每周期写命令，「停写」就是控制器故障，日志风暴是故意的报警信号。

备选方案：全 NaN 跳过不报错（旧批次语义）。放弃——用户明确「一旦绑定控制器就应该立刻控制关节，日志
爆炸是严重错误的表现」。

### D5：反馈类型固定为 1，`feedback` 接口删除

电机反馈只有「完整状态」一种实际需求，类型选择是 SDK 内部细节。带反馈参数的 SDK
调用（Pos/Spd/Cur/Tor/Stop）固定以 `feedback=1` 下发；PVT 本就总是请求反馈。

### D6：接口名集合常量从 10 改为 8，校验逻辑不变

`kCommandInterfaceNames` 删除 `control_mode`、`stop_mode`、`feedback`，新增 `stop`；
`HasExactlyInterfaces` 与 xacro 宏同步更新，`on_init()` 的精确集合校验自然拒绝
旧描述。

### D7：logger 成员化

`rclcpp::Logger` 是轻量句柄但 `get_logger()` 每次调用都要查日志器注册表；作为成员
（构造时初始化一次）消除每条日志路径上的重复查找。本类所有日志统一使用该成员。

### D8：`write()` 单循环，逐关节独立校验，批次原子性由 `Commit()` 边界保证

校验与派发合并为一个关节循环：逐关节校验必填接口，非法则记录错误日志并跳过该关节
（不阻断其余关节）；校验通过立即调用该关节的 SDK 控制接口（只排队不上总线）；循环
结束后恰好调用一次 `manager_.Commit()`（sync 模式下 `Commit()` 是命令到达总线的唯一
边界）。`write()` 仅在 SDK 控制调用或 `Commit()` 抛出异常时返回 `ERROR`。

前置条件与依据：SDK 的 `sync_mode` 默认关闭（`bus_impl.h`），关闭时
`SubmitDeviceMessage` 直接发送（`base_adapter.cc:201-222`）；而 `CommitBus` 每次执行
都无条件把 `sync_mode` 置 true（:247）——**调用一次 `Commit()` 即永久进入软同步**，
无需新增任何 SDK API。激活与重试阶段的反馈请求不需要软同步（直发即可），因此
`on_activate()` 的重试循环每轮恰好调用一次 `manager_.Commit()`：首轮请求直发、后续
轮排队时由它冲刷（空队列 drain 是 no-op），激活成功那一轮的 Commit 顺带闩锁软同步
模式。此时 Bus 扫描、电机创建与设备初始化均已完成，满足 `SetSyncMode` 的时序约束
（base_adapter.h:100）。joint_sdk 零改动。

备选方案：保留「预检循环 + 派发循环」双循环。放弃——sync 模式保证排队命令不 Commit
不上总线后，双循环只是形式上的分批，单循环把校验与派发写在同一处反而消除
「两处按同一模式分派逻辑」的漂移风险。

### D9：删除 `IsExactInteger`，torque_mode 用精确比较

`control_mode`/`stop_mode`/`feedback` 删除后，`IsExactInteger` 只剩 `torque_mode`
一个用途；0/1 校验直接用 double 精确比较（`!= 0.0 && != 1.0` 即非法），NaN 与任何
值的比较均为 false，天然被同一条件拒绝，无需转换到 int，也就不存在越界 UB 问题。

## Risks / Trade-offs

- [模式切换的生效时机]：控制器切换中 deactivate→activate 之间存在间隙，间隙内关节
  无模式绑定、命令全 NaN → 硬件不发控制，电机靠 SDK 心跳保护失能。这是官方机制的
  固有语义，文档中明确告知 controller 开发者。
- [`stop` 与 `current` 同时被两个控制器认领]：D2 的冲突校验在切换点拒绝；唯一残余
  场景是同一关节先绑定电流模式后，停止控制器再激活——此时 start 集合与既有集合混合，
  prepare 拒绝。→ 文档说明「停止应做成接管关节全部接口的独立控制器」。
- [双耦合关节的模式绑定]：MasterJoint/SlaveJoint 是独立 ros2_control 关节，各自
  绑定模式；双耦合的运动学约束由 SDK 内部保证，硬件层不新增逻辑。
- [Jazzy 回调签名差异]：on_init 在部分 Jazzy 小版本中已有
  `HardwareComponentInterfaceParams` 风格的 deprecation 警告（本包已存在）。模式
  切换回调签名当前稳定。→ 实现时以当前 Jazzy 头文件签名为准，升级时随警告一起迁移。

## Migration Plan

1. 硬件：改接口名集合（10→8）、加模式记录与切换回调、重写 write() 派发、删
   `ParseControlMode`/`ParseStopMode`/`ControlModeName`/`IsExactInteger`
   （`MotorErrorName` 保留）。
2. Xacro 宏更新为 8/6 集合；展开全部机器人描述验证。
3. 测试重写：切换接受/拒绝、六模式派发（停止固定 FullBrake、反馈固定 1）、批次语义。
4. README 与 spec 同步。
5. Controller 适配（本 change 之外）：各控制器按模式认领固定集合，删除
   `control_mode`/`stop_mode`/`feedback` 的认领与写入。

回滚：接口契约变更无运行时迁移路径，回滚 = 恢复上一提交并同步回滚 Xacro 与
controller。

## Open Questions

- 停止控制器的归属：独立 StopController 还是并入安全监控控制器——属于 controller
  侧设计，不影响本硬件契约。
