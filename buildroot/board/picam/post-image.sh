#!/bin/sh
# Buildroot post-image hook: assemble the SD card image via genimage.

set -e

BOARD_DIR="$(dirname "$0")"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

# Pi bootloader expects kernel8.img for 64-bit (arm_64bit=1).
# Buildroot outputs the kernel as "Image" — rename it.
cp "${BINARIES_DIR}/Image" "${BINARIES_DIR}/kernel8.img"

# Copy boot config files into images dir so genimage can find them
cp "${BOARD_DIR}/config.txt"  "${BINARIES_DIR}/config.txt"
cp "${BOARD_DIR}/cmdline.txt" "${BINARIES_DIR}/cmdline.txt"

# Copy DTB overlays from rpi-firmware build dir (Buildroot only installs
# the main firmware files, not the overlays/ subdirectory, to BINARIES_DIR)
RPI_FW_DIR="$(ls -d "${BUILD_DIR}"/rpi-firmware-* 2>/dev/null | head -1)"
if [ -d "${RPI_FW_DIR}/boot/overlays" ]; then
    mkdir -p "${BINARIES_DIR}/overlays"
    cp "${RPI_FW_DIR}/boot/overlays/"*.dtbo "${BINARIES_DIR}/overlays/"
else
    echo "ERROR: rpi-firmware overlays not found at ${RPI_FW_DIR}/boot/overlays" >&2
    exit 1
fi

rm -rf "${GENIMAGE_TMP}"

genimage \
    --rootpath "${TARGET_DIR}" \
    --tmppath  "${GENIMAGE_TMP}" \
    --inputpath "${BINARIES_DIR}" \
    --outputpath "${BINARIES_DIR}" \
    --config "${GENIMAGE_CFG}"

# Compress the output image
gzip -f -9 "${BINARIES_DIR}/sdcard.img"
echo "Output: ${BINARIES_DIR}/sdcard.img.gz"
echo "Flash: gunzip -c sdcard.img.gz | dd of=/dev/sdX bs=4M status=progress"
