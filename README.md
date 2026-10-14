# Hunter130 Deploy

Hunter130 人形机器人的 ROS2 Jazzy 部署工作空间 `hunter130_deploy`，
软件包统一使用 `ec_` 前缀。

## 软件包

| 软件包 | 说明 |
|--------|------|
| [ec_description](src/ec_description/) | Hunter130 URDF、网格和 ros2_control 配置 |
| [ec_joint_hardware](src/ec_joint_hardware/) | 关节电机硬件插件 |
| [ec_imu_hardware](src/ec_imu_hardware/) | IMU 硬件插件 |
| [ec_radio](src/ec_radio/) | IBUS 遥控器消息和串口节点 |
| [ec_controller](src/ec_controller/) | 阻尼、站立、强化学习行走控制器及切换节点 |

原 `phanerox_description`、`phanerox_controller`、`phanerox_radio` 分别更名为
`ec_description`、`ec_controller`、`ec_radio`。控制器插件名使用
`ec/DefaultController`、`ec/StandController`、`ec/WalkController`，C++ 命名空间为 `ec`。

## 环境和依赖

- Ubuntu 24.04、ROS2 Jazzy
- Encos `encos_driver` 和 `joint_sdk` C++ SDK
- 内置 ONNX Runtime 1.28.1（amd64 / x86_64）

以下命令在 Bash 终端中执行。安装 ROS2 Jazzy 后，安装构建和运行依赖：

```bash
source /opt/ros/jazzy/setup.bash
sudo apt update
sudo apt install -y \
  build-essential cmake python3-colcon-common-extensions \
  libeigen3-dev nlohmann-json3-dev wget \
  ros-jazzy-ros2-control ros-jazzy-ros2-controllers \
  ros-jazzy-ament-index-cpp ros-jazzy-ament-index-python \
  ros-jazzy-asio-cmake-module ros-jazzy-serial-driver \
  ros-jazzy-robot-state-publisher \
  ros-jazzy-ros2launch ros-jazzy-xacro \
  ros-jazzy-rosidl-default-generators ros-jazzy-rosidl-default-runtime \
  ros-jazzy-ament-lint-auto ros-jazzy-ament-lint-common
```

SDK 使用官方
[encos_driver v3.3.2](https://github.com/EncosTech/encos_driver/releases/tag/v3.3.2)
和 [joint_sdk v1.4.5](https://github.com/EncosTech/joint_sdk/releases/tag/v1.4.5) DEB 包：

```bash
sdk_arch=$(dpkg --print-architecture)
mkdir -p /tmp/hunter130-sdk
wget -P /tmp/hunter130-sdk \
  "https://github.com/EncosTech/encos_driver/releases/download/v3.3.2/libencosdriver_3.3.2_${sdk_arch}_noble.deb" \
  "https://github.com/EncosTech/joint_sdk/releases/download/v1.4.5/jointsdk_1.4.5_${sdk_arch}_noble.deb"
sudo apt install -y \
  "/tmp/hunter130-sdk/libencosdriver_3.3.2_${sdk_arch}_noble.deb" \
  "/tmp/hunter130-sdk/jointsdk_1.4.5_${sdk_arch}_noble.deb"
sudo ldconfig
```

ARM64 部署需要另外替换 `src/ec_controller/thirdparty/onnxruntime/` 中的头文件和
共享库为对应架构版本。

## 编译

进入本工作空间目录后执行：

```bash
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install
source install/setup.bash
```

若 CMake 选中了 Anaconda Python 并提示缺少 `catkin_pkg`，可显式指定系统 Python：

```bash
colcon build --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
```

如果已有旧包构建结果，先移走旧的 `build/`、`install/`、`log/`，并使用未加载旧
工作空间的新终端重新编译，避免原 `phanerox_*` 包残留。

## 启动

启动控制器管理器、状态发布器和控制器切换节点：

```bash
ros2 launch ec_controller real.launch.py
```

在另一个已加载工作空间环境的终端启动遥控器：

```bash
ros2 run ec_radio radio_node --ros-args \
  -p serial_port:=/dev/ttyUSB0 -p baud_rate:=115200
```

| 模式 | 控制器 | 行为 |
|------|--------|------|
| 1 | `default_controller` | 零比例增益，阻尼增益为 0.5 |
| 2 | `stand_controller` | 保持配置中的站立姿态 |
| 3 | `walk_controller` | ONNX 策略行走 |

切换顺序为阻尼 → 站立 → 行走 → 阻尼，也允许站立 → 阻尼。
CH0 控制偏航角速度、CH2 控制前后速度、CH3 控制左右速度，1450–1550 为死区。
CH4 和 CH5 分别选择站立和行走；无按键时选择阻尼。CH6、CH7 为预留模式，
当前切换节点将其映射到阻尼模式。

手动切换示例：

```bash
ros2 service call /controller_manager/switch_controller \
  controller_manager_msgs/srv/SwitchController \
  "{activate_controllers: ['stand_controller'], deactivate_controllers: ['default_controller'], strictness: 2}"
```

## 模型和硬件配置

Hunter130 模型位于 `src/ec_description/urdf/encos130.xacro`，
包含本体描述并引入 `ros2_control.xacro` 中的硬件接口配置。
模型引用的 26 个 STL 文件位于 `src/ec_description/meshes/`，
其中双腕关节、惯性及手部网格来自本次训练资产。原有关节继续使用现有描述，
双腕网格使用 `package://ec_description/meshes/...` 路径。

```text
hunter130_deploy/
├── src/
│   ├── ec_description/
│   │   ├── meshes/
│   │   └── urdf/
│   │       ├── encos130.xacro      # Hunter130 本体模型和硬件接口入口
│   │       └── ros2_control.xacro  # 关节和 IMU 配置
│   ├── ec_joint_hardware/
│   │   └── config/
│   │       ├── control.json       # SDK 电机、总线、标定和保护参数
│   │       └── control.wrist.template.json  # 已合并的双腕硬件配置参考
│   ├── ec_imu_hardware/
│   ├── ec_radio/
│   └── ec_controller/
│       ├── config/
│       │   ├── controllers.yaml
│       │   └── policy/
│       ├── include/ec_controller/
│       ├── launch/real.launch.py
│       └── src/
└── README.md
```

模型和三个控制器均配置为 25 个关节：左右腿各 6 个、腰部 3 个、左右臂各 5 个（含腕部 roll）。
顺序为左腿、右腿、腰部、左臂、右臂，与所选训练策略一致。站立和行走使用训练保存的
默认姿态及完整精度 PD 增益；默认控制器维持零 Kp、Kd=0.5 的阻尼模式。
耦合关节按名称选择 HostKpMotorKd 模式，避免增加腕关节后发生索引错位。

双腕硬件映射已合并到 `src/ec_joint_hardware/config/control.json`，
分别位于对应肘关节之后。JSON、URDF、ros2_control 和三个控制器的 25 关节顺序一致。

| 关节 | 电机型号 | busId | motorId | reverse |
|---|---|---|---|---|
| `left_wrist_roll_joint` | `EC_A5020_P2` | 3 | 20 | false |
| `right_wrist_roll_joint` | `EC_A5020_P2` | 4 | 25 | false |

`control.wrist.template.json` 保留这两项已填写配置作为参考，实际加载 `control.json`。
现有 23 项关节的电机方向、总线、并联踝/腰标定和硬件保护阈值保持原配置。
双腕限位为 ±2.7925268 rad，`maxSpd=36.34`、`maxTor=30.0`。

`ros2_control.xacro` 中的 `adapter_type_0`、`interface_name_0` 配置关节通信，
IMU 的 `adapter_type`、`interface_name`、`bus_idx` 等参数配置传感器通信。
当前沿用 EtherCAT 和网卡名 `enp86s0`，需与目标机器一致。

## 策略

默认使用 `config/policy/hunter130_amp_model_50000.onnx`，由
`/home/milkyway/mjlab/AMP_mjlab/logs/rsl_rl/hunter130_amp_locomotion/2026-10-13_13-54-20/export/Hunter130-AMP-Flat_model_50000.onnx`
复制而来，观测归一化已包含在 ONNX 中。模型 SHA256：

```text
624586e44eb62e3360932c55bb23c81175e7dab8def0a4d18121773de50291b1
```

同目录的 `hunter130_amp_model_50000.io.json` 保存输入输出约定、关节顺序、
完整精度默认姿态、Kp/Kd、action scale 及来源信息，数值已与该 run 的
`params/env.yaml` 核对。`policy_joints` 指定策略顺序，控制器按关节名建立双向映射；
`walk_pos`、`walk_kp`、`walk_kd` 按控制器 `joints` 顺序，`action_scale` 按 `policy_joints` 顺序。

- 输入 `obs`：float32，实机使用 `[1, 336]`；输出 `actions`：float32，`[1, 25]`。
- 每帧 84 维：角速度 `[0:3]`、单位投影重力 `[3:6]`、速度指令 `[6:9]`、
  关节相对默认位置 `[9:34]`、关节速度 `[34:59]`、上一策略周期原始动作 `[59:84]`。
- 历史为 4 帧，按最旧到最新展开；每次激活清零上一动作，并以第一帧实测观测填满历史。
- 策略频率 50 Hz、控制频率 1000 Hz，策略周期之间保持目标；激活后的首个更新周期立即推理。
- 目标位置为 `q_default + action_scale * action`，使用训练的逐关节缩放，观测和动作不额外裁剪。
- 控制器启动时检查模型输入输出维度、数据类型及关节集合；检查失败时拒绝配置。

IMU 输入应为 pelvis 坐标系（x 前、y 左、z 上）的 rad/s 和姿态，直立时投影重力约为
`[0, 0, -1]`。现有 IMU 插件已做单位和安装坐标转换，控制器只归一化四元数并计算投影重力；
需核对当前实物 IMU 安装位置与该转换是否对应 pelvis。

## 行走日志

WalkController 每次激活创建一个 `walk_log_N.csv`，停用时关闭并刷新文件。
`controllers.yaml` 中的 `walk_controller.ros__parameters.log_dir` 配置为
`/home/milkyway/log/hunter130`；空值时控制器也使用该目录。Joint SDK 的
`ros2_control.xacro` 硬件参数 `log_path` 同样配置为 `/home/milkyway/log/hunter130`。

目录不存在时会自动创建，也可提前创建：

```bash
mkdir -p /home/milkyway/log/hunter130
```

WalkController 的 CSV 和 Joint SDK 的 `*_command.csv.zstd`、`*_status.csv.zstd`
保存在该机器人目录下。

每个控制周期记录一行，当前为 1000 Hz，列按以下顺序排列：

| 列 | 内容 |
|---|---|
| `time_ns` | 当前控制周期的 ROS 时间戳，单位 ns |
| `mpos0...` | 关节反馈位置，rad |
| `mvel0...` | 关节反馈速度，rad/s |
| `meff0...` | 关节反馈力矩，N·m |
| `cpos0...` | 控制器生成的位置命令，rad |
| `ckp0...` | 当前周期命令 Kp |
| `ckd0...` | 当前周期命令 Kd |

列编号对应该控制器 `joints` 的 YAML 顺序，关节数量自动适配配置。
记录的是控制器生成的命令；SDK 限幅后的电机侧数据由 `ec_joint_hardware` 的 `log_path` 独立记录。

本次修改不包含编译或实机运行验证；源码改动需重新编译后生效。
