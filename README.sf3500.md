# Streets of Rage Remake runtime for the Datafrog SF3500

This branch (`sf3500`) is isage's `sorr-vita` (the BennuGD runtime that runs the
Streets of Rage Remake game files, as ported to the PS Vita), commit `b81e878`,
plus a port to the Datafrog SF3500 handheld (MIPS32r2, no GPU). Only the runtime
is here. The game itself (`SorR.dat`, `mod/`, `palettes/`) is not part of this
repository or of the port package: the player brings their own copy.

The commits on top of upstream are:

1. **SF3500 platform backend** (`src/sf3500/` and the hooks in `CMakeLists.txt`,
   `core/bgdi`, `core/common/files.c`, `modules/librender`, `modules/libsdlhandler`,
   `modules/libvideo`, `modules/mod_sound`): software video handed to the device's
   display driver, SDL_mixer's mixed output written to the device's audio driver
   (`Mix_SetPostMix`), the gamepad as synthetic keyboard state, a crash handler, and
   Select + Start to quit. The driver comes from `/tmp/tfdevice.env`.
2. **44100 Hz mixer**: the game's music is stored at 44100 Hz and the hardware only
   accepts 48000 Hz. The mixer now runs at 44100 Hz (so SDL_mixer never converts the
   music) and the output is converted with a linear interpolation.
   `SF3500_MIX_RATE=48000` restores the previous behaviour.
3. **SDL2 patch and build kit** (`sf3500/`).

## SDL2 patch

`sf3500/sdl2-2.0.14-sf3500.patch` applies to the unmodified SDL2 2.0.14 release
(`SDL2-2.0.14.tar.gz`, sha256 `d8215b571a581be1332d2106f8036fcb03d12a70bae01e20f424976d275432bc`)
and is plainly a modified version of SDL, as the zlib licence requires. It contains:

- **Dummy audio pacing** (`src/audio/SDL_audio.c`, `SDL_sysaudio.h`): the dummy audio
  driver slept a fixed time after each callback, so blocks arrived about 6% slower than
  real time (20 a second instead of 21.5) and the device's audio ring ran dry constantly.
  It now paces against absolute deadlines.
- **Divide-by-zero guard** (`src/audio/SDL_audiocvt.c`): an audio stream created with a
  zero channel count or format made later calls divide by zero (SIGFPE on MIPS).
- **Gamepad bridge** (`src/events/sf3500/`): turns the device's gamepad into keyboard
  state, which the game reads.
- **HWAUDIO driver** (`src/audio/hwaudio/`): a direct audio driver for the device. It is
  compiled in but not used (the game runs with `SDL_AUDIODRIVER=dummy`).

## Building

```sh
SF3500_TC=/path/to/toolchain/host SDL2_SRC=/path/to/SDL2-2.0.14 MIXER_SRC=/path/to/SDL2_mixer-2.6.3 \
    ./sf3500/build.sh out
```

The toolchain is a MIPS32r2 little-endian GCC (tested with Codescape MIPS MTI GCC 6.3.0)
whose sysroot has SDL2 headers, libpng and zlib. The script patches a copy of SDL2,
builds it and SDL2_mixer (Ogg through stb_vorbis, no modplug) into `out/`, then the
runtime at `-Ofast`, and writes `out/bgdi.mipsel`. The binary must keep a name starting
with `bgdi`: the engine uses that to decide that the game data is a separate file given on
the command line (`bgdi.mipsel SorR.dat`).

## Licence

Same terms as upstream (zlib-style, see `COPYING`). The SDL2 patch is under SDL's zlib
licence.
