// Tetris: fit the falling pieces, complete lines. Every 10 lines, a level faster.
// Joystick left/right: move (hold it: it repeats), down: drop faster, up: drop at once.
// A: turn right, B: turn left.
#include "arcade.h"

namespace {
constexpr int BW = 10, BH = 20, C = 11, OX = (W - BW * C) / 2, OY = (H - BH * C) / 2;
// Each piece in its 4 turns, as 4x4 bits: row r, column c is bit 15 - (r * 4 + c).
constexpr uint16_t SHAPES[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },   // I
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },   // J
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },   // L
    { 0x6600, 0x6600, 0x6600, 0x6600 },   // O
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },   // S
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },   // T
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },   // Z
};
const uint16_t COLORS[8] = { 0, RGB(0, 229, 255), RGB(41, 98, 255), RGB(255, 145, 0), RGB(255, 234, 0),
                             RGB(0, 230, 118), RGB(213, 0, 249), RGB(255, 23, 68) };
constexpr int LINE_POINTS[5] = { 0, 40, 100, 300, 1200 };

uint8_t board[BH][BW];                 // 0 empty, 1-7 the color of a piece
int piece, rot, px, py, next, score, lines, level;
int bag[7], bagLeft;
float fall;

bool cell(uint16_t shape, int r, int c) { return shape & (0x8000 >> (r * 4 + c)); }

bool fits(int p, int ro, int x, int y) {
    const uint16_t s = SHAPES[p][ro & 3];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!cell(s, r, c)) continue;
            const int bx = x + c, by = y + r;
            if (bx < 0 || bx >= BW || by >= BH) return false;
            if (by >= 0 && board[by][bx]) return false;
        }
    return true;
}

// The 7 pieces in a shuffled bag: never a long wait for the I.
int fromBag() {
    if (!bagLeft) {
        for (int i = 0; i < 7; i++) bag[i] = i;
        for (int i = 6; i > 0; i--) std::swap(bag[i], bag[rnd(0, i)]);
        bagLeft = 7;
    }
    return bag[--bagLeft];
}

bool spawn() {
    piece = next;
    next = fromBag();
    rot = 0;
    px = BW / 2 - 2;
    py = -1;
    fall = 0;
    return fits(piece, rot, px, py);
}

void start() {
    memset(board, 0, sizeof(board));
    score = lines = level = 0;
    bagLeft = 0;
    next = fromBag();
    spawn();
}

void lock() {
    const uint16_t s = SHAPES[piece][rot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cell(s, r, c) && py + r >= 0) board[py + r][px + c] = piece + 1;
    int full = 0;
    for (int y = BH - 1; y >= 0; y--) {
        bool complete = true;
        for (int x = 0; x < BW && complete; x++) complete = board[y][x];
        if (!complete) continue;
        memmove(board[1], board[0], y * BW);
        memset(board[0], 0, BW);
        full++;
        y++;                               // look at the same row again: it has the one from above now
    }
    if (full) {
        score += LINE_POINTS[full] * (level + 1);
        lines += full;
        level = lines / 10;
        sfx(full == 4 ? 1800 : 1200, full == 4 ? 300 : 120);
        flash(full == 4 ? 255 : 0, 200, 255, 200);
    } else {
        sfx(300, 15);
    }
}

// Turning near a wall or another piece: try a little to each side (a "kick").
void turn(int d) {
    const int to = (rot + d + 4) & 3;
    for (int kick : { 0, -1, 1, -2, 2 })
        if (fits(piece, to, px + kick, py)) { rot = to; px += kick; sfx(700, 10); return; }
}

void block(int x, int y, int color, bool ghost = false) {
    if (ghost) { gfx.drawRect(x + 1, y + 1, C - 2, C - 2, COLORS[color]); return; }
    gfx.fillRect(x, y, C - 1, C - 1, COLORS[color]);
    gfx.drawFastHLine(x, y, C - 1, TFT_WHITE);
}

void drawPiece(int p, int ro, int x0, int y0, bool ghost) {
    const uint16_t s = SHAPES[p][ro];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cell(s, r, c) && y0 + r * C >= OY) block(x0 + c * C, y0 + r * C, p + 1, ghost);
}

void draw() {
    gfx.fillSprite(RGB(14, 14, 28));
    gfx.fillRect(OX - 2, OY - 2, BW * C + 4, BH * C + 4, RGB(60, 60, 90));
    gfx.fillRect(OX, OY, BW * C, BH * C, TFT_BLACK);
    for (int y = 0; y < BH; y++)
        for (int x = 0; x < BW; x++)
            if (board[y][x]) block(OX + x * C, OY + y * C, board[y][x]);
    int gy = py;                                 // the ghost: where it would land
    while (fits(piece, rot, px, gy + 1)) gy++;
    drawPiece(piece, rot, OX + px * C, OY + gy * C, true);
    drawPiece(piece, rot, OX + px * C, OY + py * C, false);

    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(TFT_WHITE);
    gfx.setTextDatum(top_left);
    const int rx = OX + BW * C + 14;
    gfx.drawString("NEXT", rx, OY);
    drawPiece(next, 0, rx, OY + 18, false);
    char buf[24];
    const char* labels[] = { "SCORE", "LINES", "LEVEL" };
    const int values[] = { score, lines, level };
    for (int i = 0; i < 3; i++) {
        gfx.setTextColor(RGB(150, 150, 180));
        gfx.drawString(labels[i], 10, OY + i * 44);
        snprintf(buf, sizeof(buf), "%d", values[i]);
        gfx.setTextColor(TFT_WHITE);
        gfx.drawString(buf, 10, OY + i * 44 + 16);
    }
}

bool frame(const Input& in, float dt) {
    if (in.leftP && fits(piece, rot, px - 1, py)) px--;
    if (in.rightP && fits(piece, rot, px + 1, py)) px++;
    if (in.aP) turn(1);
    if (in.bP) turn(-1);
    static bool wasUp = false;                   // hard drop on the push only: holding up doesn't drop the next ones too
    const bool drop = in.up && !wasUp;
    wasUp = in.up;
    if (drop) {
        while (fits(piece, rot, px, py + 1)) { py++; score += 2; }
        fall = 1e9;
    }
    const float interval = in.down ? 0.04f : max(0.06f, 0.8f - level * 0.07f);
    fall += dt;
    while (fall >= interval) {
        fall -= interval;
        if (fits(piece, rot, px, py + 1)) { py++; if (in.down) score++; continue; }
        lock();
        if (!spawn()) { draw(); return false; }
        fall = 0;
        break;
    }
    draw();
    return true;
}

int score_() { return score; }
}   // namespace

const Game GAME_TETRIS = { "Tetris", "Left/right: move. Down: faster. Up: drop. A/B: turn.", RGB(41, 98, 255), start, frame, score_, nullptr };
