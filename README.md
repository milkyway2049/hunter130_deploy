# Hunter130 Deploy

Hunter130 人形机器人的 ROS2 Jazzy 部署工作空间 `hunter130_deploy`，
软件包统一使用 `ec_` 前缀。

本仓库仅保留实机部署所需的源码、构建配置、模型和运行资源。
包含 **SuperMimic 模式 4（CH6）** 和 **Quest 全身化身模式 5（CH7）**，与 AMP 行走模式 3 同时可用。
Mimic 的流程为：站立等待 → 过渡到动作起点 → 播放一次 → 平滑回站 → 持续站立。
默认动作是《爱你》完整版 `love_you.csv`（约 62 秒），也可通过启动参数选择
10 秒低强度片段 `dance_gentle.csv`。模式 4 只使用关节和 IMU 反馈，
三维相对位置输入固定为零，保留躯干相对朝向反馈。

## 软件包

| 软件包 | 说明 |
|--------|------|
| [ec_description](src/ec_description/) | Hunter130 URDF、网格和 ros2_control 配置 |
| [ec_joint_hardware](src/ec_joint_hardware/) | 关节电机硬件插件 |
| [ec_imu_hardware](src/ec_imu_hardware/) | IMU 硬件插件 |
| [ec_teleop_msgs](src/ec_teleop_msgs/) | Quest 实时 10 帧参考接口 |
| [ec_radio](src/ec_radio/) | IBUS 遥控器消息和串口节点 |
| [ec_controller](src/ec_controller/) | 阻尼、站立、AMP 行走、Mimic 动作控制器及切换节点 |

## 环境和依赖

- Ubuntu 24.04、ROS2 Jazzy
- Encos `encos_driver` 4.0.0 和 `joint_sdk` 1.5.3 C++ SDK（现有硬件包验证环境）
- 内置 ONNX Runtime 1.28.1（amd64 / x86_64）

以下命令在 Bash 终端中执行。安装 ROS2 Jazzy 后，安装构建和运行依赖：

```bash
source /opt/ros/jazzy/setup.bash
sudo apt update
sudo apt install -y \
  build-essential cmake python3-colcon-common-extensions \
  libeigen3-dev nlohmann-json3-dev \
  ros-jazzy-ros2-control ros-jazzy-ros2-controllers \
  ros-jazzy-ament-index-cpp ros-jazzy-ament-index-python \
  ros-jazzy-asio-cmake-module ros-jazzy-serial-driver \
  ros-jazzy-robot-state-publisher \
  ros-jazzy-ros2launch ros-jazzy-xacro \
  ros-jazzy-rosidl-default-generators ros-jazzy-rosidl-default-runtime
```

安装对应架构的 Encos SDK，确保 CMake 能找到 `encos_driver` 和 `joint_sdk`。
硬件包使用新版 `device/imu/imu.h`、`device/motor/types.h` 路径及 PVT 枚举接口。

ARM64 部署需要另外替换 `src/ec_controller/thirdparty/onnxruntime/` 中的头文件和
共享库为对应架构版本。

## 编译

进入本工作空间目录后执行：

```bash
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --cmake-args -DBUILD_TESTING=OFF
source install/setup.bash
```

若 CMake 选中了 Anaconda Python 并提示缺少 `catkin_pkg`，可显式指定系统 Python：

```bash
colcon build --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
```

如果已有旧包构建结果，先移走旧的 `build/`、`install/`、`log/`，并使用未加载旧
工作空间的新终端重新编译。

## 启动

启动控制器管理器、状态发布器和控制器切换节点：

```bash
ros2 launch ec_controller real.launch.py
```

默认播放《爱你》。选择低强度片段：

```bash
ros2 launch ec_controller real.launch.py \
  motion:="$(ros2 pkg prefix --share ec_controller)/config/motions/dance_gentle.csv"
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
| 4 | `mimic_controller` | SuperMimic 参考动作跟踪，位置项置零，播放前后保持站立 |
| 5 | `quest_controller` | Meta 全身骨架实时重定向，经同一 SuperMimic actor 跟随 |

首次启动按阻尼 → 站立 → AMP 行走进入；舞蹈使用 **CH5 → CH6**，退出使用 **CH6 → CH5**。
CH0 控制偏航角速度、CH2 控制前后速度、CH3 控制左右速度，1450–1550 为死区。
CH4、CH5、CH6、CH7 分别选择站立、AMP 行走、Mimic 和 Quest；无按键时选择阻尼。
从 AMP 行走切到 Mimic 后，默认先站立 1 秒，再用 3 秒过渡到参考起始姿态。
播放完成后用 3 秒回到站立并保持；模式 4 持续有效时不会重复播放。
CH6 → CH5 时先平滑回站，收到完成状态后再切回 AMP 行走；再次选择 CH6 可重播。
Mimic 播放中选择模式 2，同样会先回站再切换站立控制器；模式 1 可直接切换阻尼。

手动切换示例：

```bash
ros2 service call /controller_manager/switch_controller \
  controller_manager_msgs/srv/SwitchController \
  "{activate_controllers: ['stand_controller'], deactivate_controllers: ['default_controller'], strictness: 2}"
```

## 模型和硬件配置

Hunter130 模型位于 `src/ec_description/urdf/hunter130.xacro`，
包含本体描述并引入 `ros2_control.xacro` 中的硬件接口配置。
机器人专用配置位于 `src/ec_description/config/control.json`，由 `ec_description` 安装，
再通过 `config_file` 传给关节硬件插件。全部电机参数均显式设置 `canfd: true`，
包括双腕以及耦合关节的 `motorParams1`、`motorParams2`。
模型引用的 26 个 STL 文件位于 `src/ec_description/meshes/`，
其中双腕关节、惯性及手部网格来自本次训练资产。原有关节继续使用现有描述，
双腕网格使用 `package://ec_description/meshes/...` 路径。

```text
hunter130_deploy/
├── src/
│   ├── ec_description/
│   │   ├── config/control.json    # SDK 电机、总线、标定和保护参数
│   │   ├── meshes/
│   │   └── urdf/
│   │       ├── hunter130.xacro      # Hunter130 本体模型和硬件接口入口
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

模型和三个控制器均配置为 25 个关节：左右腿各 6 个、腰部 3 个、左右臂各 5 个（含腕部 roll）。
顺序为左腿、右腿、腰部、左臂、右臂，与所选训练策略一致。站立和行走使用训练保存的
默认姿态及完整精度 PD 增益；默认控制器维持零 Kp、Kd=0.5 的阻尼模式。
耦合关节按名称选择 HostKpMotorKd 模式，避免增加腕关节后发生索引错位。

双腕硬件映射已合并到 `src/ec_description/config/control.json`，
分别位于对应肘关节之后。JSON、URDF、ros2_control 和三个控制器的 25 关节顺序一致。

| 关节 | 电机型号 | busId | motorId | reverse |
|---|---|---|---|---|
| `left_wrist_roll_joint` | `EC_A5020_P2` | 3 | 20 | false |
| `right_wrist_roll_joint` | `EC_A5020_P2` | 4 | 25 | false |

实际加载 `ec_description` 安装目录下的 `config/control.json`。
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
安装转换由 `ec_imu_hardware` 完成：角速度和加速度按 `(x, -z, y)` 映射，
四元数右乘绕 x 轴 −90° 的旋转后归一化，角速度单位为 rad/s。

## SuperMimic 策略（模式 4）

使用 `SuperMimic-Reproduced/logs/hunter130_4096_10h/ckpt_latest.pt` 的
176000 步 actor，导出为 `config/policy/hunter130_supermimic_176000.onnx`。
同名 JSON 保存关节顺序、默认姿态、PD 增益、动作尺度、限位以及 checkpoint、ONNX
和动作文件的 SHA256。运行时不需要训练项目、PyTorch、世界模型或 critic。

- `policy [1,810]`：10 帧关节相对默认位置、关节速度、pelvis 局部角速度、
  pelvis 局部投影重力和上一原始动作；按数据项拼接，每项内部从最旧到最新。
  每次播放重新填充本体历史、清零动作历史。
- `goal [1,609]`：当前及后续 9 帧各 60 维参考特征，接三维零位置项，
  再接六维躯干相对朝向。60 维参考特征依次为相对默认关节位置、关节速度、
  根部投影重力、根部局部角速度、根部航向坐标系线速度和根部高度。
  参考高度、速度来自动作文件，不是实测位置或速度估计。
- 实际躯干朝向由 pelvis IMU 姿态右乘腰部 `yaw → roll → pitch` 关节旋转得到；
  播放开始时仅对齐一次根部航向，随后保留真实朝向误差。
  六维旋转使用相对旋转矩阵前两列，按行展开。
- ONNX 内已包含训练的观测缩放，部署端不重复归一化。
- `action [1,25]`：50 Hz 推理，目标为 `q_default + action_scale * action`，
  其中逐关节尺度为 `0.25 * effort / stiffness`。目标裁剪到训练物理模型限位，
  策略周期之间保持目标；1000 Hz 硬件控制使用训练 Kp/Kd。
  已移除旧 MimicLite 的动作低通滤波和子步延迟。

ONNX 在后台线程执行，1000 Hz 更新只提交观测、非阻塞读取结果并保持上次目标。
准备完成后等待首个推理结果再开始播放，最后一帧结果返回后才进入回站。
推理等待超过 20 ms、反馈非法或模型推理失败时锁定阻尼状态，需要停用再激活。

动作 CSV 为 64 列：`time`、根部位置 3、根部四元数 wxyz 4、25 个关节位置、
25 个关节速度、根部世界线速度 3、根部世界角速度 3。控制器严格检查列名、关节顺序、
50 Hz 时间戳、有限值、四元数和关节限位；旧 33 列动作需要先转换。
随仓库的两份动作已转换，参考速度由原 50 Hz CSV 差分得到。
导出工具也支持训练 NPZ，可保留其原始参考速度。

模型导出、骨架重定向、Quest 应用和仿真均放在 [../hunter130_quest3](../hunter130_quest3/README.md)。
仅在训练机器上重新导出或转换动作时执行（使用训练项目的 Python 环境）：

```bash
PYTHONDONTWRITEBYTECODE=1 \
  /home/milkyway/SuperMimic-Reproduced/.venv/bin/python ../hunter130_quest3/tools/export_supermimic.py \
  --training-root /home/milkyway/SuperMimic-Reproduced \
  --checkpoint /home/milkyway/SuperMimic-Reproduced/logs/hunter130_4096_10h/ckpt_latest.pt \
  --output src/ec_controller/config/policy/hunter130_supermimic_176000.onnx \
  --motion src/ec_controller/config/motions/dance_gentle.csv \
  --motion src/ec_controller/config/motions/love_you.csv
```

新增训练动作时，`--motion` 可指定 NPZ，配合
`--motion-dir src/ec_controller/config/motions` 生成同名 CSV。

位置项置零仍可能产生水平漂移，不能保证按参考路线移动。此前对照仿真中，
8 段动作运行 60 秒未触发失稳阈值，平均水平位置 RMSE 为 35.2 cm；
该结果基于理想反馈、参考首帧状态初始化，不代表实机或从站立进入动作已获验证。
《爱你》不属于该模型训练的 40 段 LAFAN 动作。已单独用 C++ actor 从参考初态
完成 3099 帧、61.96 秒的物理仿真，未触发高度或倾斜失稳阈值；
该验证不包含从站立进入动作和回站，尚未进行实机验证。

额外的无支撑物理冒烟测试中，训练模型采用现有静态 PD 站姿时，约 1.32 秒
在策略介入前失稳。准备和回站流程仍是静态 PD 插值，不能当作主动平衡控制器。
首次调试需在支撑条件下验证准备、策略接管和回站；当前不具备已验证的无支撑启动流程。

本次本机验证：C++ 与训练端 40 个样本对拍，动作最大绝对差约 `1.91e-6`；
低强度片段由 C++ actor 从参考初态完成 10 秒物理仿真；6 项控制器检查通过，
包含乱序关节映射、准备/回站、停止请求、故障阻尼及后台推理。
本机 2000 次控制更新的 P99 为 0.129 ms（单次 ONNX 推理平均 1.19 ms，运行于后台）。
`ec_controller`、`ec_radio`、`ec_description` 编译及安装检查通过；
硬件包编译受本机缺少 `encos_driver`、`joint_sdk` 限制，尚未完成实机验证。

## Quest 全身化身（模式 5 / CH7）

安装与启动步骤见 [Quest 项目说明](../hunter130_quest3/README.md)，
[已编译 APK](../hunter130_quest3/artifacts/hunter130-quest-debug.apk)。
Quest 原生应用采集 Meta Body Tracking 全身骨架，电脑完成校准、IK、180 ms 实测窗口缓存，
发布 `/quest/reference`。部署端复用 176000 步 SuperMimic 模型，以机器人自身关节和 IMU 闭环控制；位置反馈仍为零。
机器人不依赖训练项目或 Android 构建环境。

先在 Quest 松开握键、站直按 A 校准，再握双侧握键，等待桥接 `ready` 和 `/quest/ready=true`，
从站立/行走切入 CH7。准备阶段保持中立姿态，`/quest/state=tracking` 后缓慢动作。
双握键释放、断流或参考非法时回站并锁定退出，重新握键不会自动跟随；重新切换模式后才可接管。
模式 2/3 等待回站完成，模式 1 直接阻尼。模式 4/5 之间经站立或行走切换。

电脑和机器人需同步时钟，并保持同一 ROS_DOMAIN_ID。接收端校验消息时间、版本、关节名、序号、
四元数和速度/限位，独立 150 ms 看门狗；推理仍在后台线程。Quest 身体跟踪丢失会取消校准。
Meta 腿部是算法推算。人的骨盆位置用于生成参考，不能替代机器人的世界位置传感器，水平漂移仍可能存在。
现有静态 PD 准备/回站仍未通过无支撑验证，首次实机调试需使用支撑。
本机已完成协议/重定向、控制器生命周期、UDP→ROS 联动及短时合成骨架物理仿真；Quest 和实机尚未连接验证。

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

实机运行验证需在目标机器人上完成。
