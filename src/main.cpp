#include "camera.h"
#include "encoder.h"
#include "uvc_gadget.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <mutex>
#include <vector>
#include <csignal>
#include <unistd.h>

static std::atomic<bool> g_quit{false};

static void sigHandler(int) { g_quit = true; }

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -d <dev>   UVC gadget device (default: /dev/video0)\n"
        "  -q <1-100> JPEG quality (default: 85)\n"
        "  -w <px>    Initial width  (default: 2028)\n"
        "  -h <px>    Initial height (default: 1520)\n"
        "  -f <fps>   Initial fps    (default: 30)\n"
        , prog);
}

int main(int argc, char** argv) {
    const char* dev    = "/dev/video0";
    int         quality = 85;
    Camera::Mode init_mode{2028, 1520, 30};

    int opt;
    while ((opt = getopt(argc, argv, "d:q:w:h:f:")) != -1) {
        switch (opt) {
        case 'd': dev           = optarg;         break;
        case 'q': quality       = atoi(optarg);   break;
        case 'w': init_mode.width  = atoi(optarg); break;
        case 'h': init_mode.height = atoi(optarg); break;
        case 'f': init_mode.fps    = atoi(optarg); break;
        default:  usage(argv[0]); return 1;
        }
    }

    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    // Shared JPEG frame: protected by mutex, updated by camera thread.
    struct Frame {
        std::vector<uint8_t> data;
        bool fresh = false;
    };
    Frame frame;
    std::mutex frame_mx;

    Camera  camera;
    Encoder encoder(quality);

    try {
        camera.open();
        camera.setMode(init_mode);

        UVCGadget gadget(dev);

        // Wire camera controls from UVC events
        gadget.onExposure = [&](uint32_t us) {
            camera.setExposureUs(us);
        };
        gadget.onGain = [&](float g) {
            camera.setAnalogueGain(g);
        };
        gadget.onWhiteBalance = [&](uint32_t k) {
            camera.setWhiteBalanceK(k);
        };
        gadget.onModeChange = [&](UVCMode m) {
            Camera::Mode cm{m.width, m.height, m.fps};
            camera.setMode(cm);
        };

        // Camera callback: encode and stash latest frame
        camera.start([&](const uint8_t* y,  int ys,
                         const uint8_t* cb, int cs,
                         const uint8_t* cr,
                         uint32_t w, uint32_t h) {
            size_t sz = 0;
            const uint8_t* jpg = encoder.encode(y, ys, cb, cs, cr, w, h, sz);
            std::lock_guard<std::mutex> lk(frame_mx);
            frame.data.assign(jpg, jpg + sz);
            frame.fresh = true;
        });

        // UVC fill-frame callback: copy latest JPEG into gadget buffer
        auto fill = [&](void* buf, size_t cap, size_t& used) -> bool {
            std::lock_guard<std::mutex> lk(frame_mx);
            if (frame.data.empty()) { used = 0; return false; }
            size_t n = std::min(frame.data.size(), cap);
            memcpy(buf, frame.data.data(), n);
            used = n;
            frame.fresh = false;
            return true;
        };

        gadget.run(fill);  // blocks

    } catch (const std::exception& e) {
        fprintf(stderr, "Fatal: %s\n", e.what());
        return 1;
    }

    return 0;
}
