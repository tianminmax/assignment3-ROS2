#ifndef HIKROBOT_CAMERA__CAMERA_NODE_HPP_
#define HIKROBOT_CAMERA__CAMERA_NODE_HPP_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hikrobot_camera/mvs_camera.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace hikrobot_camera
{

/**
 * ROS 2 node that publishes a HIKROBOT camera as sensor_msgs/msg/Image.
 *
 * Responsibilities:
 *   - select and connect a camera (by serial number or IP address),
 *   - pull frames on a dedicated thread and publish them,
 *   - expose exposure / gain / frame rate / pixel format as live parameters,
 *   - reconnect automatically after the stream dies,
 *   - release every SDK resource on shutdown.
 *
 * The camera handle is shared between the acquisition thread and the parameter
 * callback, so every SDK access is serialised through `device_mutex_`.
 */
class CameraNode : public rclcpp::Node
{
public:
  explicit CameraNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~CameraNode() override;

private:
  // --- setup ---------------------------------------------------------------
  void declareParameters();
  void refreshCachedParameters();

  // --- device life cycle ---------------------------------------------------
  /**
   * Open the device, push `features` to it and start streaming.
   * `device_mutex_` must be held. The feature list is passed in as an argument
   * so that this function never calls the parameter API while holding the
   * device lock, which would risk a lock-order inversion with the parameter
   * callback.
   */
  bool connect(const std::vector<rclcpp::Parameter> & features, std::string * reason);

  /// Snapshot the camera feature parameters. Must be called *without* holding
  /// `device_mutex_`.
  std::vector<rclcpp::Parameter> currentCameraParameters() const;

  // --- acquisition ---------------------------------------------------------
  void acquisitionLoop();
  bool publishFrame(const Frame & frame);
  rclcpp::Time stampFor(const Frame & frame) const;

  // --- parameters ----------------------------------------------------------
  rcl_interfaces::msg::SetParametersResult onParameterChange(
    const std::vector<rclcpp::Parameter> & parameters);
  /// Apply one camera feature parameter. `device_mutex_` must be held and the
  /// device must be open.
  bool applyCameraParameter(
    const std::string & name, const rclcpp::Parameter & parameter, std::string * reason);
  static bool isCameraFeatureParameter(const std::string & name);

  // --- state ---------------------------------------------------------------
  MvsCamera camera_;
  mutable std::mutex device_mutex_;

  std::atomic<bool> running_{false};
  std::thread acquisition_thread_;

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
  rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr parameter_callback_;

  // Cached parameters. `image_topic_` is fixed at startup, everything else is
  // refreshed whenever the parameters change.
  std::string image_topic_{"/image_raw"};
  std::string serial_number_;
  std::string ip_address_;
  std::string frame_id_{"camera_optical_frame"};
  std::string timestamp_source_{"host"};
  std::string qos_reliability_{"best_effort"};
  int64_t queue_size_{5};
  int64_t grab_timeout_ms_{1000};
  int64_t reconnect_after_failures_{3};
  int64_t reconnect_interval_ms_{1000};
};

}  // namespace hikrobot_camera

#endif  // HIKROBOT_CAMERA__CAMERA_NODE_HPP_
