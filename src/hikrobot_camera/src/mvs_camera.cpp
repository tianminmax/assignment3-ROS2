#include "hikrobot_camera/mvs_camera.hpp"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>

namespace hikrobot_camera
{
namespace
{

/// Device types we search for. Virtual / GenTL transports are included so that
/// frame grabbers and the virtual camera shipped with MVS also work.
constexpr unsigned int kDeviceLayerMask =
    MV_GIGE_DEVICE | MV_USB_DEVICE | MV_GENTL_GIGE_DEVICE | MV_GENTL_CAMERALINK_DEVICE |
    MV_GENTL_CXP_DEVICE | MV_GENTL_XOF_DEVICE | MV_GENTL_XOC_DEVICE;

/// Tracks how many MvsCamera users are alive, so that MV_CC_Initialize() and
/// MV_CC_Finalize() are called exactly once per process.
std::mutex g_sdk_mutex;
int g_sdk_reference_count = 0;

/// Copy a fixed size, possibly unterminated, SDK char buffer into a std::string.
std::string toString(const unsigned char * buffer, std::size_t capacity)
{
    if (buffer == nullptr) {
        return {};
    }
    std::size_t length = 0;
    while (length < capacity && buffer[length] != '\0') {
        ++length;
    }
    return std::string(reinterpret_cast<const char *>(buffer), length);
}

std::string ipv4ToString(unsigned int ip)
{
    char buffer[32];
    std::snprintf(
        buffer, sizeof(buffer), "%u.%u.%u.%u", (ip >> 24) & 0xFFU, (ip >> 16) & 0xFFU,
        (ip >> 8) & 0xFFU, ip & 0xFFU);
    return buffer;
}

std::string macToString(unsigned int mac_high, unsigned int mac_low)
{
    char buffer[32];
    std::snprintf(
        buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", (mac_high >> 24) & 0xFFU,
        (mac_high >> 16) & 0xFFU, (mac_high >> 8) & 0xFFU, mac_high & 0xFFU,
        (mac_low >> 24) & 0xFFU, (mac_low >> 16) & 0xFFU);
    return buffer;
}

std::string trim(const std::string & input)
{
    const auto begin = input.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(begin, end - begin + 1);
}

std::string pixelTypeToHex(MvGvspPixelType type)
{
    char buffer[16] = {0};
    std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned int>(type));
    return buffer;
}

}  // namespace

// ---------------------------------------------------------------------------
// Process wide SDK lifetime.
// ---------------------------------------------------------------------------

void MvsCamera::initializeSdk()
{
    std::lock_guard<std::mutex> lock(g_sdk_mutex);
    if (g_sdk_reference_count == 0) {
        MV_CC_Initialize();
    }
    ++g_sdk_reference_count;
}

void MvsCamera::finalizeSdk()
{
    std::lock_guard<std::mutex> lock(g_sdk_mutex);
    if (g_sdk_reference_count == 0) {
        return;
    }
    --g_sdk_reference_count;
    if (g_sdk_reference_count == 0) {
        MV_CC_Finalize();
    }
}

std::string MvsCamera::errorToString(int code)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << static_cast<unsigned int>(code);
    switch (code) {
    case MV_OK:
        stream << " (success)";
        break;
    case MV_E_HANDLE:
        stream << " (invalid handle)";
        break;
    case MV_E_SUPPORT:
        stream << " (feature not supported by this camera)";
        break;
    case MV_E_BUFOVER:
        stream << " (buffer overflow)";
        break;
    case MV_E_CALLORDER:
        stream << " (functions were called in the wrong order)";
        break;
    case MV_E_PARAMETER:
        stream << " (invalid parameter)";
        break;
    case MV_E_RESOURCE:
        stream << " (resource allocation failed)";
        break;
    case MV_E_NODATA:
        stream << " (timeout, no data received)";
        break;
    case MV_E_PRECONDITION:
        stream << " (precondition not satisfied)";
        break;
    case MV_E_NOENOUGH_BUF:
        stream << " (provided buffer is too small)";
        break;
    case MV_E_ABNORMAL_IMAGE:
        stream << " (incomplete frame, likely packet loss)";
        break;
    case MV_E_LOAD_LIBRARY:
        stream << " (failed to load the transport layer library)";
        break;
    case MV_E_NOOUTBUF:
        stream << " (no output buffer available)";
        break;
    case MV_E_DEV_OFFLINE:
        stream << " (device went offline)";
        break;
    case MV_E_PARAMETER_RANGE:
        stream << " (value outside the supported range)";
        break;
    case MV_E_RESOURCE_IN_USE:
        stream << " (camera or stream already in use)";
        break;
    case MV_E_ACCESS_DENIED:
        stream << " (access denied, the camera may be occupied by another process)";
        break;
    case MV_E_BUSY:
        stream << " (device busy, or network disconnected)";
        break;
    case MV_E_NETER:
        stream << " (network error)";
        break;
    case MV_E_TIMEOUT:
        stream << " (network timeout)";
        break;
    case MV_E_DEV_DISCONNECT:
        stream << " (device disconnected)";
        break;
    case MV_E_IP_CONFLICT:
        stream << " (IP address conflict)";
        break;
    case MV_E_SUPPORT_PIXEL_FORMAT:
        stream << " (unsupported pixel format)";
        break;
    case MV_E_GC_NODE_NOT_FOUND:
        stream << " (feature node does not exist on this camera)";
        break;
    case MV_E_GC_RANGE:
        stream << " (GenICam value out of range)";
        break;
    default:
        break;
    }
    return stream.str();
}

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

std::vector<DeviceInfo> MvsCamera::enumerateDevices(std::string * error)
{
    std::vector<DeviceInfo> devices;

    MV_CC_DEVICE_INFO_LIST device_list{};
    const int ret = MV_CC_EnumDevices(kDeviceLayerMask, &device_list);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = "MV_CC_EnumDevices failed: " + errorToString(ret);
        }
        return devices;
    }

    for (unsigned int i = 0; i < device_list.nDeviceNum; ++i) {
        const MV_CC_DEVICE_INFO * info = device_list.pDeviceInfo[i];
        if (info == nullptr) {
            continue;
        }

        DeviceInfo device;
        device.index = i;
        device.transport_layer = info->nTLayerType;
        device.mac_address = macToString(info->nMacAddrHigh, info->nMacAddrLow);

        if (device.isGigE()) {
            const MV_GIGE_DEVICE_INFO & gige = info->SpecialInfo.stGigEInfo;
            device.model_name = toString(gige.chModelName, sizeof(gige.chModelName));
            device.serial_number = toString(gige.chSerialNumber, sizeof(gige.chSerialNumber));
            device.user_defined_name = toString(gige.chUserDefinedName, sizeof(gige.chUserDefinedName));
            device.ip_address = ipv4ToString(gige.nCurrentIp);
        } else if ((info->nTLayerType & MV_USB_DEVICE) != 0) {
            const MV_USB3_DEVICE_INFO & usb = info->SpecialInfo.stUsb3VInfo;
            device.model_name = toString(usb.chModelName, sizeof(usb.chModelName));
            device.serial_number = toString(usb.chSerialNumber, sizeof(usb.chSerialNumber));
            device.user_defined_name = toString(usb.chUserDefinedName, sizeof(usb.chUserDefinedName));
        } else {
            const MV_CML_DEVICE_INFO & cml = info->SpecialInfo.stCMLInfo;
            device.model_name = toString(cml.chModelName, sizeof(cml.chModelName));
            device.serial_number = toString(cml.chSerialNumber, sizeof(cml.chSerialNumber));
            device.user_defined_name = toString(cml.chUserDefinedName, sizeof(cml.chUserDefinedName));
        }

        devices.push_back(std::move(device));
    }

    return devices;
}

bool MvsCamera::pickDevice(
    const std::vector<DeviceInfo> & devices, const std::string & serial_number,
    const std::string & ip_address, unsigned int * index, std::string * error)
{
    const std::string wanted_serial = trim(serial_number);
    const std::string wanted_ip = trim(ip_address);

    if (devices.empty()) {
        if (error != nullptr) {
            *error = "no camera found; check the cable, the power supply and the MVS client";
        }
        return false;
    }

    std::vector<const DeviceInfo *> matches;
    for (const auto & device : devices) {
        const bool serial_matches =
            wanted_serial.empty() || device.serial_number == wanted_serial;
        const bool ip_matches = wanted_ip.empty() || device.ip_address == wanted_ip;
        if (serial_matches && ip_matches) {
            matches.push_back(&device);
        }
    }

    if (matches.empty()) {
        std::ostringstream stream;
        stream << "no camera matches serial='" << wanted_serial << "' ip='" << wanted_ip
               << "'; detected devices:";
        for (const auto & device : devices) {
            stream << " [" << device.model_name << " sn=" << device.serial_number
                   << " ip=" << (device.ip_address.empty() ? "-" : device.ip_address) << "]";
        }
        if (error != nullptr) {
            *error = stream.str();
        }
        return false;
    }

    if (matches.size() > 1) {
        if (error != nullptr) {
            *error = "camera selection is ambiguous (" + std::to_string(matches.size()) +
                     " matches); set serial_number to pick exactly one device";
        }
        return false;
    }

    *index = matches.front()->index;
    return true;
}

// ---------------------------------------------------------------------------
// Connection handling
// ---------------------------------------------------------------------------

MvsCamera::~MvsCamera()
{
    close();
}

bool MvsCamera::open(
    const std::string & serial_number, const std::string & ip_address, std::string * error)
{
    close();

    std::string enumerate_error;
    const std::vector<DeviceInfo> devices = enumerateDevices(&enumerate_error);
    if (!enumerate_error.empty()) {
        if (error != nullptr) {
            *error = enumerate_error;
        }
        return false;
    }

    unsigned int index = 0;
    if (!pickDevice(devices, serial_number, ip_address, &index, error)) {
        return false;
    }

    const DeviceInfo & device = devices[index];

    // The handle has to be created from the freshly enumerated list, not from the
    // copy above, because MV_CC_CreateHandle keeps a pointer into it.
    MV_CC_DEVICE_INFO_LIST device_list{};
    const int enum_ret = MV_CC_EnumDevices(kDeviceLayerMask, &device_list);
    if (enum_ret != MV_OK || index >= device_list.nDeviceNum) {
        if (error != nullptr) {
            *error = "camera disappeared between enumeration and open";
        }
        return false;
    }

    int ret = MV_CC_CreateHandle(&handle_, device_list.pDeviceInfo[index]);
    if (ret != MV_OK) {
        handle_ = nullptr;
        if (error != nullptr) {
            *error = "MV_CC_CreateHandle failed: " + errorToString(ret);
        }
        return false;
    }

    ret = MV_CC_OpenDevice(handle_);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = "MV_CC_OpenDevice failed for sn=" + device.serial_number + ": " +
                     errorToString(ret);
        }
        MV_CC_DestroyHandle(handle_);
        handle_ = nullptr;
        return false;
    }

    // GigE cameras need the packet size tuned, otherwise throughput collapses.
    if (device.isGigE()) {
        const int packet_size = MV_CC_GetOptimalPacketSize(handle_);
        if (packet_size > 0) {
            MV_CC_SetIntValueEx(handle_, "GevSCPSPacketSize", packet_size);
        }
    }

    // Continuous, free running acquisition (no hardware trigger). Keeping only the
    // newest frames prevents a slow consumer from building up latency.
    MV_CC_SetEnumValue(handle_, "TriggerMode", 0);
    MV_CC_SetEnumValueByString(handle_, "AcquisitionMode", "Continuous");
    MV_CC_SetGrabStrategy(handle_, MV_GrabStrategy_LatestImagesOnly);

    return true;
}

void MvsCamera::close()
{
    if (handle_ == nullptr) {
        return;
    }

    releaseFrame();

    if (grabbing_) {
        MV_CC_StopGrabbing(handle_);
        grabbing_ = false;
    }

    MV_CC_CloseDevice(handle_);
    MV_CC_DestroyHandle(handle_);
    handle_ = nullptr;
}

bool MvsCamera::startGrabbing(std::string * error)
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "startGrabbing called without an open device";
        }
        return false;
    }
    if (grabbing_) {
        return true;
    }

    // A small output queue absorbs scheduling jitter without adding much latency.
    MV_CC_SetImageNodeNum(handle_, 3);

    const int ret = MV_CC_StartGrabbing(handle_);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = "MV_CC_StartGrabbing failed: " + errorToString(ret);
        }
        return false;
    }

    grabbing_ = true;
    return true;
}

bool MvsCamera::stopGrabbing(std::string * error)
{
    releaseFrame();

    if (handle_ == nullptr || !grabbing_) {
        return true;
    }

    const int ret = MV_CC_StopGrabbing(handle_);
    grabbing_ = false;
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = "MV_CC_StopGrabbing failed: " + errorToString(ret);
        }
        return false;
    }
    return true;
}

GrabResult MvsCamera::grabFrame(Frame * frame, unsigned int timeout_ms, std::string * error)
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "grabFrame called without an open device";
        }
        return GrabResult::kError;
    }

    releaseFrame();

    const int ret = MV_CC_GetImageBuffer(handle_, &frame_, timeout_ms);
    if (ret == static_cast<int>(MV_E_NODATA)) {
        if (error != nullptr) {
            *error = "no frame within " + std::to_string(timeout_ms) + " ms";
        }
        return GrabResult::kTimeout;
    }
    if (ret == static_cast<int>(MV_E_ABNORMAL_IMAGE)) {
        // The SDK still hands the buffer over, but the frame is incomplete. Release
        // it immediately and let the caller decide whether to keep the stream alive.
        MV_CC_FreeImageBuffer(handle_, &frame_);
        if (error != nullptr) {
            *error = "incomplete frame (packet loss)";
        }
        return GrabResult::kDropped;
    }
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = "MV_CC_GetImageBuffer failed: " + errorToString(ret);
        }
        return GrabResult::kError;
    }

    frame_valid_ = true;
    frame->data = frame_.pBufAddr;
    frame->size = frame_.stFrameInfo.nFrameLen;
    frame->width = frame_.stFrameInfo.nWidth;
    frame->height = frame_.stFrameInfo.nHeight;
    frame->pixel_type = frame_.stFrameInfo.enPixelType;
    frame->host_timestamp_ms = static_cast<uint64_t>(frame_.stFrameInfo.nHostTimeStamp);
    frame->frame_number = frame_.stFrameInfo.nFrameNum;
    return GrabResult::kOk;
}

void MvsCamera::releaseFrame()
{
    if (frame_valid_ && handle_ != nullptr) {
        MV_CC_FreeImageBuffer(handle_, &frame_);
    }
    frame_valid_ = false;
}

// ---------------------------------------------------------------------------
// Feature access
// ---------------------------------------------------------------------------

bool MvsCamera::getFloat(
    const std::string & key, float * value, float * min_value, float * max_value,
    std::string * error) const
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    MVCC_FLOATVALUE feature{};
    const int ret = MV_CC_GetFloatValue(handle_, key.c_str(), &feature);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = key + " is not readable: " + errorToString(ret);
        }
        return false;
    }

    if (value != nullptr) {
        *value = feature.fCurValue;
    }
    if (min_value != nullptr) {
        *min_value = feature.fMin;
    }
    if (max_value != nullptr) {
        *max_value = feature.fMax;
    }
    return true;
}

bool MvsCamera::setFloat(const std::string & key, float value, std::string * error)
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    const int ret = MV_CC_SetFloatValue(handle_, key.c_str(), value);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = key + "=" + std::to_string(value) + " rejected: " + errorToString(ret);
        }
        return false;
    }
    return true;
}

bool MvsCamera::getInt(const std::string & key, int64_t * value, std::string * error) const
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    MVCC_INTVALUE_EX feature{};
    const int ret = MV_CC_GetIntValueEx(handle_, key.c_str(), &feature);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = key + " is not readable: " + errorToString(ret);
        }
        return false;
    }

    if (value != nullptr) {
        *value = feature.nCurValue;
    }
    return true;
}

bool MvsCamera::setInt(const std::string & key, int64_t value, std::string * error)
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    const int ret = MV_CC_SetIntValueEx(handle_, key.c_str(), value);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = key + "=" + std::to_string(value) + " rejected: " + errorToString(ret);
        }
        return false;
    }
    return true;
}

bool MvsCamera::setBool(const std::string & key, bool value, std::string * error)
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    const int ret = MV_CC_SetBoolValue(handle_, key.c_str(), value);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = key + "=" + (value ? "true" : "false") + " rejected: " + errorToString(ret);
        }
        return false;
    }
    return true;
}

bool MvsCamera::getEnum(const std::string & key, unsigned int * value, std::string * error) const
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    MVCC_ENUMVALUE feature{};
    const int ret = MV_CC_GetEnumValue(handle_, key.c_str(), &feature);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = key + " is not readable: " + errorToString(ret);
        }
        return false;
    }

    if (value != nullptr) {
        *value = feature.nCurValue;
    }
    return true;
}

bool MvsCamera::setEnumByString(
    const std::string & key, const std::string & value, std::string * error)
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    const int ret = MV_CC_SetEnumValueByString(handle_, key.c_str(), value.c_str());
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = key + "=" + value + " rejected: " + errorToString(ret);
        }
        return false;
    }
    return true;
}

bool MvsCamera::getEnumSymbolic(
    const std::string & key, unsigned int value, std::string * symbolic, std::string * error) const
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    MVCC_ENUMENTRY entry{};
    entry.nValue = value;
    const int ret = MV_CC_GetEnumEntrySymbolic(handle_, key.c_str(), &entry);
    if (ret != MV_OK) {
        if (error != nullptr) {
            *error = key + " symbolic lookup failed: " + errorToString(ret);
        }
        return false;
    }

    if (symbolic != nullptr) {
        *symbolic = entry.chSymbolic;
    }
    return true;
}

bool MvsCamera::getPixelFormat(std::string * symbolic, std::string * error) const
{
    unsigned int value = 0;
    if (!getEnum("PixelFormat", &value, error)) {
        return false;
    }
    return getEnumSymbolic("PixelFormat", value, symbolic, error);
}

bool MvsCamera::setPixelFormat(const std::string & symbolic, std::string * error)
{
    return setEnumByString("PixelFormat", symbolic, error);
}

// ---------------------------------------------------------------------------
// Pixel conversion
// ---------------------------------------------------------------------------

bool MvsCamera::convertFrame(const Frame & frame, MvGvspPixelType dst_type, std::string * error)
{
    if (handle_ == nullptr) {
        if (error != nullptr) {
            *error = "device is not open";
        }
        return false;
    }

    // 4 bytes per pixel is the worst case (BGRA) and keeps the buffer reusable for
    // every destination format we support.
    const std::size_t required = static_cast<std::size_t>(frame.width) * frame.height * 4U;
    if (convert_buffer_.size() < required) {
        convert_buffer_.resize(required);
    }

    MV_CC_PIXEL_CONVERT_PARAM_EX convert_param{};
    convert_param.nWidth = frame.width;
    convert_param.nHeight = frame.height;
    convert_param.enSrcPixelType = frame.pixel_type;
    convert_param.pSrcData = const_cast<unsigned char *>(frame.data);
    convert_param.nSrcDataLen = frame.size;
    convert_param.enDstPixelType = dst_type;
    convert_param.pDstBuffer = convert_buffer_.data();
    convert_param.nDstBufferSize = static_cast<unsigned int>(convert_buffer_.size());

    const int ret = MV_CC_ConvertPixelTypeEx(handle_, &convert_param);
    if (ret != MV_OK) {
        convert_size_ = 0;
        if (error != nullptr) {
            *error = "MV_CC_ConvertPixelTypeEx failed (src=" + pixelTypeToHex(frame.pixel_type) +
                     "): " + errorToString(ret);
        }
        return false;
    }

    convert_size_ = convert_param.nDstLen;
    return true;
}

}  // namespace hikrobot_camera
