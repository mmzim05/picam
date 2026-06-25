#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <string>

// Filled UVC gadget buffer ready to ship over USB.
// The UVCGadget calls this to request the next MJPEG frame.
using FillFrameFn = std::function<bool(void* buf, size_t capacity, size_t& used)>;

struct UVCMode {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
};

class UVCGadget {
public:
    explicit UVCGadget(const std::string& dev);
    ~UVCGadget();

    // Control change callbacks — set before run()
    std::function<void(uint32_t us)>     onExposure;   // 100µs units
    std::function<void(float gain)>      onGain;       // 1.0–64.0
    std::function<void(uint32_t kelvin)> onWhiteBalance;

    // Called when host picks a mode; caller should reconfigure camera.
    std::function<void(UVCMode)> onModeChange;

    // Main event loop — blocks until disconnect or fatal error.
    // fill_frame is called each time a gadget buffer needs a frame.
    void run(FillFrameFn fill_frame);

    UVCMode currentMode() const { return mode_; }

private:
    void handleEvent();
    void handleSetup(const uint8_t* data, int len);
    void handleStreamon();
    void handleStreamoff();
    void sendResponse(const void* data, int32_t len);
    void releaseBuffers();

    int fd_ = -1;

    struct GadgetBuf {
        void*  mem    = nullptr;
        size_t length = 0;
    };
    std::vector<GadgetBuf> bufs_;

    UVCMode mode_{2028, 1520, 30};
    bool    streaming_ = false;

    // Pending control SET data
    uint8_t  pending_cs_      = 0;
    uint8_t  pending_entity_  = 0;
};
