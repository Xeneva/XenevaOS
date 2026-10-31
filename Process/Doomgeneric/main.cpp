/**
 * Doomgeneric main entry point for XenevaOS.
 *
 * Boots the full Doom engine (doomgeneric_Create -> D_DoomMain) with the
 * IWAD shipped in the initrd, then pumps frames via doomgeneric_Tick().
 * The initrd packer places Resources/resources/* at the image root, so the
 * WAD is visible as /doom2.wad at runtime.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <_xeneva.h>
#include <sys/_keproc.h>

#include "doomgeneric.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void doomgeneric_Create(int argc, char **argv);
extern void doomgeneric_Tick();
extern void DG_SleepMs(uint32_t ms);
#ifdef __cplusplus
}
#endif

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    _KePrint("[doom] MARK main entry\n");
    printf("Doomgeneric for XenevaOS starting...\n");

    // Fixed argv: engine gets the IWAD path explicitly so it never
    // depends on CWD or registry probing (d_iwad Windows paths are
    // compiled out for AArch64 -- see d_iwad.c guard).
    static char *doom_argv[] = {
        (char *)"doomgeneric",
        (char *)"-iwad",
        (char *)"/doom2.wad",
        NULL
    };
    int doom_argc = 3;

    _KePrint("[doom] MARK before Create\n");
    doomgeneric_Create(doom_argc, doom_argv);
    _KePrint("[doom] MARK Create returned (D_DoomMain done? unexpected)\n");

    printf("Entering Doom main loop...\n");
    while (1) {
        doomgeneric_Tick();
        // Let the scheduler breathe; game timing itself is driven by
        // I_GetTime/DG_GetTicksMs, not by this sleep.
        DG_SleepMs(5);
    }

    return 0;
}
