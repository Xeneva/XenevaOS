//
// w_file_stdc.c -- standard C WAD file I/O for XenevaOS doomgeneric port.
//
// Implements wad_file_class_t (stdc_wad_file) on top of libc stdio,
// matching the upstream Chocolate Doom implementation. No mmap is used
// (HAVE_MMAP is unset in config.h), so W_OpenFile always takes this path.
//
// This file was missing from the port sources (only w_file.c, which
// references stdc_wad_file via extern, was present), causing an
// undefined-symbol link failure once the full engine is compiled in.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "w_file.h"
#include "z_zone.h"

extern wad_file_class_t stdc_wad_file;

typedef struct
{
    wad_file_t wad;
    FILE *fstream;
} stdc_wad_file_t;

static wad_file_t *W_StdC_OpenFile(char *path)
{
    stdc_wad_file_t *result;
    FILE *fstream;

    fstream = fopen(path, "rb");
    if (fstream == NULL)
    {
        return NULL;
    }

    // Create a new stdc_wad_file_t to hold the file handle.

    result = Z_Malloc(sizeof(stdc_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = 0;
    result->fstream = fstream;

    // Obtain file size.

    fseek(fstream, 0, SEEK_END);
    result->wad.length = (unsigned int)ftell(fstream);
    fseek(fstream, 0, SEEK_SET);

    return &result->wad;
}

static void W_StdC_CloseFile(wad_file_t *wad)
{
    stdc_wad_file_t *stdc_wad;

    stdc_wad = (stdc_wad_file_t *)wad;

    fclose(stdc_wad->fstream);
    Z_Free(stdc_wad);
}

// Read data from the specified position in the file into the provided
// buffer. Returns the number of bytes read.

static size_t W_StdC_Read(wad_file_t *wad, unsigned int offset,
                          void *buffer, size_t buffer_len)
{
    stdc_wad_file_t *stdc_wad;
    size_t result;

    stdc_wad = (stdc_wad_file_t *)wad;

    // Jump to the specified position in the file.

    fseek(stdc_wad->fstream, (long)offset, SEEK_SET);

    // Read into the buffer.

    result = fread(buffer, 1, buffer_len, stdc_wad->fstream);

    return result;
}

wad_file_class_t stdc_wad_file =
{
    W_StdC_OpenFile,
    W_StdC_CloseFile,
    W_StdC_Read,
};
