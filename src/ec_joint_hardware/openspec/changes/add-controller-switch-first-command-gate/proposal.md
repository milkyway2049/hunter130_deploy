## Why

切换控制器时，controller_manager 从 `do_switch = true` 起就同时跳过新旧控制器（`skip_cycle`，
`controller_manager.hpp:744-751`），直到 `manage_switch()` 完成 deactivate 与 activate（含重量级
`on_activate`）为止，本实例的关节没有任何命令写入。本插件每次 `write()` 末尾把所有命令槽重置为 `NaN`，
于是这段窗口内每拍都命中"已绑定模式却未写命令"的校验分支，把一次正常切换报成
静默期长度 × `update_rate` 条非法命令错误，同时掩盖真正需要暴露的"控制器已绑定却每拍不写"故障。

## What Changes

- `EcJointHardware` 增加实例级的**首帧命令门**（默认关闭）。
- `prepare_command_mode_switch()` 校验通过后开门；`perform_command_mode_switch()` 在写入新模式之前开门
  （后者才是决定性的：`prepare` 与 `do_switch = true` 之间旧控制器仍可能写入并把门关掉）。
- `write()` 在逐关节派发之前判断门：只要有**一个已认领的关节**本周期没写命令，就整批跳过本周期（不派发、
  不记错误、不合成命令）；全部已认领关节都写入首帧命令才关门并在同一周期派发。未绑定模式的关节不参与判定。
- **不设超时**：控制器持续不写命令时关节持续静默（明确取舍，见 design 的 Non-Goals 与 Risks）。
- `on_configure()` 清除门状态；README 同步门语义与两条已知代价。

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `ec-joint-hardware`：命令校验语义新增「控制器切换后的首帧命令门」例外，并规定门的开启、关闭与无超时语义。

## Impact

- 实现范围限于 `robot_hardwares/ec_joint_hardware`：`include/ec_joint_hardware/joint_runtime.hpp`、
  `src/joint_runtime.cpp`、`src/ec_joint_hardware.cpp`、README 与包内测试。不改动 `joint_sdk`、控制器代码、
  JSON 配置、xacro 描述，也不新增 hardware 参数或接口。
- 对外可观察的变化只是错误日志：静默期内的逐周期误报消失；关门后"已绑定却未写命令"的既有语义不变。
