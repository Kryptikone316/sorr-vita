/* sf3000-hwdisp implementation. dlopen's driver.so, calls video_driver_*.
 * Lifted near-verbatim from TreeFrogUI's pico286-sf3000.patch - see hwdisp.h.
 *
 * Driver scales src dims -> panel (854x480) via HCGE DMA with bilinear filter.
 * We use it unmodified (no nearest-upscale, no aspect pad) for Manic Miner:
 * source is a fixed 256x192 buffer, straight passthrough is enough.
 */

#include "hwdisp.h"
#include "tfenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define HW_W   1280
#define HW_H    720
#define HW_PITCH (HW_W * 2)
#define HW_BUFSZ (HW_W * HW_H * 2)

static void   *g_handle = NULL;
static int     g_active = 0;

typedef int  (*fn_init_t)(void);
typedef void (*fn_deinit_t)(void);
typedef int  (*fn_disp_t)(void *src, int w, int h, int pitch);

static fn_init_t   p_init   = NULL;
static fn_deinit_t p_deinit = NULL;
static fn_disp_t   p_disp   = NULL;

static unsigned short *g_pad_buf  = NULL;
static int       g_pad_cap  = 0;
static int       g_pad_w    = 0;
static int       g_pad_h    = 0;

static unsigned short *g_near_buf = NULL;

static int g_aspect_num = 0;
static int g_aspect_den = 0;
static int g_filter_nearest = 0;

int hwdisp_init(void) {
    if (g_active) return 0;

    /* driver.so itself is encrypted at rest on this device (confirmed: high
     * entropy bytes, no ELF header) - exactly what zhijack.sh's own ELF-magic
     * check on driver.so is testing for. The real usable library on SF3500
     * is driver_sf3500.so (confirmed real ELF, exports video_drivers_init/
     * video_driver_deinit/video_driver_disp_frame - verified via nm -D). */
    tfenv_log_device("hwdisp");
    g_handle = dlopen(tfdriver_path(), RTLD_NOW | RTLD_GLOBAL);
    if (!g_handle) {
        fprintf(stderr, "hwdisp: dlopen %s failed: %s\n", tfdriver_path(), dlerror());
        return -1;
    }

    p_init   = (fn_init_t)  dlsym(g_handle, "video_drivers_init");
    p_deinit = (fn_deinit_t)dlsym(g_handle, "video_driver_deinit");
    p_disp   = (fn_disp_t)  dlsym(g_handle, "video_driver_disp_frame");

    if (!p_init || !p_deinit || !p_disp) {
        fprintf(stderr, "hwdisp: dlsym failed (init=%p deinit=%p disp=%p)\n",
                (void*)p_init, (void*)p_deinit, (void*)p_disp);
        dlclose(g_handle); g_handle = NULL;
        return -1;
    }

    int rv = p_init();
    if (rv <= 0) {
        fprintf(stderr, "hwdisp: video_drivers_init returned %d\n", rv);
        dlclose(g_handle); g_handle = NULL;
        return -1;
    }

    g_active = 1;
    fprintf(stderr, "hwdisp: HW path active (init rv=%d)\n", rv);
    return 0;
}

int hwdisp_active(void) { return g_active; }

void hwdisp_set_target_aspect(int num, int den) {
    g_aspect_num = num;
    g_aspect_den = den;
}

void hwdisp_set_filter(int nearest) {
    g_filter_nearest = nearest ? 1 : 0;
    if (g_filter_nearest && !g_near_buf) {
        g_near_buf = (unsigned short*)malloc(HW_BUFSZ);
        if (g_near_buf) memset(g_near_buf, 0, HW_BUFSZ);
    }
}

static void pad_horizontal(const void *src, int w, int h, int pitch_bytes, int pad_w) {
    int need = pad_w * h;
    if (need > g_pad_cap) {
        free(g_pad_buf);
        g_pad_cap = need + 4096;
        g_pad_buf = (unsigned short*)malloc(g_pad_cap * sizeof(unsigned short));
        g_pad_h   = 0;
        g_pad_w   = 0;
    }
    if (!g_pad_buf) return;

    int off_x = (pad_w - w) / 2;
    if (off_x < 0) off_x = 0;

    if (pad_w != g_pad_w || h != g_pad_h) {
        memset(g_pad_buf, 0, (size_t)pad_w * h * sizeof(unsigned short));
        g_pad_w = pad_w;
        g_pad_h = h;
    }

    for (int y = 0; y < h; y++) {
        const unsigned short *srow = (const unsigned short *)((const char *)src + y * pitch_bytes);
        unsigned short *drow = g_pad_buf + (size_t)y * pad_w + off_x;
        memcpy(drow, srow, (size_t)w * sizeof(unsigned short));
    }
}

void hwdisp_present(const void *src, int w, int h, int pitch_bytes) {
    if (!g_active || !p_disp || !src) return;

    if (g_filter_nearest && g_near_buf) {
        /* not used by Manic Miner currently - keep stub for parity */
        p_disp(g_near_buf, HW_W, HW_H, HW_PITCH);
        return;
    }

    if (g_aspect_num <= 0 || g_aspect_den <= 0) {
        p_disp((void *)src, w, h, pitch_bytes);
        return;
    }

    int pad_w = h * g_aspect_num / g_aspect_den;
    if (pad_w <= w) {
        p_disp((void *)src, w, h, pitch_bytes);
        return;
    }

    pad_horizontal(src, w, h, pitch_bytes, pad_w);
    if (!g_pad_buf) {
        p_disp((void *)src, w, h, pitch_bytes);
        return;
    }
    p_disp(g_pad_buf, pad_w, h, pad_w * 2);
}

void hwdisp_deinit(void) {
    if (!g_active) return;
    if (p_deinit) p_deinit();
    if (g_pad_buf) { free(g_pad_buf); g_pad_buf = NULL; g_pad_cap = 0; g_pad_w = 0; g_pad_h = 0; }
    if (g_near_buf) { free(g_near_buf); g_near_buf = NULL; }
    if (g_handle) { dlclose(g_handle); g_handle = NULL; }
    p_init = NULL; p_deinit = NULL; p_disp = NULL;
    g_active = 0;
}
