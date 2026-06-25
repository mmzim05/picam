#include "encoder.h"
#include <stdexcept>
#include <cstdlib>

Encoder::Encoder(int quality) : quality_(quality) {
    tj_ = tjInitCompress();
    if (!tj_)
        throw std::runtime_error(tjGetErrorStr());
}

Encoder::~Encoder() {
    if (jpg_buf_) tjFree(jpg_buf_);
    tjDestroy(tj_);
}

const uint8_t* Encoder::encode(
    const uint8_t* y,  int y_stride,
    const uint8_t* cb, int c_stride,
    const uint8_t* cr,
    uint32_t width, uint32_t height,
    size_t& out_size)
{
    const unsigned char* planes[3] = { y, cb, cr };
    int strides[3] = { y_stride, c_stride, c_stride };

    // tjCompressFromYUVPlanes handles fully-planar YUV420 (I420).
    int rc = tjCompressFromYUVPlanes(
        tj_,
        planes, (int)width, strides, (int)height,
        TJSAMP_420,
        &jpg_buf_, &jpg_size_,
        quality_,
        TJFLAG_FASTDCT | TJFLAG_NOREALLOC * (jpg_buf_ != nullptr));

    if (rc != 0)
        throw std::runtime_error(tjGetErrorStr2(tj_));

    out_size = (size_t)jpg_size_;
    return jpg_buf_;
}
