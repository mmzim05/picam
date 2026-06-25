#include "uvc_gadget.h"
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/usb/g_uvc.h>
#include <linux/usb/ch9.h>
#include <linux/videodev2.h>

// UVC entity IDs in the kernel's built-in UVC gadget descriptors
static constexpr uint8_t CT_ID = 1;  // Camera Terminal
static constexpr uint8_t PU_ID = 2;  // Processing Unit

// UVC control selectors
static constexpr uint8_t CT_EXPOSURE_TIME_ABSOLUTE = 0x04;
static constexpr uint8_t PU_GAIN                   = 0x04;
static constexpr uint8_t PU_WB_TEMPERATURE         = 0x0A;
static constexpr uint8_t PU_WB_TEMPERATURE_AUTO    = 0x0B;

// UVC request codes (from USB Video Class spec 1.5, Table A-5)
#ifndef UVC_SET_CUR
# define UVC_SET_CUR 0x01
# define UVC_GET_CUR 0x81
# define UVC_GET_MIN 0x82
# define UVC_GET_MAX 0x83
# define UVC_GET_RES 0x84
# define UVC_GET_LEN 0x85
# define UVC_GET_INF 0x86
# define UVC_GET_DEF 0x87
#endif

// Gadget buffer count
static constexpr int N_BUFS = 4;

static int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

UVCGadget::UVCGadget(const std::string& dev) {
    fd_ = open(dev.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
        throw std::runtime_error("Cannot open " + dev + ": " + strerror(errno));

    // Subscribe to all UVC events
    static const uint32_t uvc_events[] = {
        UVC_EVENT_CONNECT, UVC_EVENT_DISCONNECT,
        UVC_EVENT_STREAMON, UVC_EVENT_STREAMOFF,
        UVC_EVENT_SETUP, UVC_EVENT_DATA,
    };
    for (auto type : uvc_events) {
        v4l2_event_subscription sub{};
        sub.type = type;
        if (xioctl(fd_, VIDIOC_SUBSCRIBE_EVENT, &sub))
            throw std::runtime_error("VIDIOC_SUBSCRIBE_EVENT failed");
    }
}

UVCGadget::~UVCGadget() {
    if (streaming_) handleStreamoff();
    if (fd_ >= 0) ::close(fd_);
}

void UVCGadget::sendResponse(const void* data, int32_t len) {
    uvc_request_data resp{};
    resp.length = len;
    if (len > 0 && data)
        memcpy(resp.data, data, std::min((size_t)len, sizeof(resp.data)));
    xioctl(fd_, UVCIOC_SEND_RESPONSE, &resp);
}

void UVCGadget::handleSetup(const uint8_t* raw, int /*len*/) {
    const auto* ctrl = reinterpret_cast<const usb_ctrlrequest*>(raw);
    uint8_t req      = ctrl->bRequest;
    uint8_t cs       = (uint16_t)(ctrl->wValue) >> 8;
    uint8_t entity   = (uint16_t)(ctrl->wIndex) >> 8;

    bool is_get = (ctrl->bRequestType & USB_DIR_IN) != 0;

    if (is_get) {
        // Host asking for info about a control
        if (entity == CT_ID && cs == CT_EXPOSURE_TIME_ABSOLUTE) {
            uint32_t val = 0;
            switch (req) {
                case UVC_GET_MIN: val = 1;       break;  //  0.1 ms
                case UVC_GET_MAX: val = 100000;  break;  // 10 s
                case UVC_GET_RES: val = 1;       break;
                case UVC_GET_DEF: val = 100;     break;  // 10 ms
                case UVC_GET_CUR: val = 100;     break;
            }
            sendResponse(&val, sizeof(val));
        } else if (entity == PU_ID && cs == PU_GAIN) {
            uint16_t val = 0;
            switch (req) {
                case UVC_GET_MIN: val = 0;   break;
                case UVC_GET_MAX: val = 255; break;
                case UVC_GET_RES: val = 1;   break;
                case UVC_GET_DEF: val = 0;   break;  // auto
                case UVC_GET_CUR: val = 0;   break;
            }
            sendResponse(&val, sizeof(val));
        } else if (entity == PU_ID && cs == PU_WB_TEMPERATURE) {
            uint16_t val = 0;
            switch (req) {
                case UVC_GET_MIN: val = 2800; break;
                case UVC_GET_MAX: val = 7500; break;
                case UVC_GET_RES: val = 10;   break;
                case UVC_GET_DEF: val = 0;    break;  // auto
                case UVC_GET_CUR: val = 0;    break;
            }
            sendResponse(&val, sizeof(val));
        } else if (entity == PU_ID && cs == PU_WB_TEMPERATURE_AUTO) {
            uint8_t val = (req == UVC_GET_DEF || req == UVC_GET_CUR) ? 1 : 0;
            sendResponse(&val, sizeof(val));
        } else {
            // Unknown control: STALL (negative length signals stall)
            sendResponse(nullptr, -EL2HLT);
        }
    } else {
        // SET request: data arrives in a subsequent DATA event
        pending_cs_     = cs;
        pending_entity_ = entity;
        // Acknowledge with zero-length response
        sendResponse(nullptr, 0);
    }
}

void UVCGadget::handleStreamon() {
    if (streaming_) return;

    // Query the format the host negotiated
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    xioctl(fd_, VIDIOC_G_FMT, &fmt);

    UVCMode old = mode_;
    mode_.width  = fmt.fmt.pix.width;
    mode_.height = fmt.fmt.pix.height;
    // fps is encoded in the UVC probe/commit; query via control if needed.
    // For now keep the fps the camera was last configured with.

    if (onModeChange && (mode_.width != old.width || mode_.height != old.height))
        onModeChange(mode_);

    // Allocate mmap buffers
    v4l2_requestbuffers req{};
    req.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;
    req.count  = N_BUFS;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) || req.count == 0)
        throw std::runtime_error("VIDIOC_REQBUFS failed");

    bufs_.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        xioctl(fd_, VIDIOC_QUERYBUF, &buf);

        void* mem = mmap(nullptr, buf.length,
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         fd_, buf.m.offset);
        if (mem == MAP_FAILED)
            throw std::runtime_error("mmap gadget buffer failed");
        bufs_[i] = {mem, buf.length};
    }

    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    xioctl(fd_, VIDIOC_STREAMON, &type);
    streaming_ = true;
}

void UVCGadget::handleStreamoff() {
    if (!streaming_) return;
    streaming_ = false;

    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);

    releaseBuffers();
}

void UVCGadget::releaseBuffers() {
    for (auto& b : bufs_)
        if (b.mem) { munmap(b.mem, b.length); b.mem = nullptr; }
    bufs_.clear();

    v4l2_requestbuffers req{};
    req.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;
    req.count  = 0;
    xioctl(fd_, VIDIOC_REQBUFS, &req);
}

void UVCGadget::handleEvent() {
    v4l2_event ev{};
    if (xioctl(fd_, VIDIOC_DQEVENT, &ev)) return;

    switch (ev.type) {
    case UVC_EVENT_CONNECT:
        break;
    case UVC_EVENT_DISCONNECT:
        handleStreamoff();
        break;
    case UVC_EVENT_STREAMON:
        handleStreamon();
        break;
    case UVC_EVENT_STREAMOFF:
        handleStreamoff();
        break;
    case UVC_EVENT_SETUP: {
        const auto* uvc = reinterpret_cast<const uvc_event*>(ev.u.data);
        handleSetup(uvc->req.data, uvc->req.length);
        break;
    }
    case UVC_EVENT_DATA: {
        // Arrived after a SET_CUR setup packet
        const auto* uvc = reinterpret_cast<const uvc_event*>(ev.u.data);
        const uint8_t* d = uvc->req.data;

        if (pending_entity_ == CT_ID && pending_cs_ == CT_EXPOSURE_TIME_ABSOLUTE) {
            uint32_t val;
            memcpy(&val, d, sizeof(val));
            // UVC unit = 100 µs
            if (onExposure) onExposure(val * 100);   // → µs for libcamera
        } else if (pending_entity_ == PU_ID && pending_cs_ == PU_GAIN) {
            uint16_t val;
            memcpy(&val, d, sizeof(val));
            // Map 0-255 → 1.0–64.0
            float gain = 1.0f + (val / 255.0f) * 63.0f;
            if (onGain) onGain(gain);
        } else if (pending_entity_ == PU_ID && pending_cs_ == PU_WB_TEMPERATURE) {
            uint16_t val;
            memcpy(&val, d, sizeof(val));
            if (onWhiteBalance) onWhiteBalance(val);
        } else if (pending_entity_ == PU_ID && pending_cs_ == PU_WB_TEMPERATURE_AUTO) {
            uint8_t val = d[0];
            if (val == 1 && onWhiteBalance) onWhiteBalance(0); // 0 = auto
        }
        pending_cs_ = pending_entity_ = 0;
        break;
    }
    default:
        break;
    }
}

void UVCGadget::run(FillFrameFn fill) {
    while (true) {
        pollfd pfd{};
        pfd.fd     = fd_;
        pfd.events = POLLPRI | (streaming_ ? POLLOUT : 0);

        if (poll(&pfd, 1, 1000) < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("poll failed");
        }

        if (pfd.revents & POLLPRI)
            handleEvent();

        if ((pfd.revents & POLLOUT) && streaming_) {
            v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            buf.memory = V4L2_MEMORY_MMAP;

            if (xioctl(fd_, VIDIOC_DQBUF, &buf) == 0) {
                size_t used = 0;
                if (buf.index < bufs_.size() && fill) {
                    fill(bufs_[buf.index].mem,
                         bufs_[buf.index].length,
                         used);
                }
                buf.bytesused = (uint32_t)used;
                xioctl(fd_, VIDIOC_QBUF, &buf);
            }
        }
    }
}
