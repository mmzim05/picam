#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <turbojpeg.h>

class Encoder {
public:
    // quality 1-100
    explicit Encoder(int quality = 85);
    ~Encoder();

    // Compress one YUV420 frame into an internally managed buffer.
    // Returns pointer + size valid until next call.
    // Not thread-safe — call only from one thread.
    const uint8_t* encode(
        const uint8_t* y,  int y_stride,
        const uint8_t* cb, int c_stride,
        const uint8_t* cr,
        uint32_t width, uint32_t height,
        size_t& out_size);

private:
    tjhandle       tj_;
    int            quality_;
    unsigned char* jpg_buf_  = nullptr;
    unsigned long  jpg_size_ = 0;
};
