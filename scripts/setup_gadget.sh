#!/bin/sh
# Set up USB UVC gadget via configfs.
# Must run after: modprobe dwc2 && mount -t configfs none /sys/kernel/config

set -e

GADGET=/sys/kernel/config/usb_gadget/picam

# ── Gadget identity ────────────────────────────────────────────────────────
mkdir -p "$GADGET"
echo 0x1d6b > "$GADGET/idVendor"   # Linux Foundation
echo 0x0104 > "$GADGET/idProduct"  # Multifunction Composite Gadget
echo 0x0100 > "$GADGET/bcdDevice"
echo 0x0200 > "$GADGET/bcdUSB"     # USB 2.0

mkdir -p "$GADGET/strings/0x409"
echo "picam0001"       > "$GADGET/strings/0x409/serialnumber"
echo "PiCam"           > "$GADGET/strings/0x409/manufacturer"
echo "Pi HD USB Camera" > "$GADGET/strings/0x409/product"

# ── Config ─────────────────────────────────────────────────────────────────
mkdir -p "$GADGET/configs/c.1/strings/0x409"
echo "UVC Camera"      > "$GADGET/configs/c.1/strings/0x409/configuration"
echo 500               > "$GADGET/configs/c.1/MaxPower"

# ── UVC function ───────────────────────────────────────────────────────────
F="$GADGET/functions/uvc.0"
mkdir -p "$F"

# High-Speed packet size: 3072 bytes = 3 × 1024 (max burst for HS isochronous)
echo 3072 > "$F/streaming_maxpacket"
echo 1    > "$F/streaming_interval"

# ── Control interface ──────────────────────────────────────────────────────
# UVC control interface is Full Speed only — no hs/ss class dirs in kernel 6.x
mkdir -p "$F/control/header/h"
mkdir -p "$F/control/class/fs"
ln -sf "$F/control/header/h" "$F/control/class/fs/h" 2>/dev/null || true

# ── Streaming: MJPEG format ────────────────────────────────────────────────
M="$F/streaming/mjpeg/m"
mkdir -p "$M"

# Helper: create one MJPEG frame descriptor
# Usage: add_frame <name> <width> <height> <default_interval> <intervals...>
add_frame() {
    local name="$1" w="$2" h="$3" def_iv="$4"
    shift 4
    local max_buf=$(( w * h * 2 ))  # upper bound for MJPEG frame (2 bytes/px)

    # Bit rates are u32 in UVC spec; cap at USB 2.0 HS max (480 Mbps) to avoid ERANGE
    local min_br=$(( w*h*2*8*5 ))
    local max_br=480000000
    [ $min_br -gt $max_br ] && min_br=$max_br

    mkdir -p "$M/$name"
    echo "$w"       > "$M/$name/wWidth"
    echo "$h"       > "$M/$name/wHeight"
    echo "$def_iv"  > "$M/$name/dwDefaultFrameInterval"
    echo "$min_br"  > "$M/$name/dwMinBitRate"
    echo "$max_br"  > "$M/$name/dwMaxBitRate"
    echo "$max_buf" > "$M/$name/dwMaxVideoFrameBufferSize"

    # Count intervals
    local n=0
    for iv in "$@"; do n=$((n+1)); done
    echo "$n" > "$M/$name/dwFrameIntervalType"

    # Write each interval in a separate write() call — configfs store() parses
    # one u32 per call; a single printf with all values only stores the first.
    for iv in "$@"; do
        printf '%u\n' "$iv" > "$M/$name/dwFrameInterval"
    done
}

# ── Frame interval constants (100 ns units) ────────────────────────────────
IV_5=2000000   # 5 fps
IV_10=1000000  # 10 fps
IV_15=666667   # 15 fps
IV_20=500000   # 20 fps
IV_24=416667   # 24 fps
IV_30=333333   # 30 fps
IV_40=250000   # 40 fps
IV_50=200000   # 50 fps
IV_60=166667   # 60 fps
IV_90=111111   # 90 fps
IV_120=83333   # 120 fps

# ── Native IMX477 sensor output sizes only — no scaling ───────────────────
# 4056×3040 — full sensor, full FOV
add_frame "4056x3040" 4056 3040 "$IV_10" "$IV_5" "$IV_10"

# 2028×1520 — 2×2 binned, full FOV
add_frame "2028x1520" 2028 1520 "$IV_30" "$IV_10" "$IV_20" "$IV_30" "$IV_40"

# 2028×1080 — 2×2 binned + 16:9 crop
add_frame "2028x1080" 2028 1080 "$IV_30" "$IV_10" "$IV_15" "$IV_24" "$IV_30" "$IV_50"

# 1332×990  — binned + crop, high-fps mode
add_frame "1332x990"  1332  990 "$IV_60" "$IV_30" "$IV_60" "$IV_90" "$IV_120"

# ── Streaming header (links format to header) ──────────────────────────────
mkdir -p "$F/streaming/header/h"
ln -sf "$M" "$F/streaming/header/h/m" 2>/dev/null || true

mkdir -p "$F/streaming/class/fs"
mkdir -p "$F/streaming/class/hs"
ln -sf "$F/streaming/header/h" "$F/streaming/class/fs/h" 2>/dev/null || true
ln -sf "$F/streaming/header/h" "$F/streaming/class/hs/h" 2>/dev/null || true

# ── Link function to config and bind ──────────────────────────────────────
ln -sf "$F" "$GADGET/configs/c.1/uvc.0" 2>/dev/null || true

# Bind to the dwc2 UDC (USB Device Controller)
UDC=$(ls /sys/class/udc | head -1)
if [ -z "$UDC" ]; then
    echo "ERROR: No UDC found. Is dwc2 loaded with dr_mode=peripheral?" >&2
    exit 1
fi
echo "$UDC" > "$GADGET/UDC"
echo "UVC gadget bound to $UDC"
