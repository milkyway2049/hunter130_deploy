# 实现验证记录

日期：2026-10-12。分支：`feat/hds/add_brakc_motor`。修改范围仅为 `ec_joint_hardware`。
使用系统安装的 JointSDK 1.4.5；CMake 正常发现 `/usr/local/lib/cmake/joint_sdk`，未安装临时
SDK、未强制设置 `joint_sdk_DIR`，也未改 SDK、驱动、其他子仓库或外部整机配置。

## 功能与测试证据

- 完整适配器 JSON、固定配置及占位符、整机配置样例、声明顺序和原始电机字段：配置测试。
  `test/fixtures/control_v2.json` 是参考文件的本地快照，测试不依赖开发机绝对路径。
- CAN FD 真/假/缺省和反向旧参数、重新配置后实际发送帧、数字/字符串 adapterId 混用、
  双耦合逐电机帧格式：Fake 集成测试。
- 全部配对后的一次性能力查询、生命周期复用、重配刷新及部分配置失败清理：Mock 计数、
  Fake 新旧电机能力切换、真实 SDK 从轴优先创建测试。
- 保持先于释放、旧缓存/注册时回调不满足新反馈要求、有限等待、非法反馈及电机错误、
  异步反馈、运行时重建和硬件销毁后的延迟回调：Mock 测试。
- 注册/解除反馈观察失败、释放 false/异常、当前失败组也回抱、回抱失败继续其他组、
  三个退出回调的成功与失败路径：Mock 测试。
- 固定目标不追随漂移、首帧等待及非法首帧持续保持、双轴共同认领、退出后重采样、
  无效快照拒绝切换、调用异常及真实 Fake `Commit()` 发送异常不确认交接：Mock/Fake 测试。
- 八命令六状态接口、原有控制模式、无抱闸行为、批次提交和 Python UI：现有回归测试。

完整整机样例仅作解析。真实 SDK 双耦合测试抽取其中一对关节，替换为 Fake 电机与适合
测试电机的速度/力矩限制；没有连接或驱动真实机器人。

## 构建与自动检查

在 robot_ws 根目录加载 ROS Jazzy 环境，使用单任务：

```bash
source /opt/ros/jazzy/setup.bash
MAKEFLAGS=-j1 CMAKE_BUILD_PARALLEL_LEVEL=1 colcon build \
  --packages-select ec_joint_hardware --executor sequential --parallel-workers 1 \
  --cmake-args -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
source install/setup.bash
MAKEFLAGS=-j1 CMAKE_BUILD_PARALLEL_LEVEL=1 colcon test \
  --packages-select ec_joint_hardware --executor sequential --parallel-workers 1
colcon test-result --test-result-base build/ec_joint_hardware --verbose
```

- 构建成功；94 个实际测试用例通过：配置 11、运行时 5、硬件 66、Python 12。
  `colcon test-result` 含 4 个 CTest 汇总项，输出 `98 tests, 0 errors, 0 failures, 0 skipped`。
- 修改的 C++ 文件按仓库 `.clang-format` 格式化，`clang-format --dry-run --Werror` 通过。
- clang-tidy 检查集为 `clang-analyzer-*`、`bugprone-*`、`performance-*`、`modernize-use-nullptr`；
  使用构建生成的编译数据库，生产代码与修改的测试均检查，未开启第三方系统头诊断展示。
- 编译保留原有 ROS `on_init(HardwareInfo)` 弃用告警；未扩展为 ROS 初始化接口迁移。
- 生产代码 clang-tidy 的既有提示为 `ModeDefinition` 枚举宽度、激活反馈的重复失败分支、
  `ResolveMode` 两个同类型参数。没有新抱闸实现诊断；未为消除旧提示改动模式 ABI 或接口。
  既有测试辅助函数另有同类型参数、字符串拼接和可选状态读取提示，均记录为历史代码检查项。
  新增测试的可选状态访问已集中到显式检查函数，并复跑对应检查确认无新增提示。
- `git diff --check` 通过。
- 本 change 严格验证通过；`openspec validate --all --strict --no-interactive` 为 5/5 通过。

本机执行日志保存在 `/tmp/ec_joint_hardware_build.log`、`/tmp/ec_joint_hardware_test.log`、
`/tmp/ec_joint_hardware_tidy.log`、`/tmp/ec_joint_hardware_test_tidy_final.log` 和最终定向复查
`/tmp/ec_joint_hardware_optional_tidy.log`。

## 外部限制与实机验收

当前安装驱动对不导出 `MakeAdapterV2` 的旧 Fake 插件会告警并忽略非空 `extend`，而非抛异常。
测试观察到该驱动告警，证明硬件确实把扩展配置传到 SDK/驱动；本包不另行实现驱动校验。
真实 CAN 总线参数生效需对应适配器插件支持扩展接口及相关字段。

型号能力和固件支持必须在实机前确认。当前检查的驱动型号表没有带抱闸标记；测试通过
显式 Fake 属性和 Mock 注入能力，不能作为真实电机型号支持抱闸的证明。新反馈和成功返回
仅提供软件时序/通信证据，不能证明受载保持力或故障下物理安全；受载保持、实际释放/回抱、
通信故障和断电行为均尚未进行实机验收。

## 2026-10-13 调整

按后续要求移除 `canfd_enable` 的检测及弃用告警。硬件层不再读取该参数，逐电机 JSON
继续原样传给 JointSDK，由 SDK 判断帧格式、默认值和配置有效性。README 与当前变更规格
已同步；沿用现有旧参数不影响初始化、JSON 帧模式及重配的回归测试。

单任务构建、94 个实际测试用例、clang-format 与 OpenSpec 全量 5/5 验证通过；
`colcon test-result` 为 `98 tests, 0 errors, 0 failures, 0 skipped`。
本次日志前缀为 `/tmp/ec_joint_hardware_canfd_cleanup_`。

同日将 `ResetJointBindings()` 的清理逻辑直接内联至 `on_configure()` 的入口和异常分支，
删除独立声明及定义，保留重配及配置失败的状态清理语义。重新构建和全部测试通过，
日志前缀为 `/tmp/ec_joint_hardware_inline_reset_`。

## 2026-10-13 合并与精简

按减少拆分的要求，将 `ec_joint_brake.cc` 合并到 `ec_joint_hardware.cpp`，删除独立文件
及 CMake 源文件项；两份生产实现从合计 712 行减少到 628 行。

- 激活流程统一处理 SDK 异常，复用有限反馈检查和等待常量。抱闸保持直接使用本次激活
  已验证并保存的位置快照，不再重复读取初始反馈。
- 各控制模式共用一次命令校验及错误日志；删除 SDK 创建契约和已匹配名称的重复判断。
- 错误与关闭回调直接复用停用逻辑，移除中间生命周期包装方法。
- 保留新反馈观察的生命周期管理、双轴保持与共同认领、失败回抱及成功提交后交接。

单任务构建、94 个实际测试用例、clang-format、diff 检查及 OpenSpec 5/5 均通过。
本次合并源文件的 clang-tidy 仅报告既有枚举宽度提示，原激活分支重复提示已随精简消除；
构建仍有既有 ROS 初始化接口弃用告警。日志前缀为 `/tmp/ec_joint_hardware_simplify_`。

## 2026-10-13 按 SDK 关节简化抱闸

按最新要求，当前实现和规格替代此前的双轴分组、代表关节、共同认领及整组交接规则。
删除 `BrakeGroup`、`brake_group`、配对键解析和双轴模式约束；硬件只读取各 SDK 关节的
缓存 `has_brake`，保持目标及交接状态直接随关节保存。双电机协调由 SDK 处理。
`ec_joint_hardware.cpp` 从 628 行减少到 573 行。

测试改为验证逐关节调用、各关节独立认领与交接，并保留真实 JointSDK/Fake 双耦合配置的
激活、松闸、抱紧及逐电机 CAN FD 透传回归；不再断言硬件层去重物理抱闸命令。
单任务构建通过，94 个实际测试用例全部通过；`colcon test-result` 汇总为
`98 tests, 0 errors, 0 failures, 0 skipped`。本次修改的两个 C++ 实现/测试文件通过
clang-format 检查，原有头文件排版保留。日志前缀为 `/tmp/ec_joint_hardware_per_joint_`。
未进行实机动作。

OpenSpec 全量严格校验 5/5 和 `git diff --check` 通过。clang-tidy 完成，仍为既有
枚举宽度及测试辅助函数参数、字符串拼接、旧 optional 访问提示，无本次新增诊断；
构建仍有原 ROS `on_init(HardwareInfo)` 弃用告警。

## 2026-10-13 删除交接临时缓存

删除 `JointRuntime::handover_pending` 及配置阶段的清理代码。命令槽直到 `write()` 结束
才重置，因此下发前和 `Commit()` 成功后直接使用首帧门及 `HasValidCommand()` 判断，
不再把可推导的本周期条件保存为成员。控制调用或提交抛异常时仍保留 `holding`。

单任务构建、全部 94 个实际测试用例及源文件格式检查通过。既有测试覆盖控制调用失败、
真实 Fake 提交异常后继续保持，以及各关节独立交接。colcon 汇总 98 tests、零错误/失败。
日志前缀为 `/tmp/ec_joint_hardware_remove_pending_`。

## 2026-10-13 按控制命令松闸

本次用户要求替代此前的激活松闸、位置保持和保持后新反馈准入设计。当前行为：

- on_activate 仅沿用有限反馈准入，不释放抱闸、不发送位置保持命令。
- prepare 只校验接口；active perform 逐关节抱紧，成功后更新模式、清除旧命令并等待新首帧。
- write 在实例首帧门允许派发且对应命令完整合法时先松闸，再派发同一帧命令。
  用 brake_released 记录成功结果；后续不重复松闸，切换或退出时清除。该记录不代表状态查询。
- 无控制器认领的抱闸关节不派发位置保持或低阻尼 PVT；其他关节沿用原有行为。
- 松闸失败不派发该关节命令；控制调用、提交或松闸失败返回 ERROR，由 on_error 尝试抱紧。

删除 PrepareAndReleaseBrakes、CaptureBrakeHoldTargets、holding、hold_position、反馈观察器、
共享回调状态和锁。ValidFeedback 只剩激活一处使用，直接内联。主实现现为 420 行。
OpenSpec 和 README 已同步，历史验证记录保留，不再代表当前抱闸时序。

本次软件验收：单任务构建成功；配置 11、runtime 5、hardware 59、Python 12，合计 87 个实际
用例通过，colcon 汇总 91 tests、0 errors、0 failures、0 skipped（包含 4 个 CTest 汇总项）。
删除旧保持/回调测试，加入切换抱紧、首帧松闸顺序、旧命令清除、无效首帧、重复切换、
能力混合及错误路径测试。真实 SDK/Fake 双耦合测试确认激活无抱闸/保持报文，首帧控制前
收到松闸报文并保留逐电机 CAN FD 配置。

提交异常测试先在正常直接发送模式下完成首帧松闸，再开启 Fake 缓冲发送来注入后续 Commit
异常，验证 on_error 实际抱紧；缓冲模式下同步 Brake 等确认会超时，不能将它当作正常的
首帧环境。本实现沿用激活时设置的直接发送模式；SDK Brake 同步确认耗时计入首帧 write。
未进行实机动作，也未修改 SDK。

两个修改的 C++ 源文件通过 clang-format 检查，原有头文件排版保留；OpenSpec 全量严格校验
5/5 及 git diff --check 通过。日志前缀 `/tmp/ec_joint_hardware_command_brake_`。

clang-tidy 已完成，无本次新增诊断；剩余为既有枚举宽度及测试辅助函数参数、字符串拼接、
旧 optional 访问提示。构建保留既有 ROS on_init(HardwareInfo) 弃用告警。

## 2026-10-13 停用保留同步模式

按要求删除 on_deactivate 中 SetAdaptersSyncMode(false)、direct 标志及相关异常处理，
直接返回 EngageBrakes 的结果；on_error/on_shutdown 复用该行为。未修改测试预期。

构建成功，13 个相关回归中 11 个 Mock 测试通过，2 个既有 Fake 集成测试失败：
RealSdkCoupledPairCachesBrakesAndPreservesPerMotorCanfd 和
FakeCommitFailureReturnsErrorAndLifecycleEngagesBrake。
当前驱动 Commit 会启用同步模式；此后 Brake(true) 的报文等待提交，而同步调用等待确认，
在没有其他提交的测试环境中超时返回 false，退出回调因此返回 ERROR。该行为未解决，
不能将本次更改标记为完整回归通过，也不能沿用前次退出抱闸成功的结论。
日志前缀为 /tmp/ec_joint_hardware_keep_sync_。未执行实机动作。

## 2026-10-13 切换抱紧不依赖 active 标志

perform_command_mode_switch 不再以 active_ 为抱紧前提，合法切换均调用 EngageBrakes；
空句柄和不支持抱闸的关节仍由该函数跳过。已配置但未激活的关节也会尝试抱紧。
现有模式切换测试加入未激活场景：prepare 不动作，perform 抱紧；激活后仍可继续切换。

单任务构建和 11 个抱闸 Mock 测试通过，格式、diff 和 OpenSpec 5/5 检查通过。
clang-tidy 仅报告既有枚举宽度提示。日志前缀 /tmp/ec_joint_hardware_switch_brake_。
此前保留同步模式引起的 2 个 Fake 集成测试失败尚未解决，本次未重跑该已知失败场景。

## 2026-10-13 松闸前以当前位置保持

按最新要求新增 ReleaseBrakes，替代 write 中直接松闸的代码。对命令完整有效且尚未松闸
的关节，读取 GetStatus(false, 0) 的当前 SDK 位置，先调用 PosControl(position)，再逐关节
Brake(false)，全部成功后派发控制器命令。速度和电流采用 SDK 默认策略，不保存保持目标，
不增加延时、反馈观察器或工作线程；位置缺失、非有限或保持调用抛异常时不松闸。

该函数在保持/松闸期间使用直接发送，成功后恢复同步发送，正常命令仍以一次 Commit
提交。先遍历待松闸关节发送保持，再遍历松闸，让完整双轴命令可由 SDK 完成配对。
硬件不解析电机拓扑；配对轴缺少有效命令时 SDK 可能仍暂存单轴位置命令，该场景的
保持生效不能由本次改动保证。PosControl 不受支持的关节会返回写入错误并保持未松闸。

单任务构建通过；13 个抱闸 Mock 用例通过，包含当前反馈位置、先保持后松闸后控制、
稳态不重复保持、无反馈/非有限位置和保持异常阻止松闸。真实 SDK/Fake 双耦合用例中的
报文顺序及 CAN FD 断言通过；同步队列起始状态下保持/松闸也成功。

全量共 89 个实际用例，87 通过，仍有此前的 2 个退出抱闸失败：
RealSdkCoupledPairCachesBrakesAndPreservesPerMotorCanfd 的 on_deactivate，及
FakeCommitFailureReturnsErrorAndLifecycleEngagesBrake 的 on_error。
退出回调按此前要求保留同步模式，Brake(true) 等待尚未提交的报文确认而超时。本次未改
这两个回调或降低测试预期。colcon 汇总为 93 tests、3 failures（包含重复计数的 CTest 失败项）。

修改的 .cpp 格式、git diff --check 和 OpenSpec 严格校验 5/5 通过；主实现 clang-tidy 仅有
既有枚举宽度提示，构建仍有既有 ROS on_init(HardwareInfo) 弃用告警。
日志前缀 /tmp/ec_joint_hardware_release_hold_。未执行实机动作；发送顺序不等于已验证实际
承载力矩或机械松闸时序。

测试文件 clang-tidy 完成，诊断均为既有测试辅助函数的易混淆参数、字符串拼接和旧 optional
访问提示；本次新增保持及失败路径测试未产生新诊断。

## 2026-10-13 改为单关节传参松闸

按要求改为 ReleaseBrake(JointRuntime& joint)，去掉 pending 容器及函数内部遍历。
write 完成当前关节命令校验后调用，按关节依次保持、松闸、执行控制命令，再处理下一关节。
后续关节失败不会撤销已处理关节的操作，相关 Mock 预期已同步。

构建、13 个抱闸 Mock 用例、格式、diff 和 OpenSpec 5/5 通过；主实现 clang-tidy 仅既有
枚举宽度提示。全量 89 个实际用例中 87 通过，2 个 Fake 集成用例失败。
除此前退出同步抱闸超时外，双耦合用例新增“保持先于松闸”的断言失败：首次松闸前
没有观察到位置保持报文。逐关节调用未完成 SDK 的两轴保持配对；这不是已通过的
双电机松闸交接方案。保留该集成测试的顺序断言，未降低其验收要求。
日志前缀 /tmp/ec_joint_hardware_single_release_。未改 SDK，未执行实机动作。

## 2026-10-13 None 模式使用零电流

将 None 分支从低阻尼 PVT 改为 CurControl(0.0F, 1)。带抱闸关节仍由现有 None 判断跳过。
同步修改既有测试，验证未认领、释放所有认领、混合抱闸能力及控制器退出行为。
单任务构建和 5 个相关回归通过；格式、diff、OpenSpec 5/5 通过，主实现 clang-tidy
仅既有枚举宽度提示。日志前缀 /tmp/ec_joint_hardware_none_current_。
本次未重跑全量测试，此前耦合松闸顺序及退出抱闸超时问题未在本次修改中解决。

## 2026-10-13 恢复原有模式 if/else

恢复 write 各模式原有的有限值、torque_mode 枚举值校验及详细接口错误日志，删除
HasValidCommand 声明和实现；仅在每个校验成功分支的控制调用前加入 ReleaseBrake 判断。
保留 None 零电流及抱闸关节未认领时跳过的行为。

构建成功；59 个硬件用例及 5 个 runtime 用例通过，未重跑此前已知失败的两个 Fake
集成用例。格式、diff、OpenSpec 5/5 通过；主实现 clang-tidy 仅既有枚举宽度提示。
日志前缀 /tmp/ec_joint_hardware_restore_checks_。既有耦合松闸及退出抱闸问题未解决。

## 2026-10-13 移除首帧命令门和切换抱闸

删除 awaiting_first_command_ 字段、所有赋值及 write 中整批等待逻辑。perform 仅校验并
记录模式、清除旧命令，不调用 EngageBrakes，也不重置 brake_released。同模式切换、
换模式和控制器退出均不操作抱闸；各关节沿用原有 if/else 校验独立派发命令。
缺失命令按原有校验记录错误并跳过该关节，不再静默等待整批就绪。

保留首次有效命令前的位置保持和松闸；无抱闸或已松闸的跳过判断已移入 ReleaseBrake。
None 模式仍跳过抱闸关节、对无抱闸关节发送零电流。控制器退出不代表电机已抱紧，也不
主动清除电机端最后的目标；停用、错误和关闭回调仍尝试抱紧并清除软件松闸记录。

构建成功；全量 89 个实际用例中 87 通过，13 个抱闸 Mock 用例全部通过。覆盖部分关节
命令缺失不阻塞其他关节、重复切换不重抱闸/松闸、旧命令清除及控制器退出无机械操作。
仍有两个既有 Fake 集成用例失败：RealSdkCoupledPairCachesBrakesAndPreservesPerMotorCanfd
的保持报文顺序与 on_deactivate，及 FakeCommitFailureReturnsErrorAndLifecycleEngagesBrake
的 on_error；未降低这些用例的断言。colcon 汇总为 93 tests、3 failures（含 CTest 重复计数）。

修改的 .cpp 格式、git diff --check 及 OpenSpec 严格校验 5/5 通过。主实现 clang-tidy
仅既有枚举宽度提示。日志前缀 /tmp/ec_joint_hardware_no_switch_brake_。未执行实机动作。

## 2026-10-13 配置完成能力查询后初始抱闸

on_configure 在全部关节 HasBrake 缓存完成后调用一次 EngageBrakes，对支持关节执行
Brake(true)，不支持的关节跳过。任一失败沿用既有配置异常清理，返回 ERROR 并禁止激活。
不把抱闸插入 AddJoint 或能力查询循环，控制器切换不操作抱闸的行为保持不变。

真实 SDK/Fake 测试确认支持关节配置时收到一次抱紧命令，重配为无抱闸关节后不再发送。
新增回包缺失和发送失败场景，均验证配置失败、绑定及缓存清除且不可激活。
单任务构建成功；全量 90 个实际用例中 88 通过，仍为此前两个 Fake 集成用例失败，
分别涉及耦合关节松闸前保持顺序和生命周期退出同步抱闸超时。相关断言未放宽。
colcon 汇总 94 tests、3 failures（含 CTest 重复计数）。

格式、diff 和 OpenSpec 5/5 检查通过；主实现 clang-tidy 仅既有枚举宽度提示。
日志前缀 /tmp/ec_joint_hardware_configure_brake_。未执行实机动作。

## 2026-10-13 移除 EngageBrakes 辅助函数

删除函数声明及实现。on_configure 在全部能力查询后直接遍历支持关节执行 Brake(true)，
失败立即走既有外层配置异常清理。on_deactivate 内联原逐关节循环，保留失败后继续其他
关节及最终返回 ERROR 的行为；on_error/on_shutdown 继续复用 on_deactivate。

构建及 15 个配置/抱闸相关回归通过；格式、diff、OpenSpec 5/5 检查通过，主实现
clang-tidy 仅既有枚举宽度提示。日志前缀 /tmp/ec_joint_hardware_inline_engage_。
此前两个已知失败的 Fake 集成用例本次未重跑，相关问题未解决。未执行实机动作。

## 2026-10-13 合并能力查询与初始抱闸循环

全部 SDK 关节创建和绑定后，在同一个循环内查询并缓存各关节 HasBrake，支持时立即
调用 Brake(true)。不再先遍历缓存全部能力再另起一个循环抱闸。
构建及 2 个配置抱闸成功/失败回归通过，格式、diff 和 OpenSpec 5/5 通过。
日志前缀 /tmp/ec_joint_hardware_configure_loop_。此前已知集成问题未在本次修改中处理。

## 2026-10-13 单一抱闸状态机

以 BrakeState 替换运行时 has_brake/brake_released：0 无能力、1 已抱闸、2 已松闸、
3 抱闸中、4 松闸中。运行期只允许 1→4→2→3→1，0 不转换；新绑定配置时根据
HasBrake 初始化为 0 或 3，初始抱闸成功后为 1。配置失败清除绑定和状态。
Brake 调用前设置过渡状态，成功返回后设置稳定状态。运行期 false/异常保留 3/4，
返回错误，不重复发起动作、不自动重试；重新配置可重新初始化。控制器切换不操作抱闸。

新增 Mock 回调观察 Brake 尚未返回时的状态，验证重复成功循环、稳定状态去重、失败
状态保留及禁止反向操作；混合能力用例验证状态 0 在控制和停用后保持不变。
构建成功；相关 17 个用例通过。全量 91 个实际用例中 89 通过，两个既有 Fake 集成
失败仍涉及耦合关节保持发送顺序和同步模式下生命周期抱闸超时，未放宽这些断言。
colcon 汇总 95 tests、3 failures（含 CTest 重复计数）。
修改范围 clang-format、git diff --check、OpenSpec 严格校验 5/5 通过。主实现
clang-tidy 仅既有 ModeDefinition 枚举宽度提示。日志前缀
/tmp/ec_joint_hardware_brake_state_。未执行真实电机动作。

## 2026-10-13 简化为三个抱闸状态

按最新要求删除 Engaging/Releasing，只保留 Unsupported=0、Engaged=1、Released=2。
运行期 Brake 成功返回后才在 1/2 间切换；等待及失败时保留原状态，失败返回 ERROR，
后续调用可以重试。配置成功前不使用运行时状态，支持关节初始抱闸成功后设置 Engaged。
本节替代上一节的过渡状态及失败后必须重配策略。

构建成功；17 个相关 Mock/Fake 测试通过，覆盖等待时保持原状态、成功切换、失败保留
和后续重试。修改范围 clang-format、git diff --check、OpenSpec 严格校验 5/5 通过，
主实现 clang-tidy 仅既有 ModeDefinition 枚举宽度提示。日志前缀
/tmp/ec_joint_hardware_three_brake_states_。未运行真实电机；此次未重跑全量测试，
此前两个已知 Fake 集成失败未在本次修改中解决。

## 2026-10-13 控制器切换保留命令缓存

perform_command_mode_switch 删除 ResetCommands，仅在所有模式解析成功后设置模式。
切换不清除命令缓存，不操作抱闸。更新既有切换回归，验证尚未消费的有效命令在切换后
仍可派发；write 周期结束时的命令复位保持原行为。本节替代此前切换清除旧命令的要求。

构建及 21 个切换/抱闸相关测试通过，格式、git diff --check 和 OpenSpec 严格校验
5/5 通过；主实现 clang-tidy 仅既有枚举宽度提示。日志前缀
/tmp/ec_joint_hardware_switch_preserve_。未重跑全量测试，未执行真实电机动作。

## 2026-10-14 恢复过渡状态并增加单关节抱闸函数

恢复 Engaging=3、Releasing=4，运行期按 1→4→2→3→1 转换。新增 EngageBrake(joint)，
配置及退出生命周期复用；配置中新绑定以 Released 为初始请求起点，确保初始抱闸被执行。
ReleaseBrake 在取得有效位置后、发送位置保持之前进入 Releasing。SDK 成功返回后才进入
对应稳定状态；失败保留过渡状态并返回错误，不重复执行，重配可重新初始化。
重复已完成动作跳过 SDK 并 WARN；write 每周期调用时重复松闸警告按 5 秒限频。
本节替代此前三状态及失败后重试策略，控制器切换仍保留命令缓存且不操作抱闸。

新增 SDK 同步回调内重入测试，验证状态 3/4 下两种动作均被拒绝，未增加 SDK 调用次数；
同时验证重复已完成动作、失败/异常状态保留和初始抱闸。本次为同步调用状态保护，
未引入工作线程或跨线程同步机制。

构建成功，18 个相关用例通过。全量 92 个实际用例中 90 通过，仍为两个既有 Fake 集成
失败：耦合关节保持发送顺序、同步模式下生命周期抱闸超时；未放宽相关断言。
colcon 汇总 96 tests、3 failures（含 CTest 重复计数）。格式、git diff --check、OpenSpec
严格校验 5/5 通过。主实现 clang-tidy 仅既有 ModeDefinition 枚举宽度提示。
日志前缀 /tmp/ec_joint_hardware_brake_pending_。未执行真实电机动作。

提交前另包含 ReleaseBrake 的 GetStatus(true, 0) 修改。针对最终代码重新构建并运行
18 个相关用例，全部通过；格式及 OpenSpec 5/5 检查通过。日志前缀
/tmp/ec_joint_hardware_commit_pending_。两个既有集成问题仍未解决。

## 2026-10-14 已松闸时静默跳过

删除 ReleaseBrake 中已松闸分支的 WARN_THROTTLE 和限频时钟，Unsupported/Released
直接返回成功。本节替代此前重复松闸警告要求。构建和 2 个重复动作/状态循环回归通过，
日志不再出现 already released。格式、diff 和 OpenSpec 5/5 检查通过；主实现静态检查
仅既有枚举宽度提示。日志前缀 /tmp/ec_joint_hardware_silent_release_。

## 2026-10-14 高频控制静默跳过过渡状态

ReleaseBrake 从 Engaged 进入后立即设为 Releasing，再读取位置及发送保持/松闸命令。
删除过渡状态松闸警告，直接重复调用静默返回 false。write 在命令校验前跳过状态 3/4
关节，不因过渡状态报错，不派发该关节命令，其他关节继续控制；实际发起操作失败的
当次调用仍返回 ERROR 并保留过渡状态。位置无效也保留 Releasing，重配重新初始化。

构建及 19 个相关测试通过。新增多周期测试确认 3/4 关节不刷日志、不读位置、不重复
动作，其他关节正常派发；读取位置的 Mock 回调验证已经处于 Releasing，重入被拒绝。
格式、diff、OpenSpec 5/5 通过；主实现静态检查仅既有枚举宽度提示。
日志前缀 /tmp/ec_joint_hardware_pending_skip_。本次未重跑全量测试，未执行实机动作。

## 2026-10-14 抱闸函数同样静默处理重复请求

EngageBrake 的 Unsupported/Engaged 静默返回 true，过渡状态静默返回 false；
Released 先进入 Engaging，SDK 成功后才进入 Engaged。实际 SDK 失败仍记录错误。
删除已抱闸及过渡状态的两个 WARN，保持与 ReleaseBrake 对称。
构建及 3 个重复调用/状态循环/生命周期失败回归通过，测试日志无相关 WARN。
格式、diff、OpenSpec 5/5 通过，主实现静态检查仅既有枚举宽度提示。
日志前缀 /tmp/ec_joint_hardware_silent_engage_。本次未重跑全量测试或执行实机动作。

## 2026-10-14 松闸中重复请求返回 true

ReleaseBrake 在 Releasing 静默返回 true，不改变状态、不重复 SDK 调用。write 仍在
状态 3/4 跳过该关节，返回 true 不会使普通控制在松闸完成前派发；实际操作失败仍返回 false。
构建及 3 个重入/多周期跳过/失败回归通过，格式、diff、OpenSpec 5/5 通过；主实现
静态检查仅既有枚举宽度提示。日志前缀 /tmp/ec_joint_hardware_releasing_true_。
本次未重跑全量测试或执行实机动作。

## 2026-10-14 抱闸函数返回 BrakeState

EngageBrake/ReleaseBrake 改为返回当前 BrakeState。无需动作或尚未完成时原样返回状态，
SDK 成功后返回 Engaged/Released，失败保留并返回 Engaging/Releasing；异常沿用外层处理。
配置要求支持关节返回 Engaged；退出接受 Unsupported/Engaged；write 仅在调用返回
Unsupported/Released 时派发控制命令。write 遇到原已处于过渡状态的关节仍静默跳过。
更新直接调用及 SDK 回调重入断言，验证五种枚举返回值和不重复执行动作。

构建成功；全量 93 个实际用例中 91 通过，仍有两个既有 Fake 集成失败，分别涉及
耦合关节保持发送顺序及同步模式下生命周期抱闸超时。colcon 汇总 97 tests、3 failures
（含 CTest 重复计数）。格式、diff、OpenSpec 5/5 通过，主实现静态检查仅既有枚举宽度
提示。日志前缀 /tmp/ec_joint_hardware_brake_enum_return_。未执行实机动作。

## 2026-10-14 位置无效及松闸返回 false 时回退

ReleaseBrake 在反馈缺失、位置非有限或 Brake(false) 返回 false 时，将 brake_state
回退为 Engaged 并返回该枚举。write 当次仍返回 ERROR，不派发控制命令；随后可重试。
SDK 异常及 EngageBrake 返回 false 的状态处理沿用原逻辑。
回归验证无效反馈恢复后可成功松闸，以及松闸 false 回退后可再次请求并成功。
构建及 19 个相关测试通过；格式、diff、OpenSpec 5/5 通过，主实现静态检查仅既有
枚举宽度提示。日志前缀 /tmp/ec_joint_hardware_release_rollback_。
本次未重跑全量测试，未执行实机动作。

## 2026-10-14 抱闸返回 false 时回退

EngageBrake 在 Brake(true) 返回 false 时回退并返回 Released；退出回调当次仍返回
ERROR，后续可以重试。与松闸 false 回退 Engaged 对称，SDK 异常处理保持原行为。
更新生命周期回归，验证失败后直接调用返回 Released、后续抱闸成功进入 Engaged，
并继续处理其他关节。构建及 19 个相关测试通过；格式、diff、OpenSpec 5/5 通过，
主实现静态检查仅既有枚举宽度提示。日志前缀 /tmp/ec_joint_hardware_engage_rollback_。
本次未重跑全量测试，未执行实机动作。

## 2026-10-14 None 模式自动抱闸

删除支持抱闸且 None 时的整关节跳过。None 分支对 Released 调用 EngageBrake，成功后
不再重复操作；Engaged 静默跳过，过渡状态沿用周期开始跳过。无抱闸关节继续零电流控制。
None 发起抱闸时临时切换直接发送，正常返回后恢复同步模式，避免 Brake 同步等回包时
命令留在 Commit 队列；失败返回 ERROR，并沿用 Released 回退。模式切换回调仍只更新模式。

更新控制器退出回归，验证下一次 write 抱紧且不发保持/普通控制、多周期不重复抱紧。
新增 None 抱闸失败及重试回归，并使用真实 SDK/Fake 验证同步控制后进入 None 可完成
抱闸，三个周期仅收到一次 Brake(true)。未执行真实电机动作。

构建成功；全量 95 个实际用例中 93 通过，两个既有 Fake 集成失败仍涉及耦合关节保持
发送顺序及生命周期退出同步抱闸超时。colcon 汇总 99 tests、3 failures（含 CTest 重复
计数）。格式、diff、OpenSpec 5/5 通过；主实现 clang-tidy 仅既有枚举宽度提示。
日志前缀 /tmp/ec_joint_hardware_none_brake_。

## 2026-10-14 同步切换移入 EngageBrake

None 分支删除 SetAdaptersSyncMode 调用，只调用 EngageBrake 并检查返回状态。
EngageBrake 在进入 Engaging 后切换直接发送，成功进入 Engaged 后恢复同步，与
ReleaseBrake 一致；无需动作时不切换模式。配置、None 及生命周期退出统一复用。
Fake 测试反馈准备显式使用直接发送，因为配置初始抱闸现在会恢复同步模式。

构建成功；全量 95 个实际用例中 94 通过。原 FakeCommitFailureReturnsErrorAndLifecycleEngagesBrake
现已通过，生命周期同步抱闸超时断言亦通过。仅 RealSdkCoupledPairCachesBrakesAndPreservesPerMotorCanfd
仍因耦合关节保持发送顺序失败，未放宽断言。colcon 汇总 99 tests、2 failures（含
CTest 重复计数）。格式、diff、OpenSpec 5/5 通过，主实现 clang-tidy 仅既有枚举宽度
提示。日志前缀 /tmp/ec_joint_hardware_engage_sync_。未执行真实电机动作。
