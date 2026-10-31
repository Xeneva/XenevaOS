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

#include "solitaire.h"
#include <string.h>
#include <stdlib.h>

SolitaireGame game;

typedef struct _game_snapshot_ {
    CardStack stock;
    CardStack waste;
    CardStack foundations[FOUNDATION_COUNT];
    CardStack tableau[TABLEAU_COUNT];
    int score;
    int moveCount;
    int recycleCount;
} GameSnapshot;

#define MAX_UNDO 32
static GameSnapshot undoHistory[MAX_UNDO];
static int undoCount = 0;

static void SolitaireSaveSnapshot() {
    if (undoCount < MAX_UNDO) {
        undoCount++;
    } else {
        for (int i = 0; i < MAX_UNDO - 1; i++) {
            undoHistory[i] = undoHistory[i + 1];
        }
    }
    GameSnapshot* snap = &undoHistory[undoCount - 1];
    snap->stock = game.stock;
    snap->waste = game.waste;
    for (int i = 0; i < FOUNDATION_COUNT; i++) snap->foundations[i] = game.foundations[i];
    for (int i = 0; i < TABLEAU_COUNT; i++) snap->tableau[i] = game.tableau[i];
    snap->score = game.score;
    snap->moveCount = game.moveCount;
    snap->recycleCount = game.recycleCount;
}

bool SolitaireCanUndo() {
    return undoCount > 0 && !game.isWon;
}

bool SolitaireUndo() {
    if (!SolitaireCanUndo()) return false;
    undoCount--;
    GameSnapshot* snap = &undoHistory[undoCount];
    game.stock = snap->stock;
    game.waste = snap->waste;
    for (int i = 0; i < FOUNDATION_COUNT; i++) game.foundations[i] = snap->foundations[i];
    for (int i = 0; i < TABLEAU_COUNT; i++) game.tableau[i] = snap->tableau[i];
    game.score = snap->score;
    game.moveCount = snap->moveCount;
    game.recycleCount = snap->recycleCount;

    game.selectedPileType = PILE_NONE;
    game.selectedPileIndex = -1;
    game.selectedCardIndex = -1;
    return true;
}

static uint32_t rng_state = 123456789;

static uint32_t RngNext() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static int RngRange(int min, int max) {
    if (min >= max) return min;
    return min + (int)(RngNext() % (uint32_t)(max - min + 1));
}

void SolitaireInit(uint32_t seed) {
    if (seed == 0) seed = 987654321;
    rng_state = seed;
    game.initialSeed = seed;
    undoCount = 0;

    memset(&game.stock, 0, sizeof(CardStack));
    memset(&game.waste, 0, sizeof(CardStack));
    for (int i = 0; i < FOUNDATION_COUNT; i++) {
        memset(&game.foundations[i], 0, sizeof(CardStack));
    }
    for (int i = 0; i < TABLEAU_COUNT; i++) {
        memset(&game.tableau[i], 0, sizeof(CardStack));
    }

    // Prepare 52 cards
    Card deck[TOTAL_CARDS];
    int idx = 0;
    for (int suit = 0; suit < 4; suit++) {
        for (int rank = 1; rank <= 13; rank++) {
            deck[idx].suit = suit;
            deck[idx].rank = rank;
            deck[idx].faceUp = false;
            idx++;
        }
    }

    // Fisher-Yates shuffle
    for (int i = TOTAL_CARDS - 1; i > 0; i--) {
        int j = RngRange(0, i);
        Card temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }

    // Deal to tableau (1 card in col 0, 2 in col 1, ..., 7 in col 6)
    int deckIdx = 0;
    for (int col = 0; col < TABLEAU_COUNT; col++) {
        for (int row = 0; row <= col; row++) {
            Card c = deck[deckIdx++];
            // Only top card is face-up initially
            c.faceUp = (row == col);
            game.tableau[col].cards[game.tableau[col].count++] = c;
        }
    }

    // Remaining 24 cards to stock
    while (deckIdx < TOTAL_CARDS) {
        Card c = deck[deckIdx++];
        c.faceUp = false;
        game.stock.cards[game.stock.count++] = c;
    }

    game.selectedPileType = PILE_NONE;
    game.selectedPileIndex = -1;
    game.selectedCardIndex = -1;

    game.moveCount = 0;
    game.score = 0;
    game.timeElapsedSec = 0;
    game.recycleCount = 0;
    game.isWon = false;
    game.autoSolved = false;

    // Splash screen configuration: ~100 frames hold (~1.6s at 60fps), then fade out
    game.splashState = SPLASH_HOLD;
    game.splashTimer = 100;
    game.splashAlpha = 1.0f;

    // Celebration
    game.celebrationActive = false;
    game.cascadeNextIndex = 0;
    game.cascadeTimer = 0;
    for (int i = 0; i < TOTAL_CARDS; i++) {
        game.cascadeCards[i].active = false;
    }
}

void SolitaireRestart() {
    uint32_t seed = game.initialSeed;
    SolitaireInit(seed);
    // On manual restart, bypass splash directly to game
    game.splashState = SPLASH_DONE;
    game.splashAlpha = 0.0f;
}

void SolitaireStockClick() {
    if (game.isWon) return;

    if (game.stock.count > 0) {
        SolitaireSaveSnapshot();
        // Draw 1 card from stock to waste
        Card c = game.stock.cards[--game.stock.count];
        c.faceUp = true;
        game.waste.cards[game.waste.count++] = c;
        game.moveCount++;
        game.score = (game.score > 5) ? game.score : 5;
    } else if (game.waste.count > 0) {
        if (game.recycleCount >= MAX_RECYCLES) {
            return; // 6-shuffle limit reached
        }
        SolitaireSaveSnapshot();
        game.recycleCount++;
        // Recycle waste to stock (reverse order)
        while (game.waste.count > 0) {
            Card c = game.waste.cards[--game.waste.count];
            c.faceUp = false;
            game.stock.cards[game.stock.count++] = c;
        }
        game.moveCount++;
        if (game.score >= 20) game.score -= 15;
    }

    // Deselect any current selection
    game.selectedPileType = PILE_NONE;
    game.selectedPileIndex = -1;
    game.selectedCardIndex = -1;
}

bool SolitaireCanMoveToFoundation(Card card, int fIdx) {
    if (fIdx < 0 || fIdx >= FOUNDATION_COUNT) return false;
    if (card.suit != fIdx) return false; // Foundation index mapped to suit

    CardStack* f = &game.foundations[fIdx];
    if (f->count == 0) {
        return card.rank == CARD_RANK_ACE;
    }
    Card top = f->cards[f->count - 1];
    return (card.rank == top.rank + 1);
}

bool SolitaireCanMoveToTableau(Card card, int tIdx) {
    if (tIdx < 0 || tIdx >= TABLEAU_COUNT) return false;

    CardStack* t = &game.tableau[tIdx];
    if (t->count == 0) {
        // Only King can be placed on empty tableau
        return card.rank == CARD_RANK_KING;
    }
    Card top = t->cards[t->count - 1];
    if (!top.faceUp) return false;

    // Opposite color and descending by 1
    bool diffColor = (CardIsRed(card.suit) != CardIsRed(top.suit));
    return (diffColor && card.rank == top.rank - 1);
}

bool SolitaireMoveCards(int srcType, int srcIdx, int srcCardIdx, int dstType, int dstIdx) {
    if (game.isWon) return false;

    Card movingCard;
    CardStack* srcStack = NULL;

    if (srcType == PILE_WASTE) {
        if (game.waste.count == 0) return false;
        srcStack = &game.waste;
        srcCardIdx = game.waste.count - 1;
        movingCard = game.waste.cards[srcCardIdx];
    } else if (srcType == PILE_FOUNDATION) {
        if (srcIdx < 0 || srcIdx >= FOUNDATION_COUNT || game.foundations[srcIdx].count == 0) return false;
        srcStack = &game.foundations[srcIdx];
        srcCardIdx = srcStack->count - 1;
        movingCard = srcStack->cards[srcCardIdx];
    } else if (srcType == PILE_TABLEAU) {
        if (srcIdx < 0 || srcIdx >= TABLEAU_COUNT || srcCardIdx < 0 || srcCardIdx >= game.tableau[srcIdx].count) return false;
        srcStack = &game.tableau[srcIdx];
        movingCard = srcStack->cards[srcCardIdx];
        if (!movingCard.faceUp) return false;
    } else {
        return false;
    }

    // Check Destination Legality
    if (dstType == PILE_FOUNDATION) {
        // Can only move single card to foundation
        if (srcType == PILE_TABLEAU && srcCardIdx != srcStack->count - 1) {
            return false;
        }
        if (!SolitaireCanMoveToFoundation(movingCard, dstIdx)) {
            return false;
        }

        SolitaireSaveSnapshot();

        // Perform move
        game.foundations[dstIdx].cards[game.foundations[dstIdx].count++] = movingCard;
        srcStack->count--;

        game.score += 10;
        game.moveCount++;
    } else if (dstType == PILE_TABLEAU) {
        if (!SolitaireCanMoveToTableau(movingCard, dstIdx)) {
            return false;
        }

        SolitaireSaveSnapshot();

        // Perform move of 1 or multiple cards (tableau sequences)
        int moveCount = srcStack->count - srcCardIdx;
        for (int i = 0; i < moveCount; i++) {
            Card c = srcStack->cards[srcCardIdx + i];
            game.tableau[dstIdx].cards[game.tableau[dstIdx].count++] = c;
        }
        srcStack->count -= moveCount;

        if (srcType == PILE_WASTE) {
            game.score += 5;
        }
        game.moveCount++;
    } else {
        return false;
    }

    // Auto-reveal new top card on source tableau if face-down
    if (srcType == PILE_TABLEAU && srcStack->count > 0) {
        if (!srcStack->cards[srcStack->count - 1].faceUp) {
            srcStack->cards[srcStack->count - 1].faceUp = true;
            game.score += 5;
        }
    }

    // Clear selection
    game.selectedPileType = PILE_NONE;
    game.selectedPileIndex = -1;
    game.selectedCardIndex = -1;

    // Check Win
    SolitaireCheckWin();
    return true;
}

bool SolitaireAutoMoveToFoundation(int srcType, int srcIdx, int srcCardIdx) {
    if (game.isWon) return false;

    Card c;
    if (srcType == PILE_WASTE) {
        if (game.waste.count == 0) return false;
        c = game.waste.cards[game.waste.count - 1];
        srcCardIdx = game.waste.count - 1;
    } else if (srcType == PILE_TABLEAU) {
        if (srcIdx < 0 || srcIdx >= TABLEAU_COUNT || game.tableau[srcIdx].count == 0) return false;
        srcCardIdx = game.tableau[srcIdx].count - 1;
        c = game.tableau[srcIdx].cards[srcCardIdx];
        if (!c.faceUp) return false;
    } else {
        return false;
    }

    int fIdx = c.suit;
    if (SolitaireCanMoveToFoundation(c, fIdx)) {
        return SolitaireMoveCards(srcType, srcIdx, srcCardIdx, PILE_FOUNDATION, fIdx);
    }
    return false;
}

bool SolitaireCheckWin() {
    int totalFoundation = 0;
    for (int i = 0; i < FOUNDATION_COUNT; i++) {
        totalFoundation += game.foundations[i].count;
    }

    if (totalFoundation == TOTAL_CARDS) {
        game.isWon = true;
        game.celebrationActive = true;
        game.score += 500;
        return true;
    }
    return false;
}

void SolitaireUpdateTimerTick() {
    if (game.splashState == SPLASH_DONE && !game.isWon) {
        game.timeElapsedSec++;
    }
}

void SolitaireUpdateSplash() {
    if (game.splashState == SPLASH_HOLD) {
        game.splashTimer--;
        if (game.splashTimer <= 0) {
            game.splashState = SPLASH_FADE_OUT;
            game.splashTimer = 30; // 30 frames fade out (~0.5s)
        }
    } else if (game.splashState == SPLASH_FADE_OUT) {
        game.splashTimer--;
        game.splashAlpha = (float)game.splashTimer / 30.0f;
        if (game.splashTimer <= 0) {
            game.splashAlpha = 0.0f;
            game.splashState = SPLASH_DONE;
        }
    }
}

void SolitaireUpdateCascade() {
    if (!game.celebrationActive) return;

    game.cascadeTimer++;
    // Spawn a new cascade card every 8 frames until all 52 are in motion
    if (game.cascadeTimer % 8 == 0 && game.cascadeNextIndex < TOTAL_CARDS) {
        int fIdx = game.cascadeNextIndex % FOUNDATION_COUNT;
        int cIdx = game.foundations[fIdx].count - 1 - (game.cascadeNextIndex / FOUNDATION_COUNT);
        if (cIdx >= 0) {
            CascadeCard* cc = &game.cascadeCards[game.cascadeNextIndex];
            cc->card = game.foundations[fIdx].cards[cIdx];
            cc->x = 340 + fIdx * 105;
            cc->y = 75;
            cc->vx = (float)RngRange(-6, 6);
            if (cc->vx == 0) cc->vx = 3.0f;
            cc->vy = (float)RngRange(-4, -1);
            cc->active = true;
            game.cascadeNextIndex++;
        }
    }

    // Physics step for active cascade cards
    for (int i = 0; i < TOTAL_CARDS; i++) {
        if (!game.cascadeCards[i].active) continue;
        CascadeCard* cc = &game.cascadeCards[i];
        cc->x += cc->vx;
        cc->y += cc->vy;
        cc->vy += 0.45f; // Gravity

        // Bounce on floor (window height 560 - card height 105)
        if (cc->y > (560 - CARD_HEIGHT)) {
            cc->y = 560 - CARD_HEIGHT;
            cc->vy = -cc->vy * 0.82f; // restitution
        }
        // Screen bounds
        if (cc->x < 0 || cc->x > (800 - CARD_WIDTH)) {
            cc->vx = -cc->vx;
        }
    }
}
