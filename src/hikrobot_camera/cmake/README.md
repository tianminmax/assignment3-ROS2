# SDK 构建集成

`FindMVS.cmake` 用于查找海康机器人 MVS SDK 的头文件和库，已接入主 `CMakeLists.txt`（`find_package(MVS REQUIRED)`）。

查找顺序：`-DMVS_ROOT_DIR=...` → 环境变量 `MVCAM_SDK_PATH` → `/opt/MVS` → `/usr/local/MVS`。
