#include "camera.h"
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <sys/mman.h>
#include <libcamera/controls.h>
#include <libcamera/formats.h>

using namespace libcamera;

// Convert colour temperature (K) to {r_gain, b_gain}.
// Linear interpolation across measured anchor points.
static std::pair<float,float> kelvinToGains(uint32_t k) {
    struct Pt { uint32_t k; float r, b; };
    static constexpr Pt pts[] = {
        {2800, 2.00f, 0.52f},   // tungsten
        {3200, 1.90f, 0.58f},   // warm white
        {4000, 1.75f, 0.72f},   // fluorescent
        {5000, 1.55f, 0.90f},   // horizon daylight
        {5500, 1.45f, 1.00f},   // noon daylight
        {6500, 1.30f, 1.20f},   // overcast / cool
        {7500, 1.20f, 1.40f},   // shade
    };
    if (k <= pts[0].k) return {pts[0].r, pts[0].b};
    for (size_t i = 1; i < sizeof(pts)/sizeof(pts[0]); ++i) {
        if (k <= pts[i].k) {
            float t = float(k - pts[i-1].k) / float(pts[i].k - pts[i-1].k);
            return {pts[i-1].r + t*(pts[i].r - pts[i-1].r),
                    pts[i-1].b + t*(pts[i].b - pts[i-1].b)};
        }
    }
    return {pts[6].r, pts[6].b};
}

Camera::Camera() = default;
Camera::~Camera() { close(); }

void Camera::open() {
    cm_ = std::make_unique<CameraManager>();
    if (cm_->start())
        throw std::runtime_error("CameraManager::start failed");

    if (cm_->cameras().empty())
        throw std::runtime_error("No cameras found");

    camera_ = cm_->get(cm_->cameras().front()->id());
    if (!camera_)
        throw std::runtime_error("Camera::get failed");

    if (camera_->acquire())
        throw std::runtime_error("Camera::acquire failed");
}

void Camera::close() {
    stop();
    if (camera_) {
        camera_->release();
        camera_.reset();
    }
    if (cm_) {
        cm_->stop();
        cm_.reset();
    }
}

void Camera::setMode(const Mode& m) {
    bool was_streaming = streaming_;
    if (was_streaming) stop();

    mode_ = m;

    if (was_streaming) start(cb_);
}

void Camera::setExposureUs(uint32_t us) {
    exposure_us_ = us;
    ctrl_dirty_  = true;
    if (streaming_) applyControls();
}

void Camera::setAnalogueGain(float gain) {
    gain_       = gain;
    ctrl_dirty_ = true;
    if (streaming_) applyControls();
}

void Camera::setWhiteBalanceK(uint32_t kelvin) {
    wb_kelvin_  = kelvin;
    ctrl_dirty_ = true;
    if (streaming_) applyControls();
}

void Camera::applyControls(bool /*force*/) {
    if (!camera_) return;
    ControlList ctrls(camera_->controls());

    if (exposure_us_ > 0) {
        ctrls.set(controls::AeEnable, false);
        ctrls.set(controls::ExposureTime, (int32_t)exposure_us_);
    } else {
        ctrls.set(controls::AeEnable, true);
    }

    if (gain_ > 0.0f) {
        ctrls.set(controls::AnalogueGain, gain_);
    }

    if (wb_kelvin_ > 0) {
        auto [r, b] = kelvinToGains(wb_kelvin_);
        ctrls.set(controls::AwbEnable, false);
        ctrls.set(controls::ColourGains, Span<const float, 2>({r, b}));
    } else {
        ctrls.set(controls::AwbEnable, true);
    }

    camera_->setControls(ctrls);
    ctrl_dirty_ = false;
}

void Camera::start(FrameCallback cb) {
    cb_ = std::move(cb);

    config_ = camera_->generateConfiguration({ StreamRole::VideoRecording });
    if (!config_)
        throw std::runtime_error("generateConfiguration failed");

    auto& sc = config_->at(0);
    sc.size        = Size(mode_.width, mode_.height);
    sc.pixelFormat = formats::YUV420;
    sc.bufferCount = 4;

    // Let libcamera pick the native sensor mode that matches this output size.
    // No scaling requested — the sensor mode that natively outputs this
    // resolution will be selected by the pipeline handler.
    if (config_->validate() == CameraConfiguration::Invalid)
        throw std::runtime_error("CameraConfiguration invalid");

    // If libcamera adjusted the size it means no native mode matches.
    if (sc.size.width != mode_.width || sc.size.height != mode_.height) {
        // Revert to what libcamera says it can do natively
        mode_.width  = sc.size.width;
        mode_.height = sc.size.height;
    }

    if (camera_->configure(config_.get()))
        throw std::runtime_error("Camera::configure failed");

    stream_ = sc.stream();

    allocator_ = std::make_unique<FrameBufferAllocator>(camera_);
    if (allocator_->allocate(stream_) < 0)
        throw std::runtime_error("FrameBufferAllocator::allocate failed");

    // Map all frame buffers once; keep mappings alive for the session.
    mapped_.clear();
    for (auto& buf : allocator_->buffers(stream_)) {
        MappedBuffer mb;
        for (const auto& plane : buf->planes()) {
            void* mem = mmap(nullptr, plane.length,
                             PROT_READ, MAP_SHARED,
                             plane.fd.get(), plane.offset);
            if (mem == MAP_FAILED)
                throw std::runtime_error("mmap failed for camera plane");
            mb.planes.push_back(static_cast<uint8_t*>(mem));
            mb.lengths.push_back(plane.length);
        }
        mapped_.push_back(std::move(mb));
    }

    // Wire up the completion signal.
    camera_->requestCompleted.connect(this, &Camera::onRequestCompleted);

    // Build and queue all requests.
    requests_.clear();
    for (size_t i = 0; i < allocator_->buffers(stream_).size(); ++i) {
        auto req = camera_->createRequest((uint64_t)i);
        if (!req) throw std::runtime_error("createRequest failed");
        if (req->addBuffer(stream_, allocator_->buffers(stream_)[i].get()))
            throw std::runtime_error("addBuffer failed");
        requests_.push_back(std::move(req));
    }

    ControlList ctrls(camera_->controls());
    // Frame duration: enforce fps
    int64_t ft = 1'000'000 / mode_.fps;
    ctrls.set(controls::FrameDurationLimits, Span<const int64_t, 2>({ft, ft}));

    if (camera_->start(&ctrls))
        throw std::runtime_error("Camera::start failed");

    applyControls(true);

    for (auto& req : requests_)
        camera_->queueRequest(req.get());

    streaming_ = true;
}

void Camera::stop() {
    if (!streaming_) return;
    streaming_ = false;

    camera_->stop();
    camera_->requestCompleted.disconnect(this, &Camera::onRequestCompleted);

    requests_.clear();

    for (auto& mb : mapped_)
        for (size_t i = 0; i < mb.planes.size(); ++i)
            munmap(mb.planes[i], mb.lengths[i]);
    mapped_.clear();

    if (allocator_) {
        allocator_->free(stream_);
        allocator_.reset();
    }
    stream_ = nullptr;
    config_.reset();
}

void Camera::onRequestCompleted(Request* req) {
    if (!streaming_ || req->status() == Request::RequestCancelled)
        return;

    auto* buf = req->findBuffer(stream_);
    if (!buf) return;

    uint64_t idx = req->cookie();
    const auto& mb = mapped_[idx];

    // YUV420: plane0=Y, plane1=Cb, plane2=Cr
    // Stride = width for each plane (libcamera packs tightly for YUV420)
    if (cb_ && mb.planes.size() >= 3) {
        cb_(mb.planes[0], mode_.width,
            mb.planes[1], mode_.width / 2,
            mb.planes[2],
            mode_.width, mode_.height);
    }

    req->reuse(Request::ReuseFlag::ReuseBuffers);
    camera_->queueRequest(req);
}
