#include <stdio.h>
#include <stdlib.h>
#include <_xeneva.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	_KePrint("[doom] MARK Create: FindResponseFile\n");
	M_FindResponseFile();

	_KePrint("[doom] MARK Create: alloc screenbuf\n");
	DG_ScreenBuffer = (pixel_t*)malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);

	_KePrint("[doom] MARK Create: DG_Init\n");
	DG_Init();

	_KePrint("[doom] MARK D_DoomMain start\n");
	_KePrint("[doom] MARK Create: D_DoomMain\n");
	_KePrint("[doom] MARK after ChWindowPaint\n");
	D_DoomMain ();
	_KePrint("[doom] MARK Create: D_DoomMain returned\n");
}

