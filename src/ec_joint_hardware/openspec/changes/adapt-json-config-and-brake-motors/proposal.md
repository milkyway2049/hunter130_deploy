## Why

JointSDK 1.4.5 支持适配器 JSON 扩展配置、逐电机 CAN FD 设置和机械抱闸接口。
当前硬件插件只保留适配器的 `type/name`，会丢失 `extend` 中的总线参数；配置阶段的
全局 CAN FD 调用又会覆盖 JSON 中逐电机的选择。插件尚未接入机械抱闸生命周期，
带抱闸关节在首次有效控制命令到达时释放；控制器切换回调不操作抱闸，进入 None 后在 write 中抱紧。

本变更需要完整接入新版配置，并实现首次有效命令触发松闸、生命周期退出时抱紧的时序。
抱闸能力与关节一起缓存，每次重建关节只主动查询一次。

## What Changes

- 保留完整适配器 JSON，使用 `CreateAdapter(adapter_json)` 传递 `extend` 的公共
  CAN 参数和 `buses` 覆盖配置；具体字段解释交给 SDK/驱动。
- 保留编号占位符替换；使用固定适配器类型和名称的 JSON 不再必须传入编号参数。
- **BREAKING**：取消 `canfd_enable` 的全局覆盖。逐电机 `canfd` 由 JSON 决定，
  缺省沿用 SDK 的 `false`；硬件不再读取或告警旧 hardware 参数。
- 在 `JointRuntime` 中与关节名称、SDK 句柄一起保存 `brake_state` 枚举（0 无能力、1 已抱闸、2 已松闸、3 抱闸中、4 松闸中），完成全部创建和
  配对后每个关节查询一次，后续硬件逻辑直接读缓存。
- 配置阶段在每个关节能力查询后，立即对该支持关节执行一次初始抱闸；失败则配置返回错误。
- 激活只检查反馈，不释放抱闸，不发送位置保持命令。
- **BREAKING**：控制器切换只更新模式、保留命令缓存，不操作抱闸；移除实例首帧命令门。
  各关节独立派发完整有效命令，首次需要松闸时先调用当前位置保持，再松闸执行。
  未认领的抱闸关节在 write 中抱紧，不发送控制命令。
- 与关节一起记录本轮是否成功松闸，避免每周期重复操作；双电机协调交由 SDK。
  停用、错误和关闭路径继续负责尝试抱紧。
- 保持插件身份、现有八个命令接口和六个状态接口；不新增抱闸 ROS 服务或控制接口。
- 使用已安装的 JointSDK 1.4.5，不新增 SDK 临时安装、路径锁定或额外版本保护。

## Capabilities

### New Capabilities

None. 抱闸与 JSON 适配属于已有 `ec-joint-hardware` 能力域。

### Modified Capabilities

- `ec-joint-hardware`：完整适配器 JSON、可选编号参数、逐电机 CAN FD、关节能力缓存、
  抱闸生命周期、首帧松闸与控制器交接。

## Impact

- 实施分支为 `ec_joint_hardware` 的 `feat/hds/add_brakc_motor`，基于 `develop`。
- 本变更同步维护 proposal、design、增量 spec、tasks 和实现；不归档历史 change，
  不修改历史主规格。
- 后续实现仅涉及该硬件子仓库及其测试、README；SDK、motor_driver、robot_description、
  robot_bringup 和外部整机配置文件均不在修改范围内。
- JSON 格式参考 `/home/encos/Desktop/work/EC-R130-V2_description/control_v2.json`；
  源码接口参考 `/home/encos/Desktop/work/joint_sdk`，构建直接使用系统已安装的 1.4.5。
- 旧配置中缺少 `canfd` 的电机将使用普通 CAN；原有 `canfd_enable=true` 不再开启
  CAN FD。部署迁移需在对应 JSON 显式设置，不能依赖旧 Xacro/YAML 参数。
- 本变更替代 `add-canfd-enable-parameter` 的全局参数语义，放宽
  `dynamic-joint-adapter-template` 的必填条件，并移除
  `add-controller-switch-first-command-gate` 的实例等待机制。
- 实机抱闸依赖驱动型号标记及固件支持。规划时检查的驱动源码型号表全部为
  `brake=false`；插件不会自行猜测或强制覆盖该能力。Mock/Fake 验证不等同于实机验收。
