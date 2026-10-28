/* sf3000-hwdisp — hardware display via driver.so + HCGE.
 * Wraps SF3000/SF3500 HiChip Graphics Engine for rotation+scale+present in
 * HW. Falls back transparently if driver.so unavailable.
 *
 * Lifted near-verbatim from TreeFrogUI's own pico286-sf3000.patch
 * (port_sf3000/hwdisp.c) - the same technique libretro cores use on this
 * device, confirmed via TreeFrogUI's docs/display-scaling.md ("SF3000 and
 * SF3500 use a rotated 854x480 driver"). This is the real hardware rotation
 * path; SDL1.2's own SDL_VIDEO_FBCON_ROTATION renders fully black on this
 * device (confirmed on real hardware, both CW and CCW) - not worth chasing
 * further when the vendor's own working mechanism is right here. */
#ifndef SF3000_HWDISP_H
#define SF3000_HWDISP_H

#include <stddef.h>
/* Not <stdint.h> on purpose: its uintptr_t typedef conflicts with SDL1.2's
 * own SDL_config_minimal.h on this target (same width, different spelling -
 * a real C redeclaration error). We only need 16-bit pixels; `unsigned
 * short` is guaranteed 16-bit on this MIPS o32 target without the clash. */

#ifdef __cplusplus
extern "C" {
#endif

int  hwdisp_init(void);
int  hwdisp_active(void);
void hwdisp_set_target_aspect(int num, int den);
void hwdisp_set_filter(int nearest);
void hwdisp_present(const void *src, int w, int h, int pitch_bytes);
void hwdisp_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
