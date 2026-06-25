# PiCam — Pi Zero 2 W as a USB webcam

Minimal Buildroot firmware that turns a **Raspberry Pi Zero 2 W + HQ Camera (IMX477)** into a standard USB UVC webcam. No drivers needed on the host. Shutter speed, ISO and white balance are exposed as standard UVC camera controls, adjustable from any app (OBS, ffmpeg, v4l2-ctl, etc.).

GitHub Actions builds and releases a flashable `.img.gz` — no local toolchain required.

---

## Hardware

| Item | Notes |
|------|-------|
| Raspberry Pi Zero 2 W | Any revision |
| Raspberry Pi HQ Camera | IMX477 sensor, any lens |
| micro-USB cable × 2 | One for **PWR** port, one for **USB OTG** port |
| microSD card | ≥ 256 MB |

**Important:** Connect the USB OTG port (the one closer to the middle of the board) to your computer. Power the Pi from the PWR port (the one at the edge).

---

## Supported resolutions

Native IMX477 sensor output — no scaling applied:

| Resolution | Frame rates | Field of view |
|------------|-------------|---------------|
| 4056×3040 | 5, 10 fps | Full |
| 2028×1520 | 10, 20, 30, 40 fps | Full |
| 2028×1080 | 10, 15, 24, 30, 50 fps | 16:9 crop |
| 1332×990 | 30, 60, 90, 120 fps | Cropped |

---

## Get the firmware

### Option A — Download from GitHub Releases (recommended)

1. Go to the **Releases** tab, download `sdcard.img.gz`
2. Flash:
   ```bash
   gunzip -c sdcard.img.gz | sudo dd of=/dev/sdX bs=4M status=progress
   ```
   Replace `/dev/sdX` with your SD card device.

### Option B — Build locally (requires Linux)

```bash
# Install Buildroot deps
sudo apt-get install build-essential libncurses-dev bison flex libssl-dev \
    libelf-dev bc cpio rsync wget unzip git python3 genimage dosfstools mtools

# Download Buildroot
wget https://buildroot.org/downloads/buildroot-2024.02.1.tar.gz
tar -xf buildroot-2024.02.1.tar.gz
cd buildroot-2024.02.1

# Configure and build
make BR2_EXTERNAL=../buildroot O=output picam_defconfig
make BR2_EXTERNAL=../buildroot O=output BR2_JLEVEL=0 all
# Output: output/images/sdcard.img.gz
```

First build: ~3 hours (downloads and compiles toolchain). Subsequent builds: ~15 minutes with cached toolchain.

---

## Host-side control

Once the Pi is running and connected, it appears as a standard UVC camera.

### Check controls
```bash
v4l2-ctl -d /dev/video0 --list-ctrls
```

### Adjust shutter speed
```bash
# Disable auto-exposure and set 10 ms shutter (unit = 100 µs, so 100 = 10 ms)
v4l2-ctl -d /dev/video0 \
    --set-ctrl=exposure_auto=1 \
    --set-ctrl=exposure_absolute=100
```

### Adjust ISO (analogue gain)
```bash
# 0 = auto, 1-255 mapped to 1×–64× gain
v4l2-ctl -d /dev/video0 --set-ctrl=gain=50
```

### White balance
```bash
# Disable auto and set colour temperature in Kelvin
v4l2-ctl -d /dev/video0 \
    --set-ctrl=white_balance_temperature_auto=0 \
    --set-ctrl=white_balance_temperature=5500
```

### Use with OBS
Add a **Video Capture Device** source → select **Pi HD USB Camera** → choose resolution and framerate from the dropdown.

---

## Project structure

```
├── .github/workflows/build.yml   # GitHub Actions CI/CD
├── buildroot/
│   ├── configs/picam_defconfig   # Buildroot config
│   ├── board/picam/              # Pi boot files, init scripts
│   └── package/picam-app/        # Buildroot package for the app
└── src/                          # C++ application
    ├── main.cpp
    ├── camera.cpp/h              # libcamera wrapper
    ├── encoder.cpp/h             # libjpeg-turbo MJPEG encoder
    └── uvc_gadget.cpp/h          # V4L2 UVC gadget driver
```

---

## Boot time

Typical boot-to-camera-ready: **3–5 seconds** (squashfs read-only root, BusyBox init).
