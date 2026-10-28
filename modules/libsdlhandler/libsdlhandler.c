/*
 *  Copyright © 2006-2012 SplinterGU (Fenix/Bennugd)
 *
 *  This file is part of Bennu - Game Development
 *
 *  This software is provided 'as-is', without any express or implied
 *  warranty. In no event will the authors be held liable for any damages
 *  arising from the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *     1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 *
 *     2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 *
 *     3. This notice may not be removed or altered from any source
 *     distribution.
 *
 */

#include "bgddl.h"

#include <SDL.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "joykey.h"
#include "hwdisp.h"
#include "hwaudio.h"

/* Declared inside our own SDL2 build (src/events/sf3500/SDL_sf3500input.h)
 * but not part of SDL2's normal public install set - see dump_new_events()
 * below and sorr_handoff project memory for why this exists. */
extern unsigned int SF3500_PumpJoykeyKeyboard(void);

/* ----------------------------------------------------------------- */
/* Public functions                                                  */

static void  dump_new_events()
{
    /* Remove all pendings events */

    /* We can't return -1, just return 0 (no event) on error */
    // We'll only discard events that no module knows how to handle here...
    // Otherwise some events seem to get discarded
//    SDL_FlushEvents(SDL_SYSWMEVENT, SDL_SYSWMEVENT);
//    SDL_FlushEvents(SDL_TEXTEDITING, SDL_TEXTINPUT);
//    SDL_FlushEvents(SDL_JOYDEVICEADDED, SDL_CONTROLLERDEVICEREMAPPED);
//    SDL_FlushEvents(SDL_DOLLARGESTURE, SDL_LASTEVENT);

    /* SF3500: periodic fsync + call-rate counter. A hard power-cut loses
     * anything still sitting in the filesystem's write-back cache even
     * with unbuffered stdio (write() succeeding isn't the same as the
     * bytes reaching the SD card) - confirmed this is why several
     * "device hang" logs came back completely empty despite the screen
     * visibly showing real content. Throttled to once/second by wall
     * clock, not every call, specifically because this hook may be
     * running in a tight busy-loop (the real suspect for the reported
     * CPU starvation - volume buttons still worked but "very very
     * slow") and an fsync every call would make that worse, not better.
     * The printed call count directly measures whether that's true.
     * See sorr_handoff project memory. */
    {
        static unsigned long call_count = 0;
        static time_t last_sync = 0;
        time_t now = time(NULL);
        call_count++;
        if (now != last_sync) {
            char dbg[96];
            int n = SDL_snprintf(dbg, sizeof(dbg),
                "dump_new_events: %lu calls in last ~%lds\n",
                call_count, (long)(last_sync ? (now - last_sync) : 0));
            write(2, dbg, n);

            /* SF3500: audio crackle diagnostic - tell overflow-drop apart
             * from underrun-insert instead of guessing which one is
             * producing the periodic warp/crackle reported in real
             * gameplay. See hwaudio.h's hwaudio_get_stats() comment and
             * sorr_handoff project memory. */
            {
                unsigned underruns, dropped, fill;
                hwaudio_get_stats(&underruns, &dropped, &fill);
                n = SDL_snprintf(dbg, sizeof(dbg),
                    "hwaudio: underruns=%u dropped=%u fill=%u\n",
                    underruns, dropped, fill);
                write(2, dbg, n);
            }

            fsync(2);
            call_count = 0;
            last_sync = now;
        }
    }

    /* Get new events */
    {
        unsigned int jk_bits = SF3500_PumpJoykeyKeyboard();
        /* Standing SF3500 exit combo - never trust a game's own in-built
         * quit path on this device (see sf3500_port_pipeline_recipe
         * project memory: Manic Miner's own quit menu hung on real
         * hardware, needed a power-cycle). Audio now goes through
         * Mix_SetPostMix() -> hwaudio_write() directly (not the SDL2
         * device layer, see mod_sound.c), so hwaudio_deinit() needs an
         * explicit call here too - SDL_Quit() no longer reaches it via
         * CloseDevice since the dummy driver is forced. */
        if ((jk_bits & (1u << JK_SELECT)) && (jk_bits & (1u << JK_START))) {
            hwaudio_deinit();
            hwdisp_deinit();
            SDL_Quit();
            exit(0);
        }
    }
    SDL_PumpEvents();
    SDL_JoystickUpdate();
}

/* ----------------------------------------------------------------- */
/* Funciones de inicializacion del modulo/plugin                     */

void __bgdexport( libsdlhandler, module_initialize )()
{
}

/* ----------------------------------------------------------------- */

void __bgdexport( libsdlhandler, module_finalize )()
{
}

/* ----------------------------------------------------------------- */

/* Bigest priority first execute
   Lowest priority last execute */

HOOK __bgdexport( libsdlhandler, handler_hooks )[] =
{
    { 5000, dump_new_events                   },
    {    0, NULL                              }
} ;

/* ----------------------------------------------------------------- */
