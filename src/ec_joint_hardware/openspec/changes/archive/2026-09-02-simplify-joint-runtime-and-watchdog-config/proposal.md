## Why

控制模式匹配、关节状态维护和接口集合处理分散在硬件插件实现中，存在重复遍历、指针式模式定义和职责边界不清的问题。与此同时，固定为 3 个周期的反馈看门狗无法适配不同 controller update rate，需要在保持默认行为的前提下允许按硬件实例配置。

## What Changes

- 将控制模式定义改为类型安全的 `ModeDefinition` 枚举，并以 `std::map<ModeDefinition, std::vector<std::string>>` 维护模式到命令接口集合的唯一映射。
- 引入独立 `JointRuntime` 类封装模式解析、命令复位和状态存储，保持 `std::vector` 的 HardwareInfo/URDF 关节顺序。
- 删除 `ComputeCandidateInterfaces` 和 `MatchMode`，用 `JointRuntime::ResolveMode()` 直接解析完整或增量认领集合；切换执行保持先全部解析、再统一写入的原子语义。
- 将 `SplitInterfaceKey`、`SameInterfaces`、`ModeName` 和 `MotorErrorName` 等无状态辅助函数统一移入 `utils`；接口集合比较拒绝重复项并忽略顺序。
- 将命令/状态接口名称常量改为 `std::vector<std::string>`，删除冗余数量常量和模式接口别名。
- 为包内所有头文件的公开及内部声明补齐中文 Doxygen 注释，并补充模式解析、重复接口和无副作用预检测试。
- 新增可选 hardware parameter `status_max_life_cycle`；省略时默认值为 `3`，显式值必须是 `int` 范围内的正整数，并传给 `DefaultJointManager::SetAdaptersStatusMaxLifeCycle()`。
- 更新 README，说明可选反馈看门狗参数、默认值和非法配置行为。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `ec-joint-hardware`: 规定内部模式解析和单关节运行时的确定性约束、头文件文档完整性，以及反馈看门狗生命周期的可选硬件参数语义。

## Impact

影响 `EcJointHardware`、新拆分的 `JointRuntime`、类型与工具函数头源文件、硬件单元测试、README 和主 OpenSpec 规范。ros2_control 导出的 8 个命令接口、6 个状态接口、控制模式认领协议及 SDK 派发行为不变；安装该包后直接依赖旧内部 C++ 类型或常量的源码需要适配新 API。
