# PiCam — CLAUDE.md

## What this project is

Buildroot-based minimal Linux firmware for **Raspberry Pi Zero 2 W + HQ Camera (IMX477)**. Presents as a standard USB UVC webcam. GitHub Actions cross-compiles the firmware and publishes a flashable `sdcard.img.gz`.

## Hardware

- Pi Zero 2 W — BCM2710, 4× Cortex-A53, 512 MB RAM, aarch64
- HQ Camera — IMX477 sensor, connected via 2-lane MIPI CSI-2
- USB OTG port → host PC (UVC webcam)
- PWR port → power supply (separate from data)

## Supported camera modes (native IMX477, no scaling)

| Resolution | FPS options | Sensor mode |
|------------|-------------|-------------|
| 4056×3040 | 5, 10 | Full sensor, full FOV |
| 2028×1520 | 10, 20, 30, 40 | 2×2 binned, full FOV |
| 2028×1080 | 10, 15, 24, 30, 50 | 2×2 binned + 16:9 crop |
| 1332×990 | 30, 60, 90, 120 | Binned + crop (fast mode) |

No ISP scaling. What the sensor outputs is what USB delivers.

## Project structure

```
.github/workflows/build.yml       GitHub Actions CI — builds firmware, caches toolchain
buildroot/
  external.desc / Config.in / external.mk   Buildroot external tree boilerplate
  configs/picam_defconfig          All Buildroot package and kernel selections
  board/picam/
    config.txt                     Pi boot config (dwc2 OTG peripheral, imx477 overlay)
    cmdline.txt                    Kernel command line (ro, squashfs, quiet)
    kernel.config                  Kernel config fragment (UVC gadget, IMX477, squashfs)
    post-build.sh                  Installs gadget script + fstab into rootfs
    post-image.sh                  Copies Image→kernel8.img, runs genimage, gzips output
    genimage.cfg                   2-partition image: FAT32 boot + squashfs root
    S99picam                       BusyBox SysV init script (mounts configfs, runs app)
  package/picam-app/
    picam-app.mk                   cmake-package; SITE = ../src relative to external tree
    Config.in                      Buildroot menu entry
scripts/
  setup_gadget.sh                  configfs UVC gadget setup (all 4 native modes + all fps)
src/
  main.cpp                         CLI entry point, JPEG frame queue, wires camera↔UVC
  camera.cpp / camera.h            libcamera wrapper — mode switching, exposure/gain/WB
  encoder.cpp / encoder.h          libjpeg-turbo MJPEG encode from YUV420 planes
  uvc_gadget.cpp / uvc_gadget.h    V4L2 UVC gadget — mmap buffers, UVC control events
  CMakeLists.txt
README.md
```

## Key design decisions

- **C++17 + libcamera API directly** — no picamera2 (Python layer, not in Buildroot)
- **libjpeg-turbo** for MJPEG encoding — SIMD-accelerated, fast enough for all modes on 4× A53
- **BusyBox init** — faster boot than systemd (~3–5 s total)
- **squashfs read-only root** — fast mount, safe against SD corruption on power-loss
- **No network stack** — nothing to negotiate, nothing to fail, faster boot
- **Native sensor modes only** — libcamera selects the matching sensor mode; the ISP does not scale

## UVC controls (host-side)

Exposed as standard UVC Camera Terminal / Processing Unit controls:

| Control | UVC entity | Unit | libcamera mapping |
|---------|-----------|------|-------------------|
| Shutter speed | Camera Terminal | 100 µs | `ExposureTime` (µs) |
| ISO / gain | Processing Unit | 0–255 → 1×–64× | `AnalogueGain` |
| White balance | Processing Unit | Kelvin (2800–7500) | `ColourGains` via lookup table |

Set to 0 / auto to re-enable automatic control.

## Build system

- **Buildroot 2024.02.1 LTS** — download cached by URL hash in Actions
- **Kernel**: `raspberrypi/linux` branch `rpi-6.6.y`, defconfig `bcm2711`
- **Toolchain cache**: `output/host` (~1.5 GB) keyed on defconfig SHA256
- **ccache**: `~/.buildroot-ccache` for incremental C/C++ rebuilds
- Cold build: ~3 hours. Warm (cached toolchain): ~15 minutes.

## How to trigger a release

```bash
git tag v1.0
git push origin v1.0
```

Actions builds the image and attaches `sdcard.img.gz` to a GitHub Release.

## Flashing

```bash
gunzip -c sdcard.img.gz | sudo dd of=/dev/sdX bs=4M status=progress
```

## Common debug commands (on host after plugging in Pi)

```bash
# Confirm UVC device appeared
v4l2-ctl --list-devices

# List available modes
v4l2-ctl -d /dev/video0 --list-formats-ext

# List camera controls (shutter, gain, WB)
v4l2-ctl -d /dev/video0 --list-ctrls

# Set manual shutter (100 µs units; 100 = 10 ms)
v4l2-ctl -d /dev/video0 --set-ctrl=exposure_auto=1 --set-ctrl=exposure_absolute=100

# Set gain (0–255)
v4l2-ctl -d /dev/video0 --set-ctrl=gain=50

# Set white balance (Kelvin, auto off)
v4l2-ctl -d /dev/video0 \
  --set-ctrl=white_balance_temperature_auto=0 \
  --set-ctrl=white_balance_temperature=5500

# Stream to screen with ffplay
ffplay -f v4l2 -input_format mjpeg -video_size 2028x1520 /dev/video0
```

## Known issues / watch-outs

- **USB power**: Pi Zero 2 W must be powered from the PWR port. The OTG port carries data only in peripheral mode.
- **IMX477 IPA**: Buildroot's libcamera must install `/usr/share/libcamera/ipa/rpi/vc4/imx477.json` — post-build.sh warns if missing.
- **Kernel image name**: Pi bootloader needs `kernel8.img` for 64-bit. post-image.sh copies Buildroot's `Image` output to `kernel8.img` before genimage runs.
- **genimage not in apt**: `BR2_PACKAGE_HOST_GENIMAGE=y` has Buildroot compile it — do not add it to the apt install step in the workflow.
