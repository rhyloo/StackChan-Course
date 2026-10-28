#pragma once
// Demo 16 - Everything drawn: Stack-chan's face, the operation signs, the equation and
// the pictures to count. Only shapes (no image files), on any LovyanGFX surface:
// main.cpp draws on a full-screen canvas and pushes it at once (no flicker).
#include <M5Unified.h>
#include "game.h"

using Gfx = lgfx::LovyanGFX;

enum class Mood { neutral, happy, oops, thinking };

// The face, w pixels wide, centered at (cx, cy).
void artFace(Gfx& g, int cx, int cy, int w, Mood mood, uint16_t color);
// +, -, x or ÷ drawn with shapes (the big fonts only have ASCII), size pixels across.
void artOp(Gfx& g, Op op, int cx, int cy, int size, uint16_t color);
// "a + b = ?" centered at (cx, cy). shown = -1 draws "?", otherwise that number in answerColor.
void artEquation(Gfx& g, const Question& q, int cx, int cy, int shown, uint16_t color, uint16_t answerColor);
// Things to count for the easy questions, in the box (x, y, w, h). False if this
// question has no picture (big numbers): then draw something else there.
bool artPicture(Gfx& g, const Question& q, int x, int y, int w, int h);
// A star, for the score.
void artStar(Gfx& g, int cx, int cy, int r, uint16_t color);
