#pragma once
// Demo 17 - What every game sees.
//
// A game is three functions: start() sets it up, frame() moves it one step and draws
// it on gfx (the whole screen, pushed by main.cpp after every frame), score() says
// the points. frame() returns false when the game is over; main.cpp does the menu,
// the pause, the game over screen and the high scores.
#include <M5Unified.h>
#include "input.h"

static constexpr int W = 320, H = 240;
extern M5Canvas gfx;

struct Game {
    const char* name;
    const char* hint;                  // one line on the menu: how to play
    uint16_t color;
    void (*start)();
    bool (*frame)(const Input& in, float dt);   // dt: seconds since the last frame
    int (*score)();
    const char* (*endTitle)();                 // nullptr, or the title instead of "GAME OVER" (a level cleared)
};

extern const Game GAME_SNAKE, GAME_BREAKOUT, GAME_TETRIS, GAME_INVADERS, GAME_PLATFORMER, GAME_DINO;

static constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }
int rnd(int lo, int hi);                         // lo..hi, both included
void sfx(int hz, int ms);                        // a beep (the speaker plays it by itself)
void flash(uint8_t r, uint8_t g, uint8_t b, int ms);   // the base LEDs for a moment
// Pixel art: a sprite from rows of characters, one per pixel, colored by color(); the
// color KEY_COLOR is transparent (push it with KEY_COLOR as the transparent color).
static constexpr uint16_t KEY_COLOR = 0xF81F;
void makeSprite(M5Canvas& c, const char* const rows[], int w, int h, uint16_t (*color)(char), bool mirror = false);
// "SCORE 120" on the left of a HUD line at y, "LIVES 3" or whatever on the right.
void hudText(const char* left, const char* right, int y = 0);
