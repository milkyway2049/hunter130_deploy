## 1. 首帧命令门

- [x] 1.1 `include/ec_joint_hardware/ec_joint_hardware.hpp`：增加实例级门成员 `awaiting_first_command_`，
  带中文 Doxygen 注释；不新增辅助函数声明。
- [x] 1.2 `src/ec_joint_hardware.cpp`：判定内联在 `write()` 的门分支里（某个已绑定模式的关节八个命令槽
  全为 `NaN` 即视为本周期没写；未绑定模式的关节跳过不参与）；`prepare_command_mode_switch()` 校验通过后
  开门、`perform_command_mode_switch()` 在写入新模式之前开门；门内只要有一个已认领关节没写就整批跳过，
  全部写入则当拍关门并照常派发；`on_configure()` 清门。
- [x] 1.3 `JointRuntime` 与 `types.hpp` 不做任何改动；不引入计数、超时、新参数与新接口。

## 2. 测试

- [x] 2.1 既有 `UnwrittenRequiredInterfacesSkipJointAndEveryWriteResetsCommands` 断言不变，注释改为"门内整批跳过"。
- [x] 2.2 新增 `FirstCommandGateReleasesOnCompleteCommandAndKeepsExistingSemantics`：门内不派发 → 全部写入后
  当拍派发 → 关门后漏写恢复既有逐关节语义。
- [x] 2.3 新增 `FirstCommandGateWaitsForEveryClaimedJoint`：部分关节写入时整批继续跳过（含已写的那个）；
  全部写入后同一周期一起派发。
- [x] 2.4 新增 `ReleasingEveryClaimResumesDampingCommand`：释放全部认领后门当拍关闭，未绑定模式的阻尼命令照常下发。

## 3. 文档

- [x] 3.1 README：行为表补充"控制器切换后、新控制器首帧命令到达前"一行；运行期错误语义补充首帧命令门条目，
  含"全部已认领关节写入才关门""实例级""无超时 ⇒ 整实例永久静默"与残留 1~2 条日志的说明。

## 4. 验证

- [x] 4.1 构建与测试：`MAKEFLAGS=-j2 colcon build --packages-select ec_joint_hardware --parallel-workers 2
  --cmake-args -DBUILD_TESTING=ON`，`colcon test --parallel-workers 2`，检查 `colcon test-result`。
- [x] 4.2 对改动的 C++ 文件运行 clang-format，运行 `git diff --check`。
- [x] 4.3 运行 `openspec validate add-controller-switch-first-command-gate --strict --no-interactive` 与
  `openspec validate --all --strict --no-interactive`。
- [ ] 4.4 实机验证：`slave_robot_bringup` 下往返切换控制器，确认每次切换的非法命令日志条数从
  "静默期长度 × `update_rate`"降到 1~2 条，且稳态无 ERROR；确认门内不派发是否让反馈看门狗过期。

## 验证结果（2026-09-21）

- 构建成功，无新增告警（仅既有的 `on_init(HardwareInfo)` 弃用告警）。
- `colcon test-result`：96 tests, 0 errors, 0 failures, 0 skipped；硬件测试新增 3 个用例，既有
  `DispatchesEverySdkControlMode`、`PrepareDoesNotApplyModeUntilPerform`、`ModeClearedWhenClaimReleased`、
  `InvalidCommandSkipsJointWithoutBlockingBatch`、`RejectsInvalidDiscreteInterfaces` 等无需改写即通过。
- 改动文件通过 `clang-format --dry-run --Werror`；`git diff --check` 无输出。
- OpenSpec：change 与全仓库 strict 校验均通过。
- 未完成：4.4 实机验证。
