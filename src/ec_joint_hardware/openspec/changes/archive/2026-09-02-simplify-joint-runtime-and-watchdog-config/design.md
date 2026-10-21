## Context

当前实现已经把控制模式绑定到 controller 认领的命令接口集合，但模式定义同时承担枚举标识和接口集合存储，切换路径又通过 `ComputeCandidateInterfaces`、`MatchMode` 等文件内辅助函数重复构造候选集合。命令复位和状态写入也由硬件类代替单关节运行时完成。反馈看门狗则在实现文件中固定为 3 个控制周期，无法按设备组或 update rate 调整。

本次变更跨越类型定义、硬件生命周期、模式切换、工具函数、测试和 API 文档，但不改变 ros2_control 对外导出的接口集合或 SDK 控制协议。

## Goals / Non-Goals

**Goals:**

- 用一个类型安全且可穷举的模式表示驱动解析和 SDK 派发。
- 让单关节相关的解析、命令复位和状态存储由 `JointRuntime` 自身负责。
- 消除模式匹配和接口集合处理的重复实现，并保持切换的无副作用预检与原子执行语义。
- 允许每个硬件实例配置反馈看门狗生命周期，缺省行为仍为 3 个周期。
- 让所有头文件声明具备可由严格 Doxygen 检查的中文说明。

**Non-Goals:**

- 不改变 8 个命令接口、6 个状态接口或六种控制模式的认领集合。
- 不把 `joints_` 改为 map，也不改变 HardwareInfo/URDF 决定的导出顺序。
- 不改变 SDK 派发、单次 `Commit()`、逐关节非法命令处理或反馈读取语义。
- 不引入新的第三方依赖。

## Decisions

### 1. 枚举与单一模式表

`ModeDefinition` 使用 `enum class` 表示 `None`、六种控制模式，并由 `std::map<ModeDefinition, std::vector<std::string>>` 保存唯一接口集合。相比保存名称和接口指针的结构体，枚举可直接用于 `switch`，模式表则成为完整集合解析的单一数据源；`None -> {}` 使空认领与非法认领分别表示为有效枚举和 `std::nullopt`。

### 2. 独立 `JointRuntime`，容器仍为 vector

`JointRuntime` 封装 joint 名称、SDK 指针、当前模式、命令/状态槽，以及 `ResolveMode`、`ResetCommands`、`StoreStatus`。硬件插件仍以 `std::vector<JointRuntime>` 按 HardwareInfo 顺序保存关节，避免 map 改变接口导出顺序或增加两套顺序来源。

### 3. 直接解析完整与增量集合

完整集合重载通过无序精确比较匹配模式；增量重载只处理属于自身 joint 的全局键，从当前模式集合开始，先移除、再添加并去重，最后复用完整集合解析。`prepare` 只解析不写入，`perform` 先解析所有关节，全部成功后再统一赋值，因此无需 `ComputeCandidateInterfaces` 和 `MatchMode` 仍能保持切换原子性。

### 4. 无状态辅助函数集中到 utils

接口键拆分、集合比较、模式名称和电机错误名称不拥有硬件状态，统一作为 `utils` 的包级函数。`SameInterfaces` 在长度相等的前提下使用 `std::is_permutation`，重复项的多重性不会被集合化掩盖。

### 5. 看门狗参数在 on_init 严格解析

hardware parameter 名为 `status_max_life_cycle`。缺失时成员保持默认值 3；存在时用 `std::from_chars` 要求整个字符串是 `int` 范围内的正整数。解析后的值在每次 `on_configure` 创建 adapter 后传入 `SetAdaptersStatusMaxLifeCycle`。相比宽松的 `stoi`，完整消费检查可明确拒绝 `3.5`、尾随字符和溢出。

### 6. Doxygen 与测试作为接口维护约束

所有头文件的类型、函数、参数、返回值和成员补充中文 Doxygen。单元测试直接覆盖默认/自定义/非法看门狗参数、所有模式、空集合与非法集合区分、重复接口、按 joint 过滤增量键，以及 `prepare` 不修改当前模式。

## Risks / Trade-offs

- [内部 C++ API 形态变化可能影响包外源码] → 在提案中标明源码兼容性影响；ros2_control 插件 ABI/接口协议不变，依赖方按新枚举和模式表迁移。
- [过小的看门狗值会提高反馈抖动敏感度] → 只接受正整数，在 README 说明其单位为控制周期并保留默认值 3。
- [过大的看门狗值会延迟掉线发现] → 配置由部署方显式负责，默认行为不变。
- [模式集合比较遗漏重复项] → 保留序列长度并用 `std::is_permutation` 比较多重性，新增重复项回归测试。

## Migration Plan

1. 部署方无需修改现有硬件声明，缺省看门狗仍为 3 个周期。
2. 需要按 update rate 调整的实例添加 `<param name="status_max_life_cycle">N</param>`，其中 `N > 0`。
3. 直接使用旧内部模式结构或常量的 C++ 调用方改用 `ModeDefinition`、`kModeDefinitions` 和 `JointRuntime` API。
4. 回滚时移除可选参数并恢复旧内部实现；对外 ros2_control 配置无需迁移。

## Open Questions

无。
