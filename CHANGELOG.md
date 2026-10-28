# SF3500 port changelog

Tracks what changed, file-by-file, across debugging/porting sessions. Each
entry here is a pointer - the real "why" and "what not to break" detail
lives as a comment at the exact line in the source file, not duplicated
here. This file exists so a future session (or a future you) can find
*which* file to go read before touching something, without re-deriving it
from scratch. See also the `sorr_handoff` project memory for the full
narrative version of the same history.

Log evidence referenced below lives in
`C:\Users\krypt\AppData\Local\Temp\sorr_logs<N>\log.txt` (Windows side) -
each pull is a separate numbered folder, timestamped at pull time.

## 2026-10-11 session

**Starting state**: game hung on real hardware with real audio enabled;
previous session had disabled audio entirely just to get a stable build.

1. **SIGILL root-caused and fixed** (sorr_logs18-21, ~02:40-03:08)
   - Added a register dump (`ra`/`t9`/`sp`/`gp`/`v0`/`a0`) to
     `core/bgdi/src/main.c`'s `sf3500_crash_handler()`, on top of the
     existing PC/maps dump. `t9 == PC` at fault confirmed a MIPS PIC
     indirect call landing on garbage; `ra` resolved via `addr2line` to
     `stb_vorbis_open_rwops`.
   - **Real fix**: `modules/mod_sound/mod_sound.c`'s `SDL_RWFromBGDFP()`
     never set `rwops->size` (only `seek`/`read`/`write`/`close`).
     `SDL_AllocRW()` doesn't zero the struct, so it was leftover heap
     garbage - invisible for every `.wav` load (never calls
     `SDL_RWsize()`), fatal for OGG music load (`stb_vorbis_open_rwops`
     calls `SDL_RWsize()` unconditionally). Added `__modsound_size_cb()`.
     See the comment right above `SDL_RWFromBGDFP()` in that file.
   - **Dead end, do not re-open without new evidence**: a prior session's
     handoff suspected `core/include/monolithic_includes.h`'s two
     parallel dispatch tables (`symbol_list[]` / `symbol_list_runtime[]`)
     had drifted (`mod_m7` missing from one). Extracted and diffed both
     tables in full this session: byte-for-byte aligned, 55/55 entries.
     Not the cause of anything.

2. **Debug-instrumentation stutter - first pass** (sorr_logs22, ~03:22-03:28)
   - Removed a per-frame `write(2,...)` trace in
     `SDL2-2.0.14/src/events/sf3500/SDL_sf3500input.c`'s
     `SF3500_PumpJoykeyKeyboard()` (fired on every frame any button was
     held - up to 60 syscalls/sec). Also removed the now-confirmed-
     unnecessary `load_wav`/`play_wav` traces in `mod_sound.c` and the
     `modflic_*` traces in `modules/mod_flic/mod_flic.c` (confirmed via
     those same traces that FLI animation was never the crash cause).
   - **This fix did not actually ship until 05:33 - see item 6.**

3. **Audio ring diagnostics added** (sorr_logs23, ~03:38-03:48)
   - Added `hwaudio_get_stats(underruns, dropped, fill)` to
     `src/sf3500/hwaudio.c` / `.h`, printed once/sec via
     `modules/libsdlhandler/libsdlhandler.c`'s existing per-second hook.
     Confirmed `dropped` stayed 0 the entire session (ring never
     overflows) but `underruns` climbed steadily (~15-20/sec) with `fill`
     repeatedly dipping below one `HWAUDIO_CHUNK` - a producer-starves-
     consumer problem, not an overflow problem. **Keep this - it's cheap
     and is what actually found the real cause later (item 5).**

4. **Buffer-size tuning - both directions regressed, reverted**
   (sorr_logs24, ~03:50-03:59)
   - Tried `mod_sound.c`'s `audio_buffers` at `HWAUDIO_CHUNK*2` (960,
     ~20ms) - worse ("hiccups on a treadmill"). Tried `8192` (~170ms) -
     also worse ("slow motion stutter"). Reverted to the original
     `1024 * audio_rate / 22050` formula. **Do not re-tune this value -
     see the comment on that line, it's not the lever.**
   - Added an underrun fade (ramp to/from silence instead of a hard cut)
     to `hwaudio.c`'s `audio_thread_fn()`. Real improvement to the
     *sound* of each underrun, did not change the underrun *rate*. Kept.

5. **Real root cause found and fixed: music sample-rate mismatch**
   (sorr_logs25-27, ~04:09-05:27)
   - Added a one-time audio-spec dump and a postmix inter-call-interval
     timer (`mod_sound.c`, `SF3500_AudioPostMix()`) - ruled out a
     channel/format mismatch (confirmed `rate=48000 format=S16LSB
     channels=2`, exactly as assumed), then proved the real cause: the
     measured interval between postmix calls sat at a *consistent*
     ~58-60ms against a ~46ms nominal, for the entire session - a
     chronic CPU-bound tax, not jitter.
   - Confirmed via `ffprobe` that all of SORR's `mod/music/*.ogg` files
     are natively 44100Hz, while the hardware is pinned to 48000Hz
     (`HWAUDIO_RATE`) - SDL_mixer was running its (hint-less on this
     build - no `libsamplerate`) windowed-sinc resampler continuously
     for as long as any music played.
   - **Fix**: re-encoded all 134 files in `mod/music/` to 48000Hz via
     `ffmpeg -ar 48000 -c:a libvorbis -b:a 128k` (verified duration/
     bitrate sane, 0 mismatches after). This is an **asset change, not a
     code change** - lives in the deployed package's `mod/music/`, not
     in the original `Gamedata\SORRv52\` source the user keeps. Confirmed
     on hardware: postmix interval back to nominal ~46-47ms, underrun
     growth rate dropped sharply, user confirmed "much improved."

6. **Stale SDL2 static library - process gotcha, now fixed**
   (discovered ~05:30, fixed 05:33)
   - **The actual root cause of why item 2's stutter fix never shipped**:
     `SDL2-2.0.14` is built as its own separate CMake project producing
     `libSDL2.a`, installed into the shared sysroot
     (`buildroot/output/host/mipsel-buildroot-linux-gnu/sysroot/usr/lib/`).
     It is **not** part of `sorr-vita`'s own CMake graph. Editing a file
     under `SDL2-2.0.14/src/` and then only running `make` in
     `sorr-vita/build-mips/` silently relinks against whatever
     `libSDL2.a` was already installed - it will NOT recompile SDL2's
     own sources, no error, no warning.
   - Confirmed via `strings sorr | grep "PumpJoykeyKeyboard: bits"` - the
     string was still present in every binary built between 03:28 and
     05:33 despite the source fix being real and correct.
   - **Rule going forward**: any edit under `SDL2-2.0.14/src/` requires,
     in this order: (1) `cd SDL2-2.0.14/build-mips && make`, (2)
     `make install` (to the sysroot), (3) `cd sorr-vita/build-mips &&
     rm -f sorr && make` (the `rm -f sorr` forces a real relink - `make`
     alone may not notice the `.a` changed). Verify with `strings sorr |
     grep <whatever string you just removed/added>` before deploying -
     don't trust the build log alone.
   - Also added a guard comment directly at the fixed call site in
     `SDL_sf3500input.c` (right above the one remaining, intentionally
     one-time `write()`) so a future per-call debug trace doesn't get
     re-added there unthrottled by accident.

## Session close-out, 2026-10-11 ~05:45 - confirmed shippable

Final real-hardware test (sorr_logs28, ~43-minute session) on the build
containing both item 5 (music re-encode) and item 6 (SDL2 relink fix)
together for the first time. Verified from the log, not just by ear:
- 0 crash-handler hits.
- 0 per-frame input-trace lines (confirms item 6's fix actually shipped).
- `dropped=0` for the entire session (ring never overflows - always true
  across every variant tried, going back to item 3).
- `max_interval_ms` back to the nominal ~47ms baseline, occasional spikes
  to 58-67ms (real jitter, not the old constant ~58-60ms plateau).
- Underrun rate averaged ~11/sec over the full session (down from the
  ~15-20/sec sustained rate before item 5), each one already softened by
  item 4's fade fix rather than a hard click.

User's call: **"Seems to be fine, I guess"** → good enough to ship as-is,
fine-tuning (if any) deferred to a future session. Nothing currently
points at an obvious further fix for the residual ~11/sec - most likely
genuine scheduling jitter at this point, not a structural bug. See the
"Pending for next session" section in the `sorr_handoff` project memory
for the one open, low-priority thread (whether to strip the diagnostic
counters for a final build) if picking this back up.
