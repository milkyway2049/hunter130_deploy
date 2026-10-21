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

新复制硬件包的追加修改、IMU 坐标转换及验证结果见
[硬件包修改说明](docs/hardware_changes.md)。

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

新复制的硬件包直接使用新版 `device/imu/imu.h`、`device/motor/types.h` 路径，
已在 `encos_driver 4.0.0` + `joint_sdk 1.5.3` 环境中通过完整构建。
上方旧版 SDK 安装示例属于原工程配置，当前硬件包未验证旧版 SDK 兼容性。

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
| 1 | `default_controller` | 零比例增益，阻尼增益为 2.5 |
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
机器人专用的电机、总线、标定和保护参数位于 `src/ec_description/config/control.json`，
由 `ec_description` 安装并通过 `config_file` 传给关节硬件插件。
所有电机参数均显式设置 `canfd: true`，包含耦合关节的 `motorParams1` 和 `motorParams2`。
模型引用的 24 个 STL 文件位于 `src/ec_description/meshes/`，
网格路径使用相对于模型文件的 `../meshes/...`。

```text
hunter130_deploy/
├── src/
│   ├── ec_description/
│   │   ├── config/control.json    # SDK 电机、总线、标定和保护参数
│   │   ├── meshes/
│   │   └── urdf/
│   │       ├── encos130.xacro      # Hunter130 本体模型和硬件接口入口
│   │       └── ros2_control.xacro  # 关节和 IMU 配置
│   ├── ec_joint_hardware/
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

模型共 23 个受控关节：左右腿各 6 个、腰部 3 个、左右臂各 4 个。
`controllers.yaml`、`ros2_control.xacro` 和 `control.json` 按上述顺序对应。

本次整理保留原 `ec_deploy` 的姿态、PD 增益、动作缩放和策略文件。
原站立/行走配置的肘关节目标为 1.0，而训练资产默认值为 -0.5；使用新导出的
Hunter130 策略时，应同时核对默认姿态和观测/动作定义。
URDF 中的速度、力矩限值用于模型描述，SDK 的 `maxSpd`、`maxTor` 保留原值，
没有自动提高硬件保护阈值。实机部署前需核对 `control.json` 中的电机型号、
电机方向、总线编号、并联踝/腰机构标定和保护参数。

`ros2_control.xacro` 中的 `adapter_type_0`、`interface_name_0` 配置关节通信，
IMU 的 `adapter_type`、`interface_name`、`bus_idx` 等参数配置传感器通信。
当前沿用 EtherCAT 和网卡名 `enp86s0`，需与目标机器一致。

IMU 的安装坐标转换由 `ec_imu_hardware` 完成，输出机身坐标系（x 前、y 左、z 上）。
角速度和加速度按 `(x, -z, y)` 映射；姿态四元数右乘绕 x 轴 -90° 的安装旋转后归一化，
角速度单位为 rad/s。该转换固定对应当前 Hunter130 的 IMU 安装方式，控制器直接使用转换后的数据。

## 策略

策略路径在 `controllers.yaml` 中配置，默认 `config/policy/policy.onnx`，
相对路径从 `ec_controller` 的安装目录解析。
现有策略文件已保留，尚未确认其训练来源；更换模型不代表策略已重新训练或适配。

- 每帧观测 78 维，历史 10 帧，ONNX 输入共 780 维。
- 输出 23 维动作，推理频率 50 Hz，控制更新频率 1000 Hz。
- 控制器将 YAML 关节顺序转换为策略使用的左右交错顺序。
