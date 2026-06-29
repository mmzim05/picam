#!/bin/sh
# Buildroot post-build hook: installs Pi-specific files into the rootfs.

set -e

TARGET="$1"
BOARD_DIR="$(dirname "$0")"

# Install the gadget setup script
install -D -m 0755 "$BOARD_DIR/../../../scripts/setup_gadget.sh" \
    "$TARGET/usr/share/picam/setup_gadget.sh"

# Install BusyBox SysV init script
install -D -m 0755 "$BOARD_DIR/S99picam" \
    "$TARGET/etc/init.d/S99picam"

# Install WiFi debug init script
install -D -m 0755 "$BOARD_DIR/S05wifi" \
    "$TARGET/etc/init.d/S05wifi"

# Create /boot mount point for FAT partition (not created by Buildroot by default)
mkdir -p "$TARGET/boot"

# Install Pi camera tuning file for IMX477 (if not already in libcamera package)
# Buildroot's libcamera installs these under /usr/share/libcamera/ipa/rpi/vc4/
# Nothing extra needed — just verify it's there.
if [ ! -f "$TARGET/usr/share/libcamera/ipa/rpi/vc4/imx477.json" ]; then
    echo "WARNING: IMX477 tuning file not found. libcamera may not expose controls." >&2
fi

# Minimal /etc/fstab for read-only root + tmpfs for /tmp and /var
cat > "$TARGET/etc/fstab" << 'EOF'
proc            /proc           proc        defaults            0  0
sysfs           /sys            sysfs       defaults            0  0
/dev/mmcblk0p1  /boot           vfat        ro,noatime          0  2
tmpfs           /tmp            tmpfs       nosuid,nodev,size=64m   0  0
tmpfs           /var            tmpfs       nosuid,nodev,size=16m   0  0
tmpfs           /run            tmpfs       nosuid,nodev,size=8m    0  0
EOF

# Remove unnecessary getty lines from inittab — we don't want a login shell
# on HDMI/serial in production. Keep serial for debugging.
if [ -f "$TARGET/etc/inittab" ]; then
    # Comment out tty1 getty (HDMI), keep ttyAMA0 for serial debugging
    sed -i 's|^tty1::.*|#&|' "$TARGET/etc/inittab"
fi
