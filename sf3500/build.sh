#!/bin/bash
# Builds Streets of Rage Remake's BennuGD runtime (bgdi) for the Datafrog SF3500.
#
#   SF3500_TC=/path/to/toolchain/host \
#   SDL2_SRC=/path/to/unpacked/SDL2-2.0.14 \
#   MIXER_SRC=/path/to/SDL2_mixer-2.6.3 \
#       ./sf3500/build.sh [output-dir]
#
# SDL2_SRC must be the unmodified SDL2 2.0.14 release; the SF3500 patch is
# applied to a copy. The toolchain's sysroot needs SDL2 headers, libpng and zlib.
# Writes <output-dir>/bgdi.mipsel (stripped). The game data (SorR.dat, mod/,
# palettes/) is the player's own and is not part of this repository.
set -e
: "${SF3500_TC:?set SF3500_TC to the toolchain host directory}"
: "${SDL2_SRC:?set SDL2_SRC to an unpacked SDL2 2.0.14 source tree}"
: "${MIXER_SRC:?set MIXER_SRC to an SDL2_mixer 2.6.x source tree}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mkdir -p "${1:-$SRC/build-sf3500}" && cd "${1:-$SRC/build-sf3500}" && pwd)"
TOOLCHAIN="$SRC/sf3500/cmake/sf3500-toolchain.cmake"
SDL="$OUT/prefix-sdl2"
MIX="$OUT/prefix-mixer"

# 1. SDL2 with the SF3500 patch (audio pacing, input bridge, divide-by-zero guard).
rm -rf "$OUT/sdl2-src" && cp -r "$SDL2_SRC" "$OUT/sdl2-src"
(cd "$OUT/sdl2-src" && patch -p1 -s < "$SRC/sf3500/sdl2-2.0.14-sf3500.patch")
mkdir -p "$OUT/sdl2-build" && cd "$OUT/sdl2-build"
# pkg-config and KMSDRM are switched off so no host headers leak into the cross build.
cmake "$OUT/sdl2-src" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_INSTALL_PREFIX="$SDL" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=OFF -DSDL_TEST=OFF \
  -DSDL_HAPTIC=OFF -DSDL_POWER=OFF -DSDL_SENSOR=OFF -DPKG_CONFIG_EXECUTABLE=/nonexistent/pkg-config \
  -DVIDEO_KMSDRM=OFF -DVIDEO_KMSDRM_SHARED=OFF -DALSA=OFF -DPULSEAUDIO=OFF -DJACK=OFF \
  -DESD=OFF -DARTS=OFF -DNAS=OFF -DSNDIO=OFF -DVIDEO_WAYLAND=OFF -DVIDEO_X11=OFF \
  -DVIDEO_OPENGL=OFF -DVIDEO_OPENGLES=OFF -DVIDEO_VULKAN=OFF -DVIDEO_RPI=OFF \
  -DVIDEO_VIVANTE=OFF -DVIDEO_DIRECTFB=OFF
make -j"$(nproc)" && make install

# 2. SDL2_mixer without modplug (Ogg through stb_vorbis, WAV only).
mkdir -p "$OUT/mixer-build" && cd "$OUT/mixer-build"
cmake "$MIXER_SRC" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_INSTALL_PREFIX="$MIX" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DSDL2MIXER_VENDORED=ON \
  -DSDL2MIXER_DEPS_SHARED=OFF -DSDL2MIXER_VORBIS=STB -DSDL2MIXER_FLAC=OFF \
  -DSDL2MIXER_MOD=OFF -DSDL2MIXER_MP3=OFF -DSDL2MIXER_MIDI=OFF -DSDL2MIXER_OPUS=OFF \
  -DSDL2MIXER_CMD=OFF -DSDL2MIXER_WAVE=ON -DSDL2MIXER_SAMPLES=OFF -DSDL2MIXER_INSTALL=ON
make -j"$(nproc)" && make install

# 3. The game runtime (built at -Ofast by the project's own CMakeLists.txt).
mkdir -p "$OUT/sorr-build" && cd "$OUT/sorr-build"
cmake "$SRC" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_MODULE_PATH="$SRC/sf3500/cmake" \
  -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=OFF \
  -DSDL2_INCLUDE_DIR="$SDL/include/SDL2" -DSDL2_LIBRARY="$SDL/lib/libSDL2.a" \
  -DSDL2_MIXER_INCLUDE_DIR="$MIX/include/SDL2" -DSDL2_MIXER_LIBRARY="$MIX/lib/libSDL2_mixer.a"
make -j"$(nproc)"
"$SF3500_TC/bin/mips-mti-linux-gnu-strip" -o "$OUT/bgdi.mipsel" sorr
echo "Built: $OUT/bgdi.mipsel"
