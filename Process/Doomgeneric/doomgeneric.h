#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdlib.h>
#include <stdint.h>

#ifndef DOOMGENERIC_RESX
#define DOOMGENERIC_RESX 640
#endif  // DOOMGENERIC_RESX

#ifndef DOOMGENERIC_RESY
#define DOOMGENERIC_RESY 400
#endif  // DOOMGENERIC_RESY


#ifdef CMAP256

typedef uint8_t pixel_t;

#else  // CMAP256

typedef uint32_t pixel_t;

#endif  // CMAP256


#ifdef __cplusplus
extern "C" {
#endif

extern pixel_t* DG_ScreenBuffer;

void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick();

// Frame-time stats (ms accumulation; silent until reported).
enum {
    DG_STAT_TICK = 0,
    DG_STAT_SCALE,
    DG_STAT_MEMCPY,
    DG_STAT_UPDATE,
    DG_STAT_N
};
void DG_StatBegin(int id);
void DG_StatEnd(int id);
void DG_StatReport(void);


//Implement below functions for your platform
void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);
void DG_SetWindowTitle(const char * title);
void DG_CloseWindow(void);

#ifdef __cplusplus
}
#endif

#endif //DOOM_GENERIC
