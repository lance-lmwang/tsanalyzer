#!/bin/bash
# libtsshaper FFmpeg Integration & Build Script
# Purpose: Compiles libtsshaper as a standalone static library and integrates it into the custom FFmpeg build.

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Robust ROOT_DIR detection: Resolve 3 levels up from the script's directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# --- Compile and Integrate libtsshaper into FFmpeg ---
echo "[*] Phase 1: Building libtsshaper..."
cd "$SCRIPT_DIR/.." || exit 1
make clean >/dev/null
make -j$(nproc) >/dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR] libtsshaper build failed!${NC}"
    exit 1
fi
echo -e "${GREEN}[PASS] libtsshaper compiled successfully.${NC}"

FFMPEG_MASTER_DIR="$(cd "$ROOT_DIR/../ffmpeg.wz.master" && pwd)"
echo "[*] Phase 2: Exporting artifacts to FFmpeg dependency tree ($FFMPEG_MASTER_DIR)..."
mkdir -p "$FFMPEG_MASTER_DIR/ffdeps_img/libtsshaper/lib/"
mkdir -p "$FFMPEG_MASTER_DIR/ffdeps_img/libtsshaper/include/tsshaper/"
cp libtsshaper.a "$FFMPEG_MASTER_DIR/ffdeps_img/libtsshaper/lib/"
cp include/tsshaper/*.h "$FFMPEG_MASTER_DIR/ffdeps_img/libtsshaper/include/tsshaper/"
echo -e "${GREEN}[PASS] Artifacts exported.${NC}"

echo "[*] Phase 3: Triggering FFmpeg build with new libtsshaper..."
cd "$FFMPEG_MASTER_DIR" || exit 1

HOST_UID=$(id -u)
HOST_GID=$(id -g)
DOCKER_IMAGE="visionular/wzffm-centos7-devtoolset-10:20230421T080217Z-1c39605f"

echo "[*] Running build in Docker container ($DOCKER_IMAGE)..."
docker run --rm \
  -e HOME=/tmp \
  --user $HOST_UID:$HOST_GID \
  -e HOST_UID=$HOST_UID \
  -e HOST_GID=$HOST_GID \
  -e ENABLE_SDK_AUTH=true \
  -v $(pwd):/work/src -w /work/src \
  $DOCKER_IMAGE \
  /bin/bash -c "git config --global user.name 'builder' && git config --global user.email 'builder@example.com' && ./scripts_ci/build.sh"

if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR] FFmpeg Docker build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}[SUCCESS] FFmpeg integration complete. You can now run the regression suite.${NC}"
