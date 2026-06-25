#!/bin/sh
# Buildroot post-image hook: assemble the SD card image via genimage.

set -e

BOARD_DIR="$(dirname "$0")"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

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
