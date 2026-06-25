#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <libcamera/libcamera.h>

class Camera {
public:
    struct Mode {
        uint32_t width;
        uint32_t height;
        uint32_t fps;
    };

    // Called from libcamera thread with YUV420 plane pointers and strides.
    // Encoder must be called (or data copied) before returning.
    using FrameCallback = std::function<void(
        const uint8_t* y,  int y_stride,
        const uint8_t* cb, int c_stride,
        const uint8_t* cr,
        uint32_t width, uint32_t height)>;

    Camera();
    ~Camera();

    void open();
    void close();

    void setMode(const Mode& m);

    // 0 = auto
    void setExposureUs(uint32_t us);
    void setAnalogueGain(float gain);
    void setWhiteBalanceK(uint32_t kelvin);

    void start(FrameCallback cb);
    void stop();

    bool streaming() const { return streaming_; }

private:
    struct MappedBuffer {
        std::vector<uint8_t*> planes;
        std::vector<size_t>   lengths;
    };

    void onRequestCompleted(libcamera::Request* req);
    void applyControls(bool force = false);

    std::unique_ptr<libcamera::CameraManager>      cm_;
    std::shared_ptr<libcamera::Camera>              camera_;
    std::unique_ptr<libcamera::CameraConfiguration> config_;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator_;
    libcamera::Stream* stream_ = nullptr;
    std::vector<std::unique_ptr<libcamera::Request>> requests_;
    std::vector<MappedBuffer> mapped_;

    FrameCallback cb_;
    Mode mode_{2028, 1520, 30};
    bool streaming_ = false;

    uint32_t exposure_us_ = 0;
    float    gain_        = 0.0f;
    uint32_t wb_kelvin_   = 0;
    bool     ctrl_dirty_  = false;
};
