/* Real audio via driver_sf3500.so's sound_driver_* functions - see hwaudio.c
 * for the calling-convention and clean-cycle fixes (confirmed from
 * picoarch's own production source).
 *
 * Playback runs on a DEDICATED background thread, mirroring picoarch's own
 * architecture (sf3000_audio_thread_fn in plat_sdl.c): a ring buffer the
 * game loop writes into non-blocking, drained in steady 480-frame/10ms
 * chunks by a thread that wakes on its own schedule. Confirmed real-hardware
 * reason this is required, not just a nicety: calling sound_driver_playframe
 * synchronously from the main game loop (which has zero frame-pacing of its
 * own - no SDL_Delay/vsync-wait anywhere in this port) produced audible
 * stutter/muffling - irregular feed timing into a real-time hardware sink.
 * The dedicated thread decouples playback timing from however fast/slow the
 * game loop happens to run. */
#ifndef SF3000_HWAUDIO_H
#define SF3000_HWAUDIO_H

#define HWAUDIO_RATE  48000
#define HWAUDIO_CHUNK 480   /* frames (10ms @ 48000Hz), fixed by the driver */

/* Ring sizing - the SINGLE shared source of truth for both hwaudio.c's own
 * buffer and every caller's backlog-cap math (SoundStream.cpp, audiotest.c).
 * Previously these were sized independently in different files with no
 * shared knowledge of each other's real values - found on real hardware
 * (2026-10-09, five rounds of testing on TinySTG) that this is exactly how
 * a "fixed" backlog cap kept reintroducing the same bug at a smaller scale:
 * each cap was arbitrary relative to what the ring could actually hold, so
 * the gap between "requested" and "accepted" never settled near zero, it
 * just oscillated at whatever the latest guessed cap size was. A single
 * port-agnostic definition here means every future port's SoundStream-
 * equivalent sizes its own backlog cap FROM this, not from a second guess.
 *
 * HWAUDIO_RING_FRAMES is deliberately generous (2 real seconds) rather than
 * tightly tuned to the "ideal" drain rate: the producer and consumer are
 * paced by two independent real-time clocks (CLOCK_MONOTONIC vs
 * nanosleep(10ms)) that will never match exactly, so SOME slack has to
 * exist for the honest accepted-count feedback loop to actually converge
 * instead of perpetually hitting a cap that's too close to the real
 * drain rate to recover from. HWAUDIO_RING_SOFT_CAP leaves 25% headroom
 * below the hard array size, so a genuine worst-case burst still has room
 * before hwaudio_write()'s own drop-the-rest-of-this-call behavior kicks in. */
/* Must stay a power of two - hwaudio.c masks the ring index with
 * (HWAUDIO_RING_FRAMES - 1) instead of a slower modulo. 65536 frames is
 * ~1.37 real seconds at 48000Hz - not a round "2 seconds", chosen instead
 * for the nearest convenient power of two comfortably above that target. */
#define HWAUDIO_RING_FRAMES    65536
#define HWAUDIO_RING_SOFT_CAP  (HWAUDIO_RING_FRAMES * 3 / 4)

#ifdef __cplusplus
extern "C" {
#endif

int  hwaudio_init(void);
int  hwaudio_active(void);

/* Non-blocking: copies into the ring buffer and returns immediately. Safe to
 * call from the main game loop regardless of its own timing. May accept
 * fewer than `frames` if the ring already has enough cushion ahead of the
 * consumer - returns the number ACTUALLY accepted. Callers that track their
 * own cumulative output total (e.g. SoundStream's drift-free accounting)
 * MUST advance by the return value, not the requested count, or dropped
 * frames silently corrupt that bookkeeping. */
int hwaudio_write(const short *buf, int frames);

void hwaudio_deinit(void);

/* Diagnostic snapshot - cumulative counters since hwaudio_init(), plus the
 * ring's current fill level (frames queued ahead of the consumer). Any
 * pointer may be NULL. Used to tell apart overflow-drop crackle (dropped
 * climbing, fill pinned near HWAUDIO_RING_SOFT_CAP) from underrun-insert
 * crackle (underruns climbing, fill near 0) instead of guessing. */
void hwaudio_get_stats(unsigned *underruns, unsigned *dropped, unsigned *fill);

#ifdef __cplusplus
}
#endif

#endif
