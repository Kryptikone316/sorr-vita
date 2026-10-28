#include "hwaudio.h"
#include "tfenv.h"

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>

static void  *g_handle = NULL;
static int    g_active = 0;

typedef int (*fn_init_t)(void *device_name, int sample_rate, int channels);
typedef int (*fn_play_t)(const void *buffer, int frame_count);
typedef int (*fn_deinit_t)(void);

static fn_init_t   p_init   = NULL;
static fn_play_t   p_play   = NULL;
static fn_deinit_t p_deinit = NULL;

/* Ring buffer (frames), written by the game loop (any timing), drained by
 * our own steady-paced thread. Free-running frame counters, masked by
 * HWAUDIO_RING_FRAMES (power of two, defined in hwaudio.h - see its comment
 * for why the size and soft cap live there, shared with every caller,
 * instead of being guessed independently per file like the earlier,
 * repeatedly-wrong attempts at this same fix). */
#define RING_MASK (HWAUDIO_RING_FRAMES - 1)
static short ring[HWAUDIO_RING_FRAMES * 2];
static unsigned ring_w = 0, ring_r = 0;
static pthread_mutex_t ring_mtx = PTHREAD_MUTEX_INITIALIZER;

static pthread_t audio_thread;
static volatile int thread_running = 0;

/* Diagnostic counters - cumulative, read by hwaudio_get_stats() so a
 * caller (libsdlhandler's once/sec hook) can log them without touching
 * the ring itself. Not reset between reads; caller computes deltas. */
static volatile unsigned g_underrun_count = 0;
static volatile unsigned g_dropped_frames = 0;

/* Ramp state for the underrun fade below - persists across calls so a
 * ramp-down started by one underrun picks up where the last real (or
 * faded) sample left off, and a ramp-up on recovery starts from silence
 * rather than jumping straight to full amplitude. */
static short g_last_l = 0, g_last_r = 0;
static int   g_was_underrun = 0;

static void *audio_thread_fn(void *unused) {
    struct timespec next_wake;
    long period_ns;
    short chunk[HWAUDIO_CHUNK * 2];
    (void)unused;
    /* 32-bit long overflows computing 480 * 1e9 directly on this target -
     * confirmed by the compiler's own overflow warning. long long avoids it;
     * the final result (~10,000,000ns) fits a 32-bit tv_nsec field fine. */
    period_ns = (long)((long long)HWAUDIO_CHUNK * 1000000000LL / HWAUDIO_RATE); /* 10ms */

    /* Absolute-deadline pacing, not a plain nanosleep(10ms) every iteration.
     * audiotest found (2026-10-09, real hardware) a genuine, steady ring
     * growth even after the ring itself was made far bigger - roughly 464
     * extra frames/sec accumulating, confirming the consumer's REAL average
     * tick period was running slightly longer than the nominal 10ms. A
     * relative nanosleep(&delay) re-bases its sleep off "now" every
     * iteration, so this loop's own overhead (the mutex lock, the 480-frame
     * copy, and critically p_play() - a real call into the closed-source
     * driver that is NOT instantaneous) all compound into the period
     * instead of being measured against a fixed schedule. Scheduling
     * against an absolute deadline that only ever advances by a fixed
     * period - irrespective of how long the loop body took - cancels that
     * compounding out, the standard fix for a periodic POSIX thread. */
    clock_gettime(CLOCK_MONOTONIC, &next_wake);

    while (thread_running) {
        next_wake.tv_nsec += period_ns;
        if (next_wake.tv_nsec >= 1000000000L) {
            next_wake.tv_nsec -= 1000000000L;
            next_wake.tv_sec += 1;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wake, NULL);

        pthread_mutex_lock(&ring_mtx);
        if (ring_w - ring_r >= (unsigned)HWAUDIO_CHUNK) {
            int i;
            for (i = 0; i < HWAUDIO_CHUNK; i++) {
                unsigned idx = (ring_r + i) & RING_MASK;
                chunk[i * 2]     = ring[idx * 2];
                chunk[i * 2 + 1] = ring[idx * 2 + 1];
            }
            ring_r += HWAUDIO_CHUNK;

            /* Coming back from an underrun: ramp 0 -> real amplitude over
             * this chunk instead of jumping straight to full volume - the
             * jump itself is an audible click/crackle (a hard discontinuity
             * in the waveform), same reasoning as the ramp-down below. Q8
             * fixed-point, no float needed for a 480-sample linear ramp. */
            if (g_was_underrun) {
                for (i = 0; i < HWAUDIO_CHUNK; i++) {
                    int scale = (i * 256) / HWAUDIO_CHUNK;
                    chunk[i * 2]     = (short)((chunk[i * 2]     * scale) >> 8);
                    chunk[i * 2 + 1] = (short)((chunk[i * 2 + 1] * scale) >> 8);
                }
                g_was_underrun = 0;
            }
            g_last_l = chunk[(HWAUDIO_CHUNK - 1) * 2];
            g_last_r = chunk[(HWAUDIO_CHUNK - 1) * 2 + 1];
        } else {
            /* Underrun: ramp DOWN from the last real sample to silence
             * instead of jumping straight to a flat zero - a hard jump from
             * whatever amplitude the music was at down to zero is itself an
             * audible click, repeated ~15-20x/sec this was almost certainly
             * most of the reported crackle (confirmed dropped=0 throughout
             * via hwaudio_get_stats() - this is a smoothing fix for the
             * underrun's audible edge, not a fix for the underrun rate
             * itself; see sorr_handoff project memory for why buffer-size
             * tuning didn't touch the underrun rate and was reverted). */
            int i;
            for (i = 0; i < HWAUDIO_CHUNK; i++) {
                int scale = 256 - (i * 256) / HWAUDIO_CHUNK;
                chunk[i * 2]     = (short)((g_last_l * scale) >> 8);
                chunk[i * 2 + 1] = (short)((g_last_r * scale) >> 8);
            }
            g_was_underrun = 1;
            g_last_l = 0;
            g_last_r = 0;
            g_underrun_count++;
        }
        pthread_mutex_unlock(&ring_mtx);

        p_play(chunk, HWAUDIO_CHUNK);
    }
    return NULL;
}

/* Retrying init (confirmed pattern from picoarch: the driver can transiently
 * fail right after boot/process handoff; retry up to 20 times, 100ms apart). */
static int try_init(void) {
    int rc = -1;
    int try;
    for (try = 0; try < 20; try++) {
        rc = p_init(NULL, HWAUDIO_RATE, 2);
        if (rc >= 0) break;
        usleep(100 * 1000);
    }
    return rc;
}

int hwaudio_init(void) {
    if (g_active) return 0;

    g_handle = dlopen(tfdriver_path(), RTLD_NOW | RTLD_GLOBAL);
    if (!g_handle) {
        fprintf(stderr, "hwaudio: dlopen %s failed: %s\n", tfdriver_path(), dlerror());
        return -1;
    }

    p_init   = (fn_init_t)  dlsym(g_handle, "sound_driver_init");
    p_play   = (fn_play_t)  dlsym(g_handle, "sound_driver_playframe");
    p_deinit = (fn_deinit_t)dlsym(g_handle, "sound_driver_deinit");

    if (!p_init || !p_play) {
        fprintf(stderr, "hwaudio: dlsym failed (init=%p play=%p)\n", (void*)p_init, (void*)p_play);
        dlclose(g_handle); g_handle = NULL;
        return -1;
    }

    /* Clean-cycle workaround (confirmed from picoarch): first init can report
     * success while leaving the hardware half-alive - open, close, re-open. */
    if (try_init() < 0) {
        fprintf(stderr, "hwaudio: initial sound_driver_init failed\n");
        dlclose(g_handle); g_handle = NULL;
        return -1;
    }
    fprintf(stderr, "hwaudio: clean AUDDEC/I2SO startup cycle\n");
    if (p_deinit) p_deinit();
    usleep(100 * 1000);

    if (try_init() < 0) {
        fprintf(stderr, "hwaudio: second sound_driver_init failed\n");
        dlclose(g_handle); g_handle = NULL;
        return -1;
    }

    ring_w = ring_r = 0;
    thread_running = 1;
    {
        /* Device is single-core (confirmed) - without a scheduling guarantee,
         * heavier game logic (real gameplay vs the menu) can delay this
         * thread's 10ms wake-up past what the ring buffer's small cushion can
         * absorb, confirmed real-hardware symptom: fine in the menu, stutters
         * once gameplay starts. SCHED_FIFO makes the kernel always preempt
         * the normal-priority game thread for this one instead of leaving it
         * to best-effort time-slicing. Falls back to default scheduling if
         * this process lacks the privilege (logged, not fatal - embedded
         * Linux here runs as root, so expected to succeed). */
        pthread_attr_t attr;
        struct sched_param sp;
        int created;

        pthread_attr_init(&attr);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        sp.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
        pthread_attr_setschedparam(&attr, &sp);

        created = pthread_create(&audio_thread, &attr, audio_thread_fn, NULL);
        if (created != 0) {
            fprintf(stderr, "hwaudio: SCHED_FIFO thread create failed (%d), falling back to default scheduling\n", created);
            created = pthread_create(&audio_thread, NULL, audio_thread_fn, NULL);
        }
        pthread_attr_destroy(&attr);

        if (created != 0) {
            fprintf(stderr, "hwaudio: failed to start audio thread\n");
            thread_running = 0;
            if (p_deinit) p_deinit();
            dlclose(g_handle); g_handle = NULL;
            return -1;
        }
    }

    g_active = 1;
    fprintf(stderr, "hwaudio: active (rate=%d chunk=%d, dedicated thread)\n", HWAUDIO_RATE, HWAUDIO_CHUNK);
    return 0;
}

int hwaudio_active(void) { return g_active; }

/* Returns the number of frames actually accepted (<= frames) - the soft cap
 * can silently accept fewer than requested, and the caller's own drift-free
 * output accounting (SoundStream.cpp) MUST advance by the real accepted
 * count, not the requested one, or dropped frames permanently corrupt that
 * bookkeeping (found 2026-10-09: the old void-returning version let
 * out_frames_produced silently drift ahead of what the ring actually got,
 * compounding into exactly the kind of systematic shortfall the throughput
 * fix was supposed to eliminate). */
int hwaudio_write(const short *buf, int frames) {
    int i;
    if (!g_active) return 0;

    pthread_mutex_lock(&ring_mtx);
    for (i = 0; i < frames; i++) {
        if (ring_w - ring_r >= HWAUDIO_RING_SOFT_CAP) {
            /* Already have enough cushion ahead of the consumer - drop the
             * rest of this call's frames instead of letting the ring grow
             * toward HWAUDIO_RING_FRAMES and eventually chop a whole chunk
             * at once. */
            break;
        }
        unsigned idx = ring_w & RING_MASK;
        ring[idx * 2]     = buf[i * 2];
        ring[idx * 2 + 1] = buf[i * 2 + 1];
        ring_w++;
    }
    if (i < frames) g_dropped_frames += (frames - i);
    pthread_mutex_unlock(&ring_mtx);
    return i;
}

void hwaudio_get_stats(unsigned *underruns, unsigned *dropped, unsigned *fill) {
    pthread_mutex_lock(&ring_mtx);
    if (underruns) *underruns = g_underrun_count;
    if (dropped)   *dropped   = g_dropped_frames;
    if (fill)       *fill      = ring_w - ring_r;
    pthread_mutex_unlock(&ring_mtx);
}

void hwaudio_deinit(void) {
    if (!g_active) return;
    thread_running = 0;
    pthread_join(audio_thread, NULL);
    if (p_deinit) p_deinit();
    if (g_handle) { dlclose(g_handle); g_handle = NULL; }
    p_init = NULL; p_play = NULL; p_deinit = NULL;
    g_active = 0;
}
