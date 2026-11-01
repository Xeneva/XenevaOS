/**
* BSD 2-Clause License
*
* Copyright (c) 2026, Kheersagar Patel (Knowivate)
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#ifndef __SOLITAIRE_H__
#define __SOLITAIRE_H__

#include <stdint.h>
#include <stdbool.h>

#define CARD_SUIT_HEARTS   0
#define CARD_SUIT_DIAMONDS 1
#define CARD_SUIT_CLUBS    2
#define CARD_SUIT_SPADES   3

#define CARD_RANK_ACE   1
#define CARD_RANK_JACK  11
#define CARD_RANK_QUEEN 12
#define CARD_RANK_KING  13

#define PILE_NONE       0
#define PILE_STOCK      1
#define PILE_WASTE      2
#define PILE_FOUNDATION 3
#define PILE_TABLEAU    4

#define CARD_WIDTH  75
#define CARD_HEIGHT 105
#define TABLEAU_COUNT 7
#define FOUNDATION_COUNT 4
#define TOTAL_CARDS 52

typedef struct _card_ {
    int suit;     // 0: Hearts, 1: Diamonds, 2: Clubs, 3: Spades
    int rank;     // 1 (Ace) .. 13 (King)
    bool faceUp;
} Card;

typedef struct _card_stack_ {
    Card cards[TOTAL_CARDS];
    int count;
} CardStack;

typedef struct _cascade_card_ {
    float x, y;
    float vx, vy;
    Card card;
    bool active;
} CascadeCard;

typedef enum _splash_state_ {
    SPLASH_HOLD = 0,
    SPLASH_FADE_OUT,
    SPLASH_DONE
} SplashState;

#define MAX_RECYCLES 6

typedef struct _solitaire_game_ {
    CardStack stock;
    CardStack waste;
    CardStack foundations[FOUNDATION_COUNT];
    CardStack tableau[TABLEAU_COUNT];
    
    // Selection state
    int selectedPileType;
    int selectedPileIndex;
    int selectedCardIndex;

    // Game statistics & states
    int moveCount;
    int score;
    int timeElapsedSec;
    int recycleCount;
    bool isWon;
    bool autoSolved;

    // Splash screen state
    SplashState splashState;
    int splashTimer;
    float splashAlpha;

    // Victory cascade animation
    bool celebrationActive;
    CascadeCard cascadeCards[TOTAL_CARDS];
    int cascadeNextIndex;
    int cascadeTimer;

    // Initial seed for restart
    uint32_t initialSeed;
} SolitaireGame;

extern SolitaireGame game;

void SolitaireInit(uint32_t seed);
void SolitaireRestart();
void SolitaireStockClick();
bool SolitaireCanMoveToFoundation(Card card, int fIdx);
bool SolitaireCanMoveToTableau(Card card, int tIdx);
bool SolitaireMoveCards(int srcType, int srcIdx, int srcCardIdx, int dstType, int dstIdx);
bool SolitaireAutoMoveToFoundation(int srcType, int srcIdx, int srcCardIdx);
bool SolitaireCheckWin();
void SolitaireUpdateTimerTick();
void SolitaireUpdateCascade();
void SolitaireUpdateSplash();
bool SolitaireCanUndo();
bool SolitaireUndo();

static inline bool CardIsRed(int suit) {
    return suit == CARD_SUIT_HEARTS || suit == CARD_SUIT_DIAMONDS;
}

static inline const char* CardRankStr(int rank) {
    switch (rank) {
        case 1:  return "A";
        case 2:  return "2";
        case 3:  return "3";
        case 4:  return "4";
        case 5:  return "5";
        case 6:  return "6";
        case 7:  return "7";
        case 8:  return "8";
        case 9:  return "9";
        case 10: return "10";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        default: return "?";
    }
}

#endif
