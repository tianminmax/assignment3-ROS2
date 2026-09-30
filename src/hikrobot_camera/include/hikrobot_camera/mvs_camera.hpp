#ifndef HIKROBOT_CAMERA__MVS_CAMERA_HPP_
#define HIKROBOT_CAMERA__MVS_CAMERA_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "MvCameraControl.h"

namespace hikrobot_camera
{

/// One camera reported by the MVS SDK during enumeration.
struct DeviceInfo
{
  unsigned int index{0};              ///< Index inside the enumeration result.
  unsigned int transport_layer{0};    ///< MV_GIGE_DEVICE / MV_USB_DEVICE / ...
  std::string model_name;
  std::string serial_number;
  std::string user_defined_name;
  std::string ip_address;             ///< Empty for non-GigE devices.
  std::string mac_address;

  bool isGigE() const
  {
    return (transport_layer & (MV_GIGE_DEVICE | MV_GENTL_GIGE_DEVICE)) != 0;
  }
};

/// A frame that is still owned by the SDK. Its memory stays valid until the
/// next call to MvsCamera::releaseFrame().
struct Frame
{
  const unsigned char * data{nullptr};
  unsigned int size{0};
  unsigned int width{0};
  unsigned int height{0};
  MvGvspPixelType pixel_type{PixelType_Gvsp_Undefined};
  uint64_t host_timestamp_ms{0};      ///< Host timestamp reported by the SDK, in milliseconds.
  unsigned int frame_number{0};
};

/// Result of a single grab attempt.
enum class GrabResult
{
  kOk,        ///< A valid frame is available.
  kTimeout,   ///< No frame arrived before the timeout expired.
  kDropped,   ///< An incomplete frame arrived (usually packet loss); safe to retry.
  kError      ///< The device reported an error; reconnection is likely required.
};

/**
 * Thin RAII wrapper around the HIKROBOT MVS SDK.
 *
 * The class is intentionally free of ROS dependencies so that it can be tested
 * on its own. It is not thread safe: the caller has to serialise access, which
 * camera_node.cpp does with a mutex around both grabbing and parameter writes.
 */
class MvsCamera
{
public:
  MvsCamera() = default;
  ~MvsCamera();

  MvsCamera(const MvsCamera &) = delete;
  MvsCamera & operator=(const MvsCamera &) = delete;

  /// Process wide SDK initialisation. Safe to call more than once.
  static void initializeSdk();
  static void finalizeSdk();

  /// List every reachable camera. Returns an empty vector on failure.
  static std::vector<DeviceInfo> enumerateDevices(std::string * error);

  /// Translate an MVS return code into a readable message.
  static std::string errorToString(int code);

  /**
   * Open the first camera whose serial number (preferred) or IP address matches.
   * An empty filter is a wildcard; if both filters are empty and several cameras
   * are present the call fails, because the target would be ambiguous.
   */
  bool open(const std::string & serial_number, const std::string & ip_address, std::string * error);
  bool isOpen() const { return handle_ != nullptr; }
  void close();

  bool startGrabbing(std::string * error);
  bool isGrabbing() const { return grabbing_; }
  bool stopGrabbing(std::string * error);

  GrabResult grabFrame(Frame * frame, unsigned int timeout_ms, std::string * error);
  void releaseFrame();

  // --- feature access ------------------------------------------------------
  // `min_value` / `max_value` may be nullptr when the range is not needed.
  bool getFloat(
    const std::string & key, float * value, float * min_value, float * max_value,
    std::string * error) const;
  bool setFloat(const std::string & key, float value, std::string * error);

  bool getInt(const std::string & key, int64_t * value, std::string * error) const;
  bool setInt(const std::string & key, int64_t value, std::string * error);

  bool setBool(const std::string & key, bool value, std::string * error);

  bool getEnum(const std::string & key, unsigned int * value, std::string * error) const;
  bool setEnumByString(const std::string & key, const std::string & value, std::string * error);
  bool getEnumSymbolic(
    const std::string & key, unsigned int value, std::string * symbolic,
    std::string * error) const;

  bool getPixelFormat(std::string * symbolic, std::string * error) const;
  bool setPixelFormat(const std::string & symbolic, std::string * error);

  /**
   * Convert the current frame into `dst_type` (typically PixelType_Gvsp_BGR8_Packed).
   * The result is owned by this object and stays valid until the next conversion.
   */
  bool convertFrame(const Frame & frame, MvGvspPixelType dst_type, std::string * error);
  const unsigned char * convertedData() const { return convert_buffer_.data(); }
  unsigned int convertedSize() const { return convert_size_; }

private:
  static bool pickDevice(
    const std::vector<DeviceInfo> & devices, const std::string & serial_number,
    const std::string & ip_address, unsigned int * index, std::string * error);

  void * handle_{nullptr};
  bool grabbing_{false};
  MV_FRAME_OUT frame_{};
  bool frame_valid_{false};

  std::vector<unsigned char> convert_buffer_;
  unsigned int convert_size_{0};
};

}  // namespace hikrobot_camera

#endif  // HIKROBOT_CAMERA__MVS_CAMERA_HPP_
