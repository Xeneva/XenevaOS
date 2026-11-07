#ifndef _CUSTOM_BTN_H
#define _CUSTOM_BTN_H

#include <widgets/base.h>
#include <widgets/window.h>

typedef struct _CustomBtn {
    ChWidget base;
    const char* text;
    uint32_t colorSquare;
    bool isColorBtn;
    uint32_t normalBg;
    uint32_t hoverBg;
    uint32_t clickedBg;
    uint32_t textColor;
    uint32_t borderColor;
} CustomBtn;

CustomBtn* CreateCustomBtn(int x, int y, int w, int h, const char* text, uint32_t colorSq, bool isCol);
CustomBtn* CreateStyledBtn(int x, int y, int w, int h, const char* text, uint32_t normalBg, uint32_t hoverBg, uint32_t clickedBg, uint32_t textColor, uint32_t borderColor);

#endif
