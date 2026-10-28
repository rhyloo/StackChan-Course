// Snake: eat the apples, don't bite yourself or the walls. Every apple makes you longer and faster.
#include "arcade.h"

namespace {
constexpr int CELL = 16, COLS = W / CELL, ROWS = (H - 16) / CELL, TOP = H - ROWS * CELL;
constexpr int DX[] = { 1, 0, -1, 0 }, DY[] = { 0, 1, 0, -1 };   // right, down, left, up
struct Cell { int8_t x, y; };

Cell body[COLS * ROWS];
int len, dir, nextDir, score;
Cell food;
float acc, stepS;

bool onSnake(Cell c, int upTo) {
    for (int i = 0; i < upTo; i++) if (body[i].x == c.x && body[i].y == c.y) return true;
    return false;
}

void placeFood() {
    do { food = { (int8_t)rnd(0, COLS - 1), (int8_t)rnd(0, ROWS - 1) }; } while (onSnake(food, len));
}

void start() {
    len = 3;
    for (int i = 0; i < len; i++) body[i] = { (int8_t)(COLS / 2 - i), (int8_t)(ROWS / 2) };
    dir = nextDir = 0;
    score = 0;
    acc = 0;
    stepS = 0.16f;
    placeFood();
}

void draw() {
    gfx.fillSprite(RGB(20, 60, 30));
    for (int y = 0; y < ROWS; y++)
        for (int x = (y & 1); x < COLS; x += 2) gfx.fillRect(x * CELL, TOP + y * CELL, CELL, CELL, RGB(24, 70, 36));
    const int fx = food.x * CELL + CELL / 2, fy = TOP + food.y * CELL + CELL / 2;
    gfx.fillCircle(fx, fy + 1, 6, RGB(229, 57, 53));
    gfx.fillRect(fx - 1, fy - 7, 2, 4, RGB(121, 85, 72));
    gfx.fillEllipse(fx + 3, fy - 6, 3, 2, RGB(102, 187, 106));
    for (int i = len - 1; i >= 0; i--) {
        const int x = body[i].x * CELL, y = TOP + body[i].y * CELL;
        gfx.fillRoundRect(x + 1, y + 1, CELL - 2, CELL - 2, 4, i ? RGB(124, 179, 66) : RGB(156, 204, 101));
    }
    // Eyes looking where it goes.
    const int hx = body[0].x * CELL + CELL / 2, hy = TOP + body[0].y * CELL + CELL / 2;
    for (int s = -1; s <= 1; s += 2) {
        const int ex = hx + DX[dir] * 3 + DY[dir] * 4 * s, ey = hy + DY[dir] * 3 + DX[dir] * 4 * s;
        gfx.fillCircle(ex, ey, 2, TFT_WHITE);
        gfx.drawPixel(ex + DX[dir], ey + DY[dir], TFT_BLACK);
    }
    gfx.fillRect(0, 0, W, TOP, RGB(10, 30, 15));
    char s[24];
    snprintf(s, sizeof(s), "APPLES %d", score);
    hudText(s, "SNAKE");
}

bool frame(const Input& in, float dt) {
    int want = in.rightP ? 0 : in.downP ? 1 : in.leftP ? 2 : in.upP ? 3 : -1;
    if (want >= 0 && want != (dir + 2) % 4) nextDir = want;   // no turning back on yourself
    acc += dt;
    while (acc >= stepS) {
        acc -= stepS;
        dir = nextDir;
        const Cell h = { (int8_t)(body[0].x + DX[dir]), (int8_t)(body[0].y + DY[dir]) };
        const bool grow = h.x == food.x && h.y == food.y;
        // The tail moves out of the way this step, unless the snake grows.
        if (h.x < 0 || h.x >= COLS || h.y < 0 || h.y >= ROWS || onSnake(h, grow ? len : len - 1)) {
            flash(255, 0, 0, 500);
            draw();
            return false;
        }
        memmove(body + 1, body, (grow ? len : len - 1) * sizeof(Cell));
        body[0] = h;
        if (grow) {
            len++;
            score++;
            stepS = max(0.06f, stepS * 0.97f);
            sfx(1400, 30);
            flash(0, 255, 0, 120);
            if (len >= COLS * ROWS) return false;   // the whole board: nothing left to eat
            placeFood();
        }
    }
    draw();
    return true;
}

int score_() { return score; }
}   // namespace

const Game GAME_SNAKE = { "Snake", "Joystick: turn. Eat apples, don't bite yourself.", RGB(124, 179, 66), start, frame, score_, nullptr };
