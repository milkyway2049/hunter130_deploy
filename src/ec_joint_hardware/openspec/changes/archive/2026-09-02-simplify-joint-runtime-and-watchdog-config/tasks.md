## 1. 模式与运行时简化

- [x] 1.1 将 `ModeDefinition` 改为枚举并建立模式到接口向量的唯一映射
- [x] 1.2 拆分 `JointRuntime`，实现完整/增量模式解析、命令复位和状态存储方法
- [x] 1.3 删除 `ComputeCandidateInterfaces`、`MatchMode` 和冗余模式接口别名
- [x] 1.4 保持 `joints_` 为按 HardwareInfo 顺序排列的 vector，并使 perform 切换全量解析后原子写入

## 2. 工具与文档规范化

- [x] 2.1 将接口键拆分、集合比较、模式名和电机错误名函数移入 `utils`
- [x] 2.2 将命令/状态接口名常量改为 `std::vector<std::string>` 并删除冗余数量常量
- [x] 2.3 为所有头文件声明补齐中文 Doxygen 注释
- [x] 2.4 更新 README 中的硬件参数和控制器对接说明

## 3. 反馈看门狗参数

- [x] 3.1 新增可选 `status_max_life_cycle` hardware parameter，默认值为 3
- [x] 3.2 严格拒绝非正整数、尾随字符和溢出值，并在 configure 时应用到 SDK manager

## 4. 测试与验证

- [x] 4.1 覆盖模式解析、重复接口、跨关节过滤和 prepare 无副作用语义
- [x] 4.2 覆盖看门狗参数默认值、自定义正整数和非法输入
- [x] 4.3 运行 clang-format、严格 Doxygen、构建、完整测试和 OpenSpec 严格校验
