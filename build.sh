#!/usr/bin/env bash
# Local build helper — runs the Buildroot firmware build inside Docker.
# Requires: docker
#
# First run: ~3 hours (downloads toolchain, kernel, packages)
# Warm run:  ~15 minutes (ccache + cached toolchain)
#
# Output: buildroot-tree/output/images/sdcard.img.gz

set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BR_VERSION="2024.02.1"
BR_TARBALL="$REPO_DIR/buildroot-src.tar.gz"
BR_TREE="$REPO_DIR/buildroot-tree"
CCACHE_DIR="${BUILDROOT_CCACHE_DIR:-$HOME/.buildroot-ccache}"
IMAGE_NAME="picam-builder"

# Rebuild image if Dockerfile changed
DOCKERFILE="$REPO_DIR/docker/Dockerfile"
DOCKERFILE_HASH=$(sha256sum "$DOCKERFILE" | cut -d' ' -f1)
IMAGE_LABEL="picam-builder-$DOCKERFILE_HASH"

if ! docker image inspect "$IMAGE_LABEL" &>/dev/null; then
    echo ">>> Building Docker image..."
    docker build -t "$IMAGE_NAME" -t "$IMAGE_LABEL" "$REPO_DIR/docker"
fi

# Download Buildroot tarball if needed
if [ ! -f "$BR_TARBALL" ]; then
    echo ">>> Downloading Buildroot $BR_VERSION..."
    wget --show-progress \
        "https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.gz" \
        -O "$BR_TARBALL"
fi

# Extract Buildroot if needed
if [ ! -d "$BR_TREE" ]; then
    echo ">>> Extracting Buildroot $BR_VERSION..."
    tar -xf "$BR_TARBALL" -C "$REPO_DIR"
    mv "$REPO_DIR/buildroot-$BR_VERSION" "$BR_TREE"
fi

mkdir -p "$CCACHE_DIR"

echo ">>> Building firmware in Docker..."
docker run --rm \
    -v "$REPO_DIR:/build" \
    -v "$CCACHE_DIR:/root/.buildroot-ccache" \
    -e FORCE_UNSAFE_CONFIGURE=1 \
    "$IMAGE_NAME" \
    bash -c "
        set -e
        cd /build/buildroot-tree
        make BR2_EXTERNAL=/build/buildroot O=output picam_defconfig
        make BR2_EXTERNAL=/build/buildroot O=output \
            BR2_CCACHE_DIR=/root/.buildroot-ccache \
            BR2_JLEVEL=0 \
            all
    "

echo ""
echo "Build complete!"
echo "  Image: $BR_TREE/output/images/sdcard.img.gz"
echo ""
echo "Flash:"
echo "  gunzip -c $BR_TREE/output/images/sdcard.img.gz | sudo dd of=/dev/sdX bs=4M status=progress"
