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

# Copy Pi firmware files from rpi-firmware build dir to BINARIES_DIR.
# Buildroot's rpi-firmware package downloads to BUILD_DIR but does not
# install start.elf/fixup.dat/bootcode.bin/overlays to BINARIES_DIR.
RPI_FW_DIR="$(ls -d "${BUILD_DIR}"/rpi-firmware-* 2>/dev/null | head -1)"
if [ -d "${RPI_FW_DIR}/boot" ]; then
    cp "${RPI_FW_DIR}/boot/"*.elf  "${BINARIES_DIR}/" 2>/dev/null || true
    cp "${RPI_FW_DIR}/boot/"*.dat  "${BINARIES_DIR}/" 2>/dev/null || true
    cp "${RPI_FW_DIR}/boot/bootcode.bin" "${BINARIES_DIR}/" 2>/dev/null || true
    mkdir -p "${BINARIES_DIR}/overlays"
    cp "${RPI_FW_DIR}/boot/overlays/"*.dtbo "${BINARIES_DIR}/overlays/" 2>/dev/null || true
else
    echo "ERROR: rpi-firmware build dir not found at ${RPI_FW_DIR}" >&2
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
