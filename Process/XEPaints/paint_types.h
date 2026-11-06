#ifndef __PAINT_TYPES_H__
#define __PAINT_TYPES_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAINT_WIN_WIDTH  780
#define PAINT_WIN_HEIGHT 540

#define CANVAS_X 78
#define CANVAS_Y 70
#define CANVAS_W 692
#define CANVAS_H 420

#define MAX_UNDO_LEVELS 8

enum PaintTool {
    TOOL_PENCIL = 0,
    TOOL_BRUSH,
    TOOL_ERASER,
    TOOL_FLOOD_FILL,
    TOOL_LINE,
    TOOL_RECTANGLE,
    TOOL_FILLED_RECTANGLE,
    TOOL_CIRCLE,
    TOOL_FILLED_CIRCLE,
    TOOL_EYEDROPPER,
    TOOL_COUNT
};

struct PaintPoint {
    int x;
    int y;
};

struct PaintRect {
    int x;
    int y;
    int w;
    int h;
};

#define COLOR_PALETTE_SIZE 24

static const uint32_t PRESET_PALETTE[COLOR_PALETTE_SIZE] = {
    // Row 1
    0xFF000000, // Black
    0xFF333333, // Dark Slate
    0xFF666666, // Dark Gray
    0xFF999999, // Medium Gray
    0xFFCCCCCC, // Light Gray
    0xFFFFFFFF, // White
    0xFFE53935, // Crimson Red
    0xFFFB8C00, // Vivid Orange
    0xFFFDD835, // Golden Yellow
    0xFF43A047, // Emerald Green
    0xFF039BE5, // Sky Blue
    0xFF1E88E5, // Royal Blue

    // Row 2
    0xFF6D4C41, // Brown
    0xFFFF8A80, // Coral
    0xFFFFB300, // Amber
    0xFF7CB342, // Lime Green
    0xFF00897B, // Teal
    0xFF00ACC1, // Cyan
    0xFF3949AB, // Indigo
    0xFF8E24AA, // Violet
    0xFFD81B60, // Deep Magenta
    0xFFFF4081, // Hot Pink
    0xFF558B2F, // Olive
    0xFF1A237E  // Deep Navy
};

#endif
