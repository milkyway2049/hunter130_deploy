## 1. 接口集合与类型定义

- [x] 1.1 `types.hpp`：`kCommandInterfaceNames` 删除 `control_mode`、`stop_mode`、
  `feedback`，新增 `stop`（8 个接口）；接口位掩码即模式身份，不引入模式枚举。
- [x] 1.2 删除 `ParseControlMode`、`ParseStopMode`、`ControlModeName`；保留
  `IsExactInteger`（`torque_mode` 校验仍用）与 `MotorErrorName`。
- [x] 1.3 关节运行时状态新增模式记录字段（`perform_command_mode_switch` 写入），
  命令存储删除 `command_feedback`、`command_stop_mode`，新增 `command_stop`。

## 2. 命令模式切换回调

- [x] 2.1 实现 `prepare_command_mode_switch()`：对每个关节应用起停列表得到最终认领
  集合，校验为空集或恰好一个合法模式集合（静态查找表），非法即返回 `ERROR`。
- [x] 2.2 实现 `perform_command_mode_switch()`：把每个关节的最终认领集合解析并记录为
  对应模式（或无绑定）；实时路径内只做集合解析与 POD 写入，不分配、不调 SDK。
- [x] 2.3 声明处（hpp）与插件导出保持 Jazzy 当前回调签名，不触碰既有 deprecation 警告。

## 3. write() 派发重写

- [x] 3.1 逐关节独立校验：必填接口缺失/非法则记录错误日志并跳过该关节，其余关节照常派发并 Commit；write() 仅在 SDK 异常时返回
  ERROR（逐关节独立语义：单关节非法不阻断其他关节，返回 OK；仅 SDK 异常时返回 ERROR）。
- [x] 3.2 按模式派发六个 SDK 调用；停止固定 `Stop(MotorStopMode::FullBrake, current,
  1)`；Pos/Spd/Cur/Tor 固定 `feedback=1`；`stop` 仅作非 NaN 触发标记。
- [x] 3.3 保留整批成功后恰好一次 `manager_.Commit()`、无命令不提交、每周期
  `ResetCommands()` 与 SDK 异常单次日志语义。

## 4. Xacro 与机器人描述

- [x] 4.1 `ec_joint_hardware_joint_interfaces` 宏更新为 8 command / 6 state，删除
  `control_mode`、`stop_mode`、`feedback`，新增 `stop`。
- [x] 4.2 展开所有使用该宏的机器人描述，验证接口集合与实现一致。

## 5. 测试重写

- [x] 5.1 更新接口导出测试为 8/6 集合；新增「声明含 `control_mode`/`stop_mode`/
  `feedback` 的旧描述被 on_init 拒绝」用例。
- [x] 5.2 切换回调测试：六种合法认领接受并记录正确模式；不完整、跨模式混合、多控制器
  冲突、空集认领各被拒/接受。
- [x] 5.3 write() 测试：六模式正确派发（停止固定 FullBrake、Pos/Spd/Cur/Tor 固定 feedback=1、必填缺失/非法跳过并记录日志，write 返回 OK（仅 SDK 异常时 ERROR）、每周期 Commit 一次、SDK 异常
  单次日志、每周期 NaN 复位。
- [x] 5.4 保留并适配既有测试：read 六状态直通、看门狗缺失 ERROR、激活准入与
  不自动刹车语义。

## 6. 文档与验证

- [x] 6.1 README 更新：接口表（8/6）、认领集合-模式表、切换校验语义、`stop` 标记与
  固定全刹、反馈固定完整状态；删除 `control_mode`/`stop_mode`/`feedback` 全部内容。
- [x] 6.2 运行 clang-format、clang-tidy、包构建、全部 GTest、Xacro 展开、
  `git diff --check` 与 `openspec validate --all --strict`。

## 7. 实现结构精简

- [x] 7.1 删除 `IsExactInteger`：`torque_mode` 改用 `0.0`/`1.0` 的 double 精确比较校验。
- [x] 7.2 模式匹配改用字符串名直接比较：`ModeDefinition` 静态表 + 具名常量引用，
  删除位掩码机制（`InterfaceBit`/`kXxxInterfaces`/`IsModeMask`）；`JointRuntime`
  改存 `const ModeDefinition*`。
- [x] 7.3 logger 成员化：`rclcpp::Logger` 成员构造时初始化一次，全部日志改用该成员。
- [x] 7.4 `on_activate()` 重试循环每轮恰好调用一次 `manager_.Commit()`：冲刷本轮
  排队的反馈请求（空转 no-op），成功轮顺带闩锁软同步；删除 `feedback_requested`
  标志与重复 Commit。joint_sdk 零改动。
- [x] 7.5 `write()` 合并校验与派发为单关节循环；校验失败立即返回 `ERROR` 且不
  `Commit()`。
- [x] 7.6 回归验证：全部 GTest、clang-format、`git diff --check`、
  `openspec validate --all --strict`。
- [x] 7.7 `stop` 接口更名 `stop_current`：停止模式认领集合改为单接口
  `{stop_current}`，值即制动电流；更新 types.hpp、xacro 宏、write() 停止分支、
  测试与 README。
