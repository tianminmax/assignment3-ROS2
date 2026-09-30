# RoboMaster assignment3 ROS2 — HIKROBOT 相机 ROS 2 封装

基于海康机器人 MVS SDK 的 ROS 2 Humble 相机驱动。支持发现并按序列号/IP 选相机、发布 `sensor_msgs/msg/Image`、在线控制曝光/增益/帧率/像素格式、断线自动重连，并在退出时释放全部 SDK 资源。

## 功能概览

- 设备发现与选择：支持 GigE / USB3 / GenTL，可按序列号或 IP 指定目标相机
- 图像发布：Bayer/单色/RGB 自动转为 `bgr8` 或 `mono8`，话题可配置，RViz2 可直接显示
- 参数控制：曝光、增益、帧率、像素格式均可在线设置，先校验范围再写设备，失败返回明确原因
- 断线重连：连续取帧失败后自动重连并恢复原有参数
- 资源管理：`MvsCamera` 以 RAII 管理句柄，进程级 SDK 初始化用引用计数保护
- 帧率：默认追求最高实际帧率，日志里区分"设置帧率"与"实际帧率"

## 仓库结构

```text
assignment3-ROS2/                       # 同时作为 colcon 工作空间
├── README.md
├── docs/
│   ├── ROS2Tutorial.md
│   └── assignment.md                   # 作业要求
└── src/hikrobot_camera/
    ├── package.xml
    ├── CMakeLists.txt
    ├── include/hikrobot_camera/
    │   ├── mvs_camera.hpp              # SDK 封装（不依赖 ROS）
    │   └── camera_node.hpp             # 节点声明
    ├── src/
    │   ├── main.cpp
    │   ├── mvs_camera.cpp              # 枚举/开关设备/取流/参数读写/像素转换
    │   └── camera_node.cpp             # 取流线程、发布、参数回调、重连
    ├── cmake/FindMVS.cmake             # 查找厂商 SDK
    ├── launch/camera.launch.py
    └── config/
        ├── camera.yaml                 # 参数配置
        └── camera.rviz                 # 预配置好的 RViz2 布局
```

## 快速开始

### 1. 安装依赖

- Ubuntu 22.04 + ROS 2 Humble（`ros2`、`colcon`、`rosdep` 可用）
- 海康机器人 MVS SDK（厂商依赖，不能用 rosdep 安装）
  1. 到 [海康机器人下载中心](https://www.hikrobotics.com/cn/machinevision/service/download/?module=0) 下载 Linux 版 MVS SDK 并安装（默认装到 `/opt/MVS`）。
  2. 确认环境变量已导出：`echo $MVCAM_SDK_PATH` 应输出 `/opt/MVS`。为空时执行并写入 shell 配置：
     ```bash
     source /opt/MVS/bin/set_env_path.sh
     ```
  3. 可先用官方客户端确认相机能被识别、能出图：
     ```bash
     /opt/MVS/bin/MVS.sh
     ```

ROS 依赖已写进 `package.xml`（`rclcpp`、`sensor_msgs`、`launch`、`launch_ros`、`ament_index_python`），用 rosdep 安装即可。

### 2. 编译

本仓库本身就是工作空间，不需要再放进别的 `src` 目录。

```bash
cd /assignment3-ROS2
source /opt/ros/humble/setup.bash

rosdep install --from-paths src --ignore-src -r -y --rosdistro humble

colcon build --symlink-install --packages-select hikrobot_camera
source install/setup.bash
```

Zsh 用户把 `setup.bash` 换成 `setup.zsh`。

`cmake/FindMVS.cmake` 按以下顺序找 SDK：`-DMVS_ROOT_DIR=...` → 环境变量 `MVCAM_SDK_PATH` → `/opt/MVS`。SDK 装在别处时：

```bash
colcon build --symlink-install --packages-select hikrobot_camera --cmake-args -DMVS_ROOT_DIR=/your/MVS
```

### 3. 运行

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch hikrobot_camera camera.launch.py
```

指定自己的参数文件：

```bash
ros2 launch hikrobot_camera camera.launch.py params_file:=/absolute/path/to/camera.yaml
```

启动后应看到类似输出：

```text
[camera_node-1] [INFO] [hikrobot_camera]: camera connected, streaming started
[camera_node-1] [INFO] [hikrobot_camera]: active pixel format: BayerRG8
[camera_node-1] [INFO] [hikrobot_camera]: publishing sensor_msgs/Image on '/image_raw' with frame_id='camera_optical_frame'
[camera_node-1] [INFO] [hikrobot_camera]: published 148 frames in 5.0 s (29.62 fps, actual)
```

最后一行是实际发布帧率，和参数里的 `acquisition_frame_rate` 对照，就能区分"设置帧率"和"实际接收帧率"。

### 4. 在 RViz2 查看

直接用仓库自带的配置（话题和 QoS 都已配好）：

```bash
ros2 launch hikrobot_camera camera.launch.py rviz:=true
```

或单独启动：

```bash
rviz2 -d $(ros2 pkg prefix hikrobot_camera)/share/hikrobot_camera/config/camera.rviz
```

若自己在空白 RViz2 里添加：`Add` → `Image` → `Image Topic` 选 `/image_raw`，并把该显示的 `Reliability Policy` 改成 `Best effort`。相机图像按传感器数据惯例用 `best_effort` 发布，而 RViz2 的 Image 显示默认是 `Reliable`，不匹配时会"看得到话题但画面全黑"。仓库里的 `camera.rviz` 已把这一项设好。

### 5. 命令行查看/设置参数

```bash
ros2 param list /hikrobot_camera
ros2 param get /hikrobot_camera exposure_time_us
ros2 param describe /hikrobot_camera acquisition_frame_rate
ros2 param set /hikrobot_camera exposure_time_us 20000.0
ros2 param set /hikrobot_camera gain_db 10.0
ros2 param set /hikrobot_camera acquisition_frame_rate 15.0
```

> `exposure_time_us`、`gain_db`、`acquisition_frame_rate` 是 double 类型，命令行要写成小数（`20000.0` 而不是 `20000`），否则会报 `Wrong parameter type ...`。

设置成功时画面亮度/帧率会立刻变化；越界或相机不支持时会报错并给出原因，例如：

```text
Setting parameter failed: gain_db=20 is outside the supported range [0, 16.9807] dB
```

## 参数说明

参数文件：[`config/camera.yaml`](src/hikrobot_camera/config/camera.yaml)。负数/空字符串表示"不改动相机"，因此默认启动不会覆盖你在 MVS 客户端里的设置。

| 参数                       | 类型   | 默认值                 | 含义                                                               |
| -------------------------- | ------ | ---------------------- | ------------------------------------------------------------------ |
| `serial_number`            | string | `""`                   | 按序列号选相机；空 = 使用唯一找到的设备                            |
| `ip_address`               | string | `""`                   | 按 IP 选相机（GigE）；空 = 不限                                    |
| `image_topic`              | string | `/image_raw`           | 图像话题，启动后不可改                                             |
| `frame_id`                 | string | `camera_optical_frame` | 写入 `header.frame_id`                                             |
| `timestamp_source`         | string | `host`                 | `host` 用 SDK 时间戳，`now` 用发布时刻                             |
| `qos_reliability`          | string | `best_effort`          | 图像 QoS：`best_effort` 或 `reliable`                              |
| `queue_size`               | int    | `5`                    | QoS 队列深度                                                       |
| `grab_timeout_ms`          | int    | `1000`                 | 单次取帧超时；也是断流后的重试周期                                 |
| `reconnect_after_failures` | int    | `3`                    | 连续失败多少次后关闭设备并重连                                     |
| `reconnect_interval_ms`    | int    | `1000`                 | 两次重连尝试之间的间隔                                             |
| `pixel_format`             | string | `BayerRG8`             | 请求的相机像素格式，如 `BayerRG8`/`Mono8`/`RGB8`；切换时会短暂停流 |
| `exposure_auto`            | string | `""`                   | `ExposureAuto`：`""`/`Off`/`Once`/`Continuous`                     |
| `exposure_time_us`         | double | `-1.0`                 | 手动曝光时间，单位微秒；设值时会先关闭自动曝光                     |
| `gain_auto`                | string | `""`                   | `GainAuto`：`""`/`Off`/`Once`/`Continuous`                         |
| `gain_db`                  | float  | `-1.0`                 | 手动增益，单位 dB；设值时会先关闭自动增益                          |
| `acquisition_frame_rate`   | double | `-1.0`                 | 采集帧率（fps），会自动打开 `AcquisitionFrameRateEnable`           |

像素格式与图像编码：相机原始输出按 `pixel_format` 设置，`Mono8` 直接发布 `mono8`，`BGR8` 直接发布 `bgr8`，其余（Bayer/RGB/YUV）由 SDK 的 ISP 转成 `bgr8` 后发布。这样既能用 Bayer 拿高帧率，又能保证 RViz2 直接显示。

## 功能与实现要点

- 设备发现与选择：`MvsCamera::enumerateDevices()` 枚举设备，`pickDevice()` 按序列号或 IP 匹配；设备不存在、多个匹配、被占用等情况都返回可读原因。
- 取流与发布：独立取流线程用 `MV_CC_GetImageBuffer`/`MV_CC_FreeImageBuffer` 取帧（比 `MV_CC_GetOneFrameTimeout` 少一次拷贝），用 `MV_GrabStrategy_LatestImagesOnly` 避免消费慢时延迟堆积。
- 参数控制：`add_on_set_parameters_callback` 在参数被设置时同步写设备，先校验范围再写，失败原因回填到 `SetParametersResult.reason`。
- 断线重连：连续取帧失败达阈值后关闭设备，重新枚举并按原序列号/IP 打开，再重新应用全部有效参数。
- 线程与锁：设备句柄由取流线程与参数回调共享，统一用 `device_mutex_` 串行化；取参数时先取锁外快照，避免锁序反转。
- 资源释放：`MvsCamera` 为 RAII，析构时停止取流、关闭设备、销毁句柄；进程级初始化用引用计数保证只调用一次。

## 验证与实测

### 验收清单

- [x] `colcon build` 通过，无错误无警告
- [x] launch 后日志出现 `camera connected, streaming started`，`ros2 topic list` 有 `/image_raw`
- [x] `ros2 topic hz /image_raw` 频率稳定，`ros2 topic echo /image_raw --field header` 时间戳在更新
- [x] RViz2 中 `/image_raw` 画面稳定正常
- [x] `ros2 param set` 曝光/增益/帧率/像素格式后成像实际变化，越界值被拒绝并给出原因
- [x] 断线重连：拔掉 USB/网线再插回，日志出现 `closing the device to reconnect` 后重新 `camera connected`，图像恢复
- [x] Ctrl+C 退出无报错，无残留进程

### 本机实测基准

以下数据在本机（Ubuntu 22.04 + ROS 2 Humble + MVS 4.8.2.1）用真实相机 `MV-CS016-10UC`（USB3，序列号 `DB0178696`，1440×1080 BayerRG8）测得，可作为验收对照：

| 测试项                                | 结果                                                                                       |
| ------------------------------------- | ------------------------------------------------------------------------------------------ |
| 消息内容                              | `encoding=bgr8`、`width=1440`、`height=1080`、`step=4320`、`len(data)=4665600=height*step` |
| 时间戳                                | 与系统时间差约 0.01 s（`timestamp_source: host`）                                          |
| 默认实际帧率                          | 约 165 fps                                                                                 |
| `acquisition_frame_rate=30.0`         | 实测 30.00 fps，与设置值一致                                                               |
| `exposure_time_us` 200 / 2000 / 20000 | 平均亮度 0.07 / 2.57 / 32.76，单调变化                                                     |
| `gain_db=20.0`                        | 被拒绝并提示 `outside the supported range [0, 16.9807] dB`                                 |
| `pixel_format` BayerRG8 ↔ Mono8       | 均切换成功，编码分别 `bgr8`/`mono8`、`step` 4320/1440                                      |
| `pixel_format=NotAFormat`             | 被拒绝并提示 `0x80000004 (invalid parameter)`                                              |
| 断线重连                              | 通过，拔插后自动恢复出图                                                                   |

## 故障排查

| 现象                                                                    | 处理                                                                                                                  |
| ----------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| 编译报 `Could not find MVS`                                             | SDK 未装或路径未导出；用 `-DMVS_ROOT_DIR=` 指定                                                                       |
| 运行报 `libMvCameraControl.so: cannot open shared object file`          | 执行 `source /opt/MVS/bin/set_env_path.sh` 并重新 `source install/setup.zsh`                                          |
| `no camera found`                                                       | 相机未上电/未接线；或网卡与相机不在同一网段（用 MVS 客户端的 IP 配置工具改）                                          |
| `access denied, the camera may be occupied by another process`          | MVS 客户端或另一个节点已占用，先关掉                                                                                  |
| `camera selection is ambiguous`                                         | 同时接多台相机，请设 `serial_number`                                                                                  |
| RViz2 画面花屏/错位                                                     | 像素格式与编码不匹配；确认 `pixel_format` 受支持，并看日志的 `active pixel format`                                    |
| 实际帧率远低于设置值                                                    | 曝光过长、带宽不足、Bayer 转换开销；先看日志里的 actual fps                                                           |
| 别的节点/RViz2 收不到图，提示 `requesting incompatible QoS`             | 订阅端用了 `reliable`，而图像默认 `best_effort`；改订阅端为 `best_effort`，或把节点 `qos_reliability` 改成 `reliable` |
| `ros2 param get/set` 长时间不返回，或 `ros2 node list` 出现两个同名节点 | 同一 `ROS_DOMAIN_ID` 网络里有别的机器也起了 `/hikrobot_camera`；用 `ROS_DOMAIN_ID=xx` 隔离或加 namespace              |
| 参数设置报 `rejected: 0x...`                                            | 该相机不支持此节点；用 MVS 客户端的 Node 列表确认可用功能                                                             |

## 已知限制

- 未发布 `camera_info` 内参（导航组如需内参，可后续加 `camera_info_manager`）。
- 未实现硬件触发/软触发，当前固定为连续自由采集。
- 参数只覆盖题目要求的曝光、增益、帧率、像素格式，其余功能需在 MVS 客户端设置。
- 仅在 MVS SDK 4.8.x 上编译验证；其他版本若接口有变动需相应调整。

## 提交

将源代码、Launch 和参数配置推送到你的 Fork，然后提交仓库链接到 **2719850558@qq.com**，格式：`第三次作业-班级-姓名`（例如 `第三次作业-自动化2305-周湛昊`）。
