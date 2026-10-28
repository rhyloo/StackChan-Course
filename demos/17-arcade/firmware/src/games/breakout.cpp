// Breakout: bounce the ball with the paddle and break every brick. The joystick is
// analog here: push it a little and the paddle moves slowly, a lot and it flies.
#include "arcade.h"

namespace {
constexpr int COLS = 10, ROWS = 6, BW = 30, BH = 12, BX = (W - COLS * BW) / 2, BY = 34;
constexpr int PW = 56, PH = 7, PY = H - 16, R = 4;
constexpr float PADDLE_SPEED = 360, BALL_SPEED = 190;
const uint16_t ROW_COLORS[ROWS] = { RGB(239, 83, 80), RGB(255, 152, 0), RGB(255, 235, 59),
                                    RGB(102, 187, 106), RGB(66, 165, 245), RGB(171, 71, 188) };

uint8_t bricks[ROWS][COLS];          // hits left
int left_, lives, score, level;
float px, bx, by, vx, vy, speed;
bool stuck;                           // the ball waits on the paddle until A

void newLevel() {
    left_ = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            // From level 2 the top rows need two hits; from level 3 some gaps.
            const bool gap = level >= 3 && (r + c + level) % 7 == 0;
            bricks[r][c] = gap ? 0 : (level >= 2 && r < 2 ? 2 : 1);
            left_ += bricks[r][c] > 0;
        }
    speed = BALL_SPEED * (1 + 0.12f * (level - 1));
    stuck = true;
}

void start() {
    level = 1;
    lives = 3;
    score = 0;
    px = W / 2;
    newLevel();
}

void serve() {
    const float a = (-60 + rnd(0, 40)) * DEG_TO_RAD;   // up, a bit to one side
    vx = speed * sinf(a);
    vy = -speed * cosf(a);
    stuck = false;
}

// Moves the ball a small step and bounces it. False if it fell.
bool move(float d) {
    const float ox = bx, oy = by;
    bx += vx * d;
    by += vy * d;
    if (bx < R) { bx = R; vx = fabsf(vx); sfx(600, 10); }
    if (bx > W - R) { bx = W - R; vx = -fabsf(vx); sfx(600, 10); }
    if (by < 16 + R) { by = 16 + R; vy = fabsf(vy); sfx(600, 10); }
    if (by > H + R) return false;
    // The paddle: where it hits sets the angle, up to 60 degrees from vertical.
    if (vy > 0 && by + R >= PY && oy + R <= PY + 2 && bx >= px - PW / 2 - R && bx <= px + PW / 2 + R) {
        const float t = constrain((bx - px) / (PW / 2), -1.0f, 1.0f);
        vx = speed * sinf(t * 60 * DEG_TO_RAD);
        vy = -speed * cosf(t * 60 * DEG_TO_RAD);
        by = PY - R;
        sfx(800, 15);
    }
    // One brick per step: bounce on the side the ball came from.
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (!bricks[r][c]) continue;
            const int x0 = BX + c * BW, y0 = BY + r * BH;
            if (bx + R < x0 || bx - R > x0 + BW || by + R < y0 || by - R > y0 + BH) continue;
            if (ox + R <= x0 || ox - R >= x0 + BW) vx = -vx; else vy = -vy;
            bx = ox;
            by = oy;
            if (--bricks[r][c] == 0) { left_--; score += (ROWS - r) * 10 * level; }
            sfx(1000 + (ROWS - r) * 120, 20);
            const uint16_t c565 = ROW_COLORS[r];
            flash((c565 >> 11) << 3, ((c565 >> 5) & 0x3F) << 2, (c565 & 0x1F) << 3, 80);   // the brick's color
            return true;
        }
    return true;
}

void draw() {
    gfx.fillSprite(RGB(12, 14, 30));
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (!bricks[r][c]) continue;
            const int x = BX + c * BW, y = BY + r * BH;
            gfx.fillRect(x + 1, y + 1, BW - 2, BH - 2, ROW_COLORS[r]);
            if (bricks[r][c] > 1) gfx.drawRect(x + 3, y + 3, BW - 6, BH - 6, TFT_WHITE);
        }
    gfx.fillRoundRect(px - PW / 2, PY, PW, PH, 3, RGB(224, 224, 224));
    gfx.fillCircle(bx, by, R, TFT_WHITE);
    gfx.fillRect(0, 0, W, 16, RGB(30, 34, 60));
    char s[24], l[24];
    snprintf(s, sizeof(s), "SCORE %d", score);
    snprintf(l, sizeof(l), "LEVEL %d  LIVES %d", level, lives);
    hudText(s, l);
    if (stuck) {
        gfx.setFont(&fonts::efontJA_16);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(RGB(255, 214, 0));
        gfx.drawString("A: launch", W / 2, 160);
    }
}

bool frame(const Input& in, float dt) {
    // Analog: the further the stick, the faster the paddle.
    px += in.x * PADDLE_SPEED * dt;
    px = constrain(px, PW / 2.0f, W - PW / 2.0f);
    if (stuck) {
        bx = px;
        by = PY - R - 1;
        if (in.aP) serve();
    } else {
        const int steps = max(1, (int)ceilf(speed * dt / 3));   // small steps: the ball can't jump a brick
        for (int i = 0; i < steps; i++) {
            if (move(dt / steps)) continue;
            if (--lives <= 0) { draw(); return false; }
            sfx(200, 300);
            flash(255, 0, 0, 400);
            stuck = true;
            break;
        }
        if (left_ == 0) {
            level++;
            sfx(1600, 200);
            flash(0, 255, 0, 500);
            newLevel();
        }
    }
    draw();
    return true;
}

int score_() { return score; }
}   // namespace

const Game GAME_BREAKOUT = { "Breakout", "Joystick: move the paddle. A: launch the ball.", RGB(239, 83, 80), start, frame, score_, nullptr };
