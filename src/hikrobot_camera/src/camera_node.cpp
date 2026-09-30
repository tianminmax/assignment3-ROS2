#include "hikrobot_camera/camera_node.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "rcl_interfaces/msg/parameter_descriptor.hpp"

namespace hikrobot_camera
{
namespace
{

rcl_interfaces::msg::ParameterDescriptor describe(const std::string & text)
{
  rcl_interfaces::msg::ParameterDescriptor descriptor;
  descriptor.description = text;
  return descriptor;
}

/// Parameters that map onto a camera feature. They are pushed to the device
/// whenever it is opened and every time the parameter is changed at runtime.
const char * const kCameraFeatureParameters[] = {
  "pixel_format",
  "exposure_auto",
  "exposure_time_us",
  "gain_auto",
  "gain_db",
  "acquisition_frame_rate",
};

}  // namespace

CameraNode::CameraNode(const rclcpp::NodeOptions & options)
: Node("hikrobot_camera", options)
{
  declareParameters();
  refreshCachedParameters();
  image_topic_ = get_parameter("image_topic").as_string();

  rclcpp::QoS qos(rclcpp::KeepLast(static_cast<std::size_t>(queue_size_)));
  if (qos_reliability_ == "reliable") {
    qos.reliable();
  } else {
    qos.best_effort();
  }
  publisher_ = create_publisher<sensor_msgs::msg::Image>(image_topic_, qos);

  MvsCamera::initializeSdk();

  {
    // Snapshot first, then lock: see the note on connect().
    const std::vector<rclcpp::Parameter> features = currentCameraParameters();
    std::lock_guard<std::mutex> lock(device_mutex_);
    std::string reason;
    if (connect(features, &reason)) {
      RCLCPP_INFO(get_logger(), "camera connected, streaming started");
    } else {
      RCLCPP_ERROR(
        get_logger(),
        "initial camera connection failed: %s "
        "(the acquisition thread keeps retrying)", reason.c_str());
    }
  }

  RCLCPP_INFO(
    get_logger(), "publishing sensor_msgs/Image on '%s' with frame_id='%s'",
    image_topic_.c_str(), frame_id_.c_str());

  running_ = true;
  acquisition_thread_ = std::thread(&CameraNode::acquisitionLoop, this);

  parameter_callback_ = add_on_set_parameters_callback(
    std::bind(&CameraNode::onParameterChange, this, std::placeholders::_1));
}

CameraNode::~CameraNode()
{
  running_ = false;
  if (acquisition_thread_.joinable()) {
    acquisition_thread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(device_mutex_);
    camera_.close();
  }
  MvsCamera::finalizeSdk();
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

void CameraNode::declareParameters()
{
  declare_parameter<std::string>(
    "serial_number", "", describe(
      "Serial number of the camera to open. Empty means 'the only one found'."));

  declare_parameter<std::string>(
    "ip_address", "", describe(
      "IP address of the camera to open (GigE only). Empty means 'any'."));

  declare_parameter<std::string>(
    "image_topic", "/image_raw", describe(
      "Topic for the published sensor_msgs/Image messages. Fixed at startup."));

  declare_parameter<std::string>(
    "frame_id", "camera_optical_frame", describe(
      "Value written into header.frame_id. Used by RViz2 and by TF consumers."));

  declare_parameter<std::string>(
    "timestamp_source", "host", describe(
      "'host': use the timestamp reported by the SDK. 'now': stamp at publish time."));

  declare_parameter<std::string>(
    "qos_reliability", "best_effort", describe(
      "'best_effort' (default, lowest latency) or 'reliable'."));

  declare_parameter<int64_t>(
    "queue_size", 5, describe(
      "QoS depth of the image publisher."));

  declare_parameter<int64_t>(
    "grab_timeout_ms", 1000, describe(
      "Timeout of a single frame grab; also the retry period once the stream is dead."));

  declare_parameter<int64_t>(
    "reconnect_after_failures", 3, describe(
      "Consecutive failed grabs before the device is closed and reconnected."));

  declare_parameter<int64_t>(
    "reconnect_interval_ms", 1000, describe(
      "Delay between two reconnection attempts."));

  declare_parameter<std::string>(
    "pixel_format", "", describe(
      "Requested camera pixel format, e.g. BayerRG8, Mono8, RGB8. Empty leaves the "
      "camera untouched. Frames are published as mono8 or bgr8."));

  declare_parameter<std::string>(
    "exposure_auto", "", describe(
      "ExposureAuto mode: '' (leave unchanged), Off, Once or Continuous."));

  declare_parameter<double>(
    "exposure_time_us", -1.0, describe(
      "Manual exposure time in microseconds. Negative leaves the camera untouched; "
      "setting it also forces ExposureAuto=Off."));

  declare_parameter<std::string>(
    "gain_auto", "", describe(
      "GainAuto mode: '' (leave unchanged), Off, Once or Continuous."));

  declare_parameter<double>(
    "gain_db", -1.0, describe(
      "Manual gain in dB. Negative leaves the camera untouched; setting it also "
      "forces GainAuto=Off."));

  declare_parameter<double>(
    "acquisition_frame_rate", -1.0, describe(
      "Requested acquisition frame rate in fps (turns AcquisitionFrameRateEnable on). "
      "Negative leaves the camera untouched, which usually means the maximum rate."));
}

void CameraNode::refreshCachedParameters()
{
  std::lock_guard<std::mutex> lock(config_mutex_);
  serial_number_ = get_parameter("serial_number").as_string();
  ip_address_ = get_parameter("ip_address").as_string();
  frame_id_ = get_parameter("frame_id").as_string();
  timestamp_source_ = get_parameter("timestamp_source").as_string();
  qos_reliability_ = get_parameter("qos_reliability").as_string();
  queue_size_ = std::max<int64_t>(1, get_parameter("queue_size").as_int());
  grab_timeout_ms_.store(std::max<int64_t>(1, get_parameter("grab_timeout_ms").as_int()));
  reconnect_after_failures_.store(
    std::max<int64_t>(1, get_parameter("reconnect_after_failures").as_int()));
  reconnect_interval_ms_.store(
    std::max<int64_t>(1, get_parameter("reconnect_interval_ms").as_int()));
}

std::vector<rclcpp::Parameter> CameraNode::currentCameraParameters() const
{
  std::vector<rclcpp::Parameter> features;
  const std::size_t count = sizeof(kCameraFeatureParameters) / sizeof(kCameraFeatureParameters[0]);
  features.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::string name = kCameraFeatureParameters[i];
    if (has_parameter(name)) {
      features.push_back(get_parameter(name));
    }
  }
  return features;
}

bool CameraNode::isCameraFeatureParameter(const std::string & name)
{
  const std::size_t count = sizeof(kCameraFeatureParameters) / sizeof(kCameraFeatureParameters[0]);
  for (std::size_t i = 0; i < count; ++i) {
    if (name == kCameraFeatureParameters[i]) {
      return true;
    }
  }
  return false;
}

bool CameraNode::applyCameraParameter(
  const std::string & name, const rclcpp::Parameter & parameter, std::string * reason)
{
  if (name == "pixel_format") {
    const std::string value = parameter.as_string();
    if (value.empty()) {
      return true;
    }

    // Most cameras refuse a new PixelFormat while acquisition is running (the
    // SDK answers MV_E_GC_ACCESS), so stop the stream, reconfigure and restart.
    const bool was_grabbing = camera_.isGrabbing();
    if (was_grabbing) {
      std::string ignored;
      camera_.stopGrabbing(&ignored);
    }

    std::string set_error;
    const bool switched = camera_.setPixelFormat(value, &set_error);

    if (was_grabbing) {
      std::string restart_error;
      if (!camera_.startGrabbing(&restart_error)) {
        RCLCPP_ERROR(
          get_logger(), "could not restart the stream after changing the pixel format: %s",
          restart_error.c_str());
      }
    }

    if (!switched) {
      *reason = set_error;
      return false;
    }

    RCLCPP_INFO(get_logger(), "pixel format switched to %s", value.c_str());
    return true;
  }

  if (name == "exposure_auto") {
    const std::string value = parameter.as_string();
    if (value.empty()) {
      return true;
    }
    return camera_.setEnumByString("ExposureAuto", value, reason);
  }

  if (name == "gain_auto") {
    const std::string value = parameter.as_string();
    if (value.empty()) {
      return true;
    }
    return camera_.setEnumByString("GainAuto", value, reason);
  }

  if (name == "exposure_time_us") {
    const double value = parameter.as_double();
    if (value < 0.0) {
      return true;  // Sentinel: keep whatever the camera currently uses.
    }

    float min_value = 0.0F;
    float max_value = 0.0F;
    if (!camera_.getFloat("ExposureTime", nullptr, &min_value, &max_value, reason)) {
      return false;
    }
    if (value < static_cast<double>(min_value) || value > static_cast<double>(max_value)) {
      std::ostringstream stream;
      stream << "exposure_time_us=" << value << " is outside the supported range ["
             << min_value << ", " << max_value << "] us";
      *reason = stream.str();
      return false;
    }

    // The camera ignores ExposureTime while auto exposure is active.
    if (!camera_.setEnumByString("ExposureAuto", "Off", reason)) {
      return false;
    }
    return camera_.setFloat("ExposureTime", static_cast<float>(value), reason);
  }

  if (name == "gain_db") {
    const double value = parameter.as_double();
    if (value < 0.0) {
      return true;
    }

    float min_value = 0.0F;
    float max_value = 0.0F;
    if (!camera_.getFloat("Gain", nullptr, &min_value, &max_value, reason)) {
      return false;
    }
    if (value < static_cast<double>(min_value) || value > static_cast<double>(max_value)) {
      std::ostringstream stream;
      stream << "gain_db=" << value << " is outside the supported range [" << min_value
             << ", " << max_value << "] dB";
      *reason = stream.str();
      return false;
    }

    if (!camera_.setEnumByString("GainAuto", "Off", reason)) {
      return false;
    }
    return camera_.setFloat("Gain", static_cast<float>(value), reason);
  }

  if (name == "acquisition_frame_rate") {
    const double value = parameter.as_double();
    if (value <= 0.0) {
      return true;
    }

    float min_value = 0.0F;
    float max_value = 0.0F;
    if (!camera_.getFloat("AcquisitionFrameRate", nullptr, &min_value, &max_value, reason)) {
      return false;
    }
    if (value < static_cast<double>(min_value) || value > static_cast<double>(max_value)) {
      std::ostringstream stream;
      stream << "acquisition_frame_rate=" << value << " is outside the supported range ["
             << min_value << ", " << max_value << "] fps";
      *reason = stream.str();
      return false;
    }

    // The node is read-only until frame rate control is explicitly enabled.
    if (!camera_.setBool("AcquisitionFrameRateEnable", true, reason)) {
      return false;
    }
    return camera_.setFloat("AcquisitionFrameRate", static_cast<float>(value), reason);
  }

  return true;
}

rcl_interfaces::msg::SetParametersResult CameraNode::onParameterChange(
  const std::vector<rclcpp::Parameter> & parameters)
{
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;
  result.reason = "ok";

  for (const rclcpp::Parameter & parameter : parameters) {
    const std::string & name = parameter.get_name();

    if (name == "image_topic") {
      RCLCPP_WARN(
        get_logger(),
        "'image_topic' is fixed at startup; restart the node to publish on '%s'",
        parameter.as_string().c_str());
      continue;
    }

    if (!isCameraFeatureParameter(name)) {
      continue;
    }

    std::lock_guard<std::mutex> lock(device_mutex_);
    if (!camera_.isOpen()) {
      // The new value is picked up as soon as the camera is reconnected.
      continue;
    }

    std::string reason;
    if (!applyCameraParameter(name, parameter, &reason)) {
      result.successful = false;
      result.reason = reason;
      return result;
    }
  }

  refreshCachedParameters();
  return result;
}

// ---------------------------------------------------------------------------
// Device life cycle
// ---------------------------------------------------------------------------

bool CameraNode::connect(const std::vector<rclcpp::Parameter> & features, std::string * reason)
{
  std::string serial;
  std::string ip;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    serial = serial_number_;
    ip = ip_address_;
  }

  if (!camera_.open(serial, ip, reason)) {
    return false;
  }

  for (const rclcpp::Parameter & feature : features) {
    std::string error;
    if (!applyCameraParameter(feature.get_name(), feature, &error)) {
      // A camera that does not expose one of the features should still stream,
      // so this is reported and not treated as a fatal error.
      RCLCPP_WARN(
        get_logger(), "could not apply parameter '%s': %s", feature.get_name().c_str(),
        error.c_str());
    }
  }

  std::string format;
  std::string format_error;
  if (camera_.getPixelFormat(&format, &format_error)) {
    RCLCPP_INFO(get_logger(), "active pixel format: %s", format.c_str());
  }

  if (!camera_.startGrabbing(reason)) {
    camera_.close();
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Acquisition
// ---------------------------------------------------------------------------

void CameraNode::acquisitionLoop()
{
  int consecutive_failures = 0;
  uint64_t published = 0;
  auto statistics_start = std::chrono::steady_clock::now();
  const auto statistics_period = std::chrono::seconds(5);

  while (running_ && rclcpp::ok()) {
    // Read the parameters before taking the device lock (lock order: the
    // parameter callback holds the parameter mutex and wants the device mutex,
    // so this thread must never do it the other way round).
    const std::vector<rclcpp::Parameter> features = currentCameraParameters();

    Frame frame;
    std::string grab_error;
    std::string connect_error;
    GrabResult result = GrabResult::kError;
    bool connection_failed = false;

    {
      std::lock_guard<std::mutex> lock(device_mutex_);
      if (!camera_.isOpen()) {
        if (!connect(features, &connect_error)) {
          connection_failed = true;
        }
      }
      if (!connection_failed && camera_.isOpen()) {
        result = camera_.grabFrame(
          &frame, static_cast<unsigned int>(grab_timeout_ms_.load()), &grab_error);
        if (result == GrabResult::kOk) {
          publishFrame(frame);
        }
      }
    }

    if (connection_failed) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000, "camera connection failed: %s", connect_error.c_str());
      std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_interval_ms_.load()));
      continue;
    }

    if (result == GrabResult::kOk) {
      consecutive_failures = 0;
      ++published;
      const auto now_time = std::chrono::steady_clock::now();
      const auto elapsed = now_time - statistics_start;
      if (elapsed >= statistics_period) {
        const double seconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
        RCLCPP_INFO(
          get_logger(), "published %lu frames in %.1f s (%.2f fps, actual)",
          static_cast<unsigned long>(published), seconds,
          static_cast<double>(published) / seconds);
        published = 0;
        statistics_start = now_time;
      }
      continue;
    }

    if (result == GrabResult::kDropped) {
      RCLCPP_DEBUG(get_logger(), "dropped an incomplete frame: %s", grab_error.c_str());
      continue;
    }

    ++consecutive_failures;
    if (consecutive_failures == 1) {
      RCLCPP_WARN(get_logger(), "frame grab failed: %s", grab_error.c_str());
    }
    if (consecutive_failures >= reconnect_after_failures_.load()) {
      RCLCPP_ERROR(
        get_logger(), "%d consecutive grab failures (%s); closing the device to reconnect",
        consecutive_failures, grab_error.c_str());
      std::lock_guard<std::mutex> lock(device_mutex_);
      camera_.close();
      consecutive_failures = 0;
    }
  }
}

rclcpp::Time CameraNode::stampFor(const Frame & frame) const
{
  std::string source;
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    source = timestamp_source_;
  }
  if (source == "host" && frame.host_timestamp_ms > 0) {
    return rclcpp::Time(static_cast<int64_t>(frame.host_timestamp_ms) * 1000000LL);
  }
  return now();
}

bool CameraNode::publishFrame(const Frame & frame)
{
  if (frame.data == nullptr || frame.width == 0 || frame.height == 0) {
    return false;
  }

  sensor_msgs::msg::Image message;
  message.header.stamp = stampFor(frame);
  {
    std::lock_guard<std::mutex> lock(config_mutex_);
    message.header.frame_id = frame_id_;
  }
  message.height = frame.height;
  message.width = frame.width;
  message.is_bigendian = false;

  if (frame.pixel_type == PixelType_Gvsp_Mono8) {
    message.encoding = "mono8";
    message.step = frame.width;
    message.data.assign(frame.data, frame.data + frame.size);
  } else if (frame.pixel_type == PixelType_Gvsp_BGR8_Packed) {
    message.encoding = "bgr8";
    message.step = frame.width * 3U;
    message.data.assign(frame.data, frame.data + frame.size);
  } else {
    // Bayer / RGB / YUV sources are turned into bgr8 by the SDK's ISP, which is
    // faster than doing it in OpenCV and handles every format the camera offers.
    std::string error;
    if (!camera_.convertFrame(frame, PixelType_Gvsp_BGR8_Packed, &error)) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 2000, "pixel conversion failed: %s", error.c_str());
      return false;
    }
    const unsigned char * data = camera_.convertedData();
    message.encoding = "bgr8";
    message.step = frame.width * 3U;
    message.data.assign(data, data + camera_.convertedSize());
  }

  publisher_->publish(message);
  return true;
}

}  // namespace hikrobot_camera
