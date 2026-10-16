#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DEVKITPRO="/Users/pdrtorres/.gemini/antigravity/scratch/tools/dkp_root/opt/devkitpro"
export DEVKITARM="${DEVKITPRO}/devkitARM"
export PATH="/Users/pdrtorres/.gemini/antigravity/scratch/tools/cmake-4.4.4-macos-universal/CMake.app/Contents/bin:${DEVKITPRO}/tools/bin:${DEVKITPRO}/portlibs/3ds/bin:${DEVKITARM}/bin:${PATH}"

cd "${SCRIPT_DIR}"

echo "=== Compiling Julius ELF and 3DSX ==="
arm-none-eabi-cmake -B build-3ds -S . -DCMAKE_BUILD_TYPE=Release -DTARGET_PLATFORM=3ds -DSYSTEM_LIBS=OFF
cmake --build build-3ds -j8

mkdir -p dist/3ds/julius
cp build-3ds/julius.3dsx dist/3ds/julius/

echo "=== Generating 3DS Banner & Fanfare Audio ==="
python3 tools/make_banner_and_cia.py

echo "=== Packaging Banner & SMDH Icon ==="
bannertool makebanner -i build-3ds/banner.png -a build-3ds/banner.wav -o build-3ds/banner.bin
bannertool makesmdh -s "Julius" -l "Julius (Caesar III)" -p "bvschaik / Pedro Torres" -i res/julius_48.png -o build-3ds/julius.smdh
cp build-3ds/julius.smdh dist/3ds/julius/

echo "=== Building CIA Package ==="
makerom -f cia -o dist/3ds/julius/julius.cia -elf build-3ds/julius.elf -rsf julius.rsf -icon build-3ds/julius.smdh -banner build-3ds/banner.bin

echo "============================================="
echo "Build complete!"
echo "  3DSX: ${SCRIPT_DIR}/dist/3ds/julius/julius.3dsx"
echo "  CIA : ${SCRIPT_DIR}/dist/3ds/julius/julius.cia"
echo "============================================="
