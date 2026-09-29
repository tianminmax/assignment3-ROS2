# RoboMaster assignment3 ROS2 — HIKROBOT 相机 ROS 2 封装

在 Ubuntu 22.04 / ROS 2 Humble 上，基于海康机器人 MVS SDK 实现的相机功能包：发现并选择设备、采集发布 `sensor_msgs/msg/Image`、通过 ROS 参数在线控制曝光/增益/帧率/像素格式、断线自动重连，并在退出时释放全部 SDK 资源。

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
    └── config/camera.yaml
```

## 一、环境与依赖

- Ubuntu 22.04 + ROS 2 Humble（`ros2`、`colcon`、`rosdep` 可用）
- **海康机器人 MVS SDK**（厂商依赖，不能用 rosdep 安装）
  1. 到 [海康机器人下载中心](https://www.hikrobotics.com/cn/machinevision/service/download/?module=0) 下载 Linux 版 MVS SDK 并安装（默认装到 `/opt/MVS`）。
  2. 安装脚本会把 `MVCAM_SDK_PATH=/opt/MVS`、`MVCAM_COMMON_RUNENV=/opt/MVS/lib` 和 `LD_LIBRARY_PATH` 写入 shell 环境。**如果 `echo $MVCAM_SDK_PATH` 为空**，先手动执行：
     ```bash
     source /opt/MVS/bin/set_env_path.sh
     ```
     并把它追加到 `~/.zshrc` 或 `~/.bashrc` 里，否则程序运行时会找不到 `libMvCameraControl.so`。
  3. 可先用官方客户端 `MVS.sh` 确认相机能被识别、能出图：
     ```bash
     /opt/MVS/bin/MVS.sh
     ```

ROS 依赖已经在 `package.xml` 里声明（`rclcpp`、`sensor_msgs`、`launch`、`launch_ros`、`ament_index_python`），用 rosdep 安装即可。

## 二、编译

本仓库本身就是工作空间，**不需要**再放进别的 `src` 目录。

```bash
cd /home/tomliu/Programs/assignment3-ROS2
source /opt/ros/humble/setup.bash

rosdep install --from-paths src --ignore-src -r -y --rosdistro humble

colcon build --symlink-install --packages-select hikrobot_camera
source install/setup.bash
```

Zsh 用户把 `setup.bash` 换成 `setup.zsh`。

`cmake/FindMVS.cmake` 按下面的顺序找 SDK：`-DMVS_ROOT_DIR=...` → 环境变量 `MVCAM_SDK_PATH` → `/opt/MVS`。SDK 装在别处时：

```bash
colcon build --symlink-install --packages-select hikrobot_camera --cmake-args -DMVS_ROOT_DIR=/your/MVS
```

## 三、运行

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch hikrobot_camera camera.launch.py
```

换个参数文件：

```bash
ros2 launch hikrobot_camera camera.launch.py params_file:=/absolute/path/to/camera.yaml
```

启动后应该看到类似输出：

```text
[camera_node-1] [INFO] [hikrobot_camera]: camera connected, streaming started
[camera_node-1] [INFO] [hikrobot_camera]: active pixel format: BayerRG8
[camera_node-1] [INFO] [hikrobot_camera]: publishing sensor_msgs/Image on '/image_raw' with frame_id='camera_optical_frame'
[camera_node-1] [INFO] [hikrobot_camera]: published 148 frames in 5.0 s (29.62 fps, actual)
```

最后一行是**实际发布帧率**，可以和参数里设置的 `acquisition_frame_rate` 对照，用来区分"设置帧率"和"实际接收帧率"。

### 在 RViz2 中查看

```bash
rviz2
```

左侧 `Displays` → `Add` → `Image` → `Image Topic` 选 `/image_raw`。图像应稳定刷新，`Status` 为 `OK`。（如果 RViz2 里不显示，先确认话题有数据：`ros2 topic hz /image_raw`。）

### 命令行查看/设置参数

```bash
ros2 param list /hikrobot_camera
ros2 param get /hikrobot_camera exposure_time_us
ros2 param describe /hikrobot_camera acquisition_frame_rate
ros2 param set /hikrobot_camera exposure_time_us 20000.0
ros2 param set /hikrobot_camera gain_db 10.0
ros2 param set /hikrobot_camera acquisition_frame_rate 15.0
```

> 注意：`exposure_time_us`、`gain_db`、`acquisition_frame_rate` 是 double 类型，
> 命令行必须写成小数（`20000.0` 而不是 `20000`），否则会报
> `Wrong parameter type ... is of type {double}, setting it to {integer} is not allowed`。

设置成功时画面亮度/帧率会立刻变化；越界或相机不支持时，`ros2 param set` 会报错并给出原因，例如：

```text
Setting parameter failed: exposure_time_us=5000000 is outside the supported range [15, 1000000] us
```

## 四、参数说明

参数文件：[`config/camera.yaml`](src/hikrobot_camera/config/camera.yaml)。负数/空字符串表示"不改动相机"，因此默认启动不会覆盖你在 MVS 客户端里的设置。

| 参数 | 类型 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `serial_number` | string | `""` | 按序列号选相机；空 = 使用唯一找到的设备 |
| `ip_address` | string | `""` | 按 IP 选相机（GigE）；空 = 不限 |
| `image_topic` | string | `/image_raw` | 图像话题，启动后不可改 |
| `frame_id` | string | `camera_optical_frame` | 写入 `header.frame_id` |
| `timestamp_source` | string | `host` | `host` 用 SDK 时间戳，`now` 用发布时刻 |
| `qos_reliability` | string | `best_effort` | 图像 QoS：`best_effort` 或 `reliable` |
| `queue_size` | int | `5` | QoS 队列深度 |
| `grab_timeout_ms` | int | `1000` | 单次取帧超时；也是断流后的重试周期 |
| `reconnect_after_failures` | int | `3` | 连续失败多少次后关闭设备并重连 |
| `reconnect_interval_ms` | int | `1000` | 两次重连尝试之间的间隔 |
| `pixel_format` | string | `BayerRG8` | 请求的相机像素格式，如 `BayerRG8`/`Mono8`/`RGB8` |
| `exposure_auto` | string | `""` | `ExposureAuto`：`""`/`Off`/`Once`/`Continuous` |
| `exposure_time_us` | double | `-1.0` | 手动曝光时间，单位**微秒**；设值时会先关闭自动曝光 |
| `gain_auto` | string | `""` | `GainAuto`：`""`/`Off`/`Once`/`Continuous` |
| `gain_db` | float | `-1.0` | 手动增益，单位 **dB**；设值时会先关闭自动增益 |
| `acquisition_frame_rate` | double | `-1.0` | 采集帧率（fps），会自动打开 `AcquisitionFrameRateEnable` |

关于像素格式与图像编码：相机原始输出按 `pixel_format` 设置，若为 `Mono8` 直接发布 `mono8`；若为 `BGR8` 直接发布 `bgr8`；其它格式（如 Bayer、RGB、YUV）由 SDK 的 ISP 转换成 `bgr8` 后发布，这样既能用 Bayer 拿到更高帧率，又保证 RViz2 能直接显示。

## 五、实现要点

- **设备发现与选择**：`MvsCamera::enumerateDevices()` 枚举 GigE/USB/GenTL 设备；`pickDevice()` 按序列号或 IP 匹配，设备不存在、多个匹配（标识冲突）、被其它进程占用（`MV_E_ACCESS_DENIED`）等情况都会返回可读原因。
- **取流与发布**：独立的取流线程用 `MV_CC_GetImageBuffer`/`MV_CC_FreeImageBuffer` 取帧（比 `MV_CC_GetOneFrameTimeout` 少一次拷贝），采用 `MV_GrabStrategy_LatestImagesOnly`，避免消费慢时延迟堆积。
- **参数控制**：通过 `add_on_set_parameters_callback` 在参数被设置时同步写入相机，设置前校验范围、设置后检查 SDK 返回值，失败时把原因回填到 `SetParametersResult.reason`。
- **断线重连**：连续取帧失败达到阈值后关闭设备，重新枚举并按相同序列号/IP 打开，然后重新应用全部有效参数。
- **线程与锁**：设备句柄由取流线程和参数回调共享，统一用 `device_mutex_` 串行化；取参数时先取锁外快照，避免与参数回调形成锁序反转。
- **资源释放**：`MvsCamera` 是 RAII 类型，析构时停止取流、关闭设备、销毁句柄；进程级 `MV_CC_Initialize/Finalize` 用引用计数保证只调用一次。

## 六、验证步骤

1. **编译**：`colcon build` 无错误、无警告。
2. **连接与发布**：`ros2 launch hikrobot_camera camera.launch.py`，日志出现 `camera connected, streaming started`。
3. **话题**：`ros2 topic list` 有 `/image_raw`；`ros2 topic hz /image_raw` 频率稳定；`ros2 topic echo /image_raw --field header` 时间戳在更新。
4. **RViz2**：`Image` 显示插件选 `/image_raw`，画面稳定正常。
5. **参数生效**：`ros2 param set /hikrobot_camera exposure_time_us <值>` 后画面亮度明显变化；设置越界值被拒绝并给出原因。
6. **断线重连**：采集过程中拔掉网线/断电，等待日志出现 `closing the device to reconnect`，恢复连接后日志重新出现 `camera connected, streaming started`，图像恢复。
7. **帧率对比**：设置 `acquisition_frame_rate` 为某个值，观察日志里的 actual fps；两者差异说明瓶颈在传输或消费端。
8. **退出**：Ctrl+C 退出后无报错，`ps` 里没有残留进程。

## 七、故障排查

| 现象 | 可能原因与处理 |
| --- | --- |
| 编译报 `Could not find MVS` | SDK 未安装或路径未导出，看第一节，或用 `-DMVS_ROOT_DIR=` 指定 |
| 运行报 `libMvCameraControl.so: cannot open shared object file` | 没执行 `source /opt/MVS/bin/set_env_path.sh`；或未重新 `source install/setup.zsh` |
| `no camera found` | 相机未上电/未插网线；网卡 IP 与相机不在同一网段（用 MVS 客户端的 IP 配置工具改） |
| `access denied, the camera may be occupied by another process` | MVS 客户端或另一个节点已占用相机，先关掉 |
| `camera selection is ambiguous` | 同时接多台相机，请设置 `serial_number` |
| RViz2 画面花屏/错位 | 像素格式与编码不匹配；确认 `pixel_format` 是相机支持的格式，并检查日志里的 `active pixel format` |
| 实际帧率远低于设置值 | 曝光时间过长、网卡带宽不足、Bayer 转换开销；先看日志里的 actual fps，再逐项排除 |
| 别的节点/RViz2 收不到图像，提示 `requesting incompatible QoS` | 订阅端用了 `reliable`，而图像默认是 `best_effort`（可靠订阅者收不到 best_effort 的发布者）。把节点参数改成 `qos_reliability: reliable`，或把订阅端改成 `best_effort` |
| 参数设置报 `rejected: 0x...` | 该相机不支持此节点（如 `AcquisitionFrameRateEnable`）；用 MVS 客户端的 Node 列表确认可用功能 |

## 八、已知限制

- 未实现 `camera_info` 标定信息发布（导航组如需内参，可后续加 `camera_info_manager`）。
- 未实现硬件触发/软触发模式，当前固定为连续自由采集。
- 参数只覆盖题目要求的曝光、增益、帧率、像素格式，其它相机功能需在 MVS 客户端设置。
- 仅在本机的 MVS SDK 4.8.x 上编译验证；不同 SDK 版本若接口有变动，需要相应调整。

## 九、提交

将源代码、Launch 和参数配置推送到你的 Fork，然后提交仓库链接到 **2719850558@qq.com**，格式：`第三次作业-班级-姓名`（例如 `第三次作业-自动化2305-周湛昊`）。
