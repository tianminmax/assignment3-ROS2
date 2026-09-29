cmake_minimum_required(VERSION 3.8)
project(hikrobot_camera)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# The vendor SDK is located before ament so that a missing SDK fails early with
# a readable message instead of a link error.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(MVS REQUIRED)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(camera_node
  src/main.cpp
  src/camera_node.cpp
  src/mvs_camera.cpp
)

target_compile_features(camera_node PRIVATE cxx_std_17)
target_include_directories(camera_node PRIVATE include)

# The vendor headers are not warning free; SYSTEM keeps -Wall/-Wextra/-Wpedantic
# meaningful for our own sources.
target_include_directories(camera_node SYSTEM PRIVATE ${MVS_INCLUDE_DIRS})

ament_target_dependencies(camera_node
  rclcpp
  sensor_msgs
)

# NOTE: ament_target_dependencies uses the plain target_link_libraries()
# signature, so this call has to use it as well - CMake refuses to mix the plain
# and the keyword signature on one target.
target_link_libraries(camera_node ${MVS_LIBRARIES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(camera_node PRIVATE -Wall -Wextra -Wpedantic)
endif()

# libMvCameraControl.so lives outside the default library search path, so bake it
# into the binary. Alternatively export MVCAM_COMMON_RUNENV and LD_LIBRARY_PATH
# from the MVS installer in every shell.
if(MVS_LIBRARY_DIRS)
  set_target_properties(camera_node PROPERTIES
    BUILD_RPATH "${MVS_LIBRARY_DIRS}"
    INSTALL_RPATH "${MVS_LIBRARY_DIRS}"
  )
endif()

install(TARGETS camera_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_package()
