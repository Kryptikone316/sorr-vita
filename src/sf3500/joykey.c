/* See joykey.h. Default bit table lifted from TreeFrogUI's own
 * apps/image_viewer/image_viewer.c (same device, same shared-memory reader),
 * with the same optional /mnt/sdcard/frogui/keymap.txt override so this
 * respects whatever button remapping the user has already configured. */
#include "joykey.h"

#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define KEYMAP_FILE "/mnt/sdcard/frogui/keymap.txt"

static const char *key_names[JK_COUNT] = {
    "LEFT", "RIGHT", "UP", "DOWN", "A", "B", "L1", "R1", "X", "Y", "START", "SELECT"
};
static int key_bits[JK_COUNT] = { 7, 5, 2, 3, 13, 14, 10, 11, 12, 15, 1, 0 };

static volatile unsigned int *cv_keys = NULL;

static void load_keymap(void) {
    FILE *f = fopen(KEYMAP_FILE, "r");
    char line[64], name[32];
    int bit, i;
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%31[^=]=%d", name, &bit) != 2) continue;
        for (i = 0; i < JK_COUNT; i++)
            if (strcmp(name, key_names[i]) == 0 && bit >= 0 && bit < 32) key_bits[i] = bit;
    }
    fclose(f);
}

void joykey_init(void) {
    key_t key;
    int id;
    void *mem;

    load_keymap();

    key = ftok("/tmp/joy_key", 'a');
    if (key == (key_t)-1) return;
    id = shmget(key, 4, 0666);
    if (id < 0) return;
    mem = shmat(id, NULL, 0);
    if (mem != (void *)-1) cv_keys = (volatile unsigned int *)mem;
}

unsigned int joykey_read(void) {
    unsigned int raw, out;
    int i;

    if (!cv_keys) return 0;

    raw = *cv_keys & 0xFFFFu;
    out = 0;
    for (i = 0; i < JK_COUNT; i++) {
        if (raw & (1u << key_bits[i])) out |= 1u << i;
    }
    return out;
}
