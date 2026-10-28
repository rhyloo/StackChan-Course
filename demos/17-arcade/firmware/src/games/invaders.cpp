// Space Invaders: the fleet comes down, faster and faster the fewer are left. Shoot them
// before they land, and hide behind the shields (they wear out, for you and for them).
// Joystick: move (analog). A: fire.
#include "arcade.h"

namespace {
constexpr int COLS = 8, ROWS = 5, AW = 16, AH = 12, GX = 28, GY = 22;   // alien size and spacing
constexpr int PY = H - 18, PW = 22, SHIELDS = 4, SC = 4, SW_ = 6, SH = 4;   // shield: 6x4 cells of 4 px
constexpr int SHIELD_Y = PY - 34, MAX_SHOTS = 3;

// 8x8 drawings, two frames each (they wiggle as they march), drawn at 2x.
constexpr uint8_t ALIENS[3][2][8] = {
    { { 0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x5A, 0xA5 }, { 0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x5A, 0x81, 0x42 } },
    { { 0x24, 0x18, 0x3C, 0x5A, 0xFF, 0xBD, 0xA5, 0x24 }, { 0x24, 0x99, 0xBD, 0xDB, 0x7E, 0x3C, 0x24, 0x42 } },
    { { 0x3C, 0x7E, 0xFF, 0x99, 0xFF, 0x66, 0x99, 0x42 }, { 0x3C, 0x7E, 0xFF, 0x99, 0xFF, 0x24, 0x5A, 0x81 } },
};
const uint16_t ALIEN_COLORS[ROWS] = { RGB(239, 83, 80), RGB(255, 167, 38), RGB(255, 167, 38), RGB(102, 187, 106), RGB(102, 187, 106) };

bool alive[ROWS][COLS];
uint8_t shield[SHIELDS][SH][SW_];
int count, lives, score, wave, animFrame, fleetDir;
float fx, fy, stepT, px;
struct Shot { float x, y; bool on; };
Shot mine, theirs[MAX_SHOTS];
float fireT, hitT;

int alienX(int c) { return (int)fx + c * GX; }
int alienY(int r) { return (int)fy + r * GY; }

void newWave() {
    count = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) { alive[r][c] = true; count++; }
    fx = 20;
    fy = 26 + min(wave - 1, 4) * 8;         // each wave starts a bit lower
    fleetDir = 1;
    stepT = 0;
    mine.on = false;
    for (auto& s : theirs) s.on = false;
}

void start() {
    wave = 1;
    lives = 3;
    score = 0;
    px = W / 2;
    for (auto& s : shield) for (auto& row : s) for (auto& c : row) c = 2;
    // Round off the top corners and cut an arch underneath.
    for (auto& s : shield) { s[0][0] = s[0][SW_ - 1] = 0; s[SH - 1][2] = s[SH - 1][3] = 0; }
    newWave();
}

int shieldX(int i) { return 30 + i * ((W - 60 - SW_ * SC) / (SHIELDS - 1)); }

// A shot at (x, y) that hits a shield cell wears it down. True if it hit.
bool hitShield(float x, float y) {
    for (int i = 0; i < SHIELDS; i++) {
        const int c = ((int)x - shieldX(i)) / SC, r = ((int)y - SHIELD_Y) / SC;
        if (x < shieldX(i) || y < SHIELD_Y || c >= SW_ || r >= SH || !shield[i][r][c]) continue;
        shield[i][r][c]--;
        return true;
    }
    return false;
}

// One step of the fleet: sideways, and down at the edges. The fewer, the faster.
void marchFleet(float dt) {
    stepT -= dt;
    if (stepT > 0) return;
    stepT = max(0.03f, 0.6f * count / (ROWS * COLS) / (1 + 0.15f * (wave - 1)));
    animFrame ^= 1;
    int minC = COLS, maxC = -1;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (alive[r][c]) { minC = min(minC, c); maxC = max(maxC, c); }
    const int nx = (int)fx + fleetDir * 6;
    if (nx + minC * GX < 4 || nx + maxC * GX + AW > W - 4) { fy += 10; fleetDir = -fleetDir; }
    else fx = nx;
    sfx(animFrame ? 90 : 110, 30);
}

void alienFire(float dt) {
    fireT -= dt;
    if (fireT > 0) return;
    fireT = max(0.25f, 1.1f - 0.12f * wave) * (0.5f + rnd(0, 100) / 100.0f);
    // The lowest alien of a random column with someone left.
    for (int tries = 0; tries < 8; tries++) {
        const int c = rnd(0, COLS - 1);
        for (int r = ROWS - 1; r >= 0; r--) {
            if (!alive[r][c]) continue;
            for (auto& s : theirs)
                if (!s.on) { s = { (float)alienX(c) + AW / 2, (float)alienY(r) + AH, true }; return; }
            return;
        }
    }
}

void drawAlien(int r, int c) {
    const uint8_t* bits = ALIENS[r == 0 ? 0 : r < 3 ? 1 : 2][animFrame];
    const int x0 = alienX(c), y0 = alienY(r);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (bits[y] & (0x80 >> x)) gfx.fillRect(x0 + x * 2, y0 + y * 2 - 2, 2, 2, ALIEN_COLORS[r]);
}

void draw() {
    gfx.fillSprite(TFT_BLACK);
    for (int i = 0; i < 30; i++) gfx.drawPixel((i * 97) % W, 20 + (i * 53) % (H - 40), RGB(80, 80, 110));   // stars
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (alive[r][c]) drawAlien(r, c);
    for (int i = 0; i < SHIELDS; i++)
        for (int r = 0; r < SH; r++)
            for (int c = 0; c < SW_; c++)
                if (shield[i][r][c])
                    gfx.fillRect(shieldX(i) + c * SC, SHIELD_Y + r * SC, SC, SC, shield[i][r][c] > 1 ? RGB(76, 175, 80) : RGB(46, 105, 48));
    if (hitT <= 0 || ((int)(hitT * 10) & 1)) {   // blinks after being hit
        const int x = px - PW / 2;
        gfx.fillRect(x, PY + 4, PW, 6, RGB(41, 182, 246));
        gfx.fillRect(x + PW / 2 - 3, PY, 6, 4, RGB(41, 182, 246));
    }
    if (mine.on) gfx.fillRect(mine.x - 1, mine.y, 2, 8, TFT_WHITE);
    for (auto& s : theirs)
        if (s.on) gfx.drawLine(s.x, s.y, s.x + (((int)s.y / 4) & 1 ? 2 : -2), s.y + 6, RGB(255, 235, 59));
    gfx.drawFastHLine(0, H - 4, W, RGB(76, 175, 80));
    char s[24], l[24];
    snprintf(s, sizeof(s), "SCORE %d", score);
    snprintf(l, sizeof(l), "WAVE %d  LIVES %d", wave, lives);
    hudText(s, l);
}

bool frame(const Input& in, float dt) {
    px = constrain(px + in.x * 200 * dt, PW / 2.0f + 2, W - PW / 2.0f - 2);
    if (hitT > 0) hitT -= dt;
    if (in.aP && !mine.on) { mine = { px, (float)PY - 8, true }; sfx(1500, 20); }

    if (mine.on) {
        mine.y -= 330 * dt;
        if (mine.y < 16 || hitShield(mine.x, mine.y)) mine.on = false;
        for (int r = 0; r < ROWS && mine.on; r++)
            for (int c = 0; c < COLS && mine.on; c++) {
                if (!alive[r][c]) continue;
                const int x = alienX(c), y = alienY(r);
                if (mine.x < x || mine.x > x + AW || mine.y > y + AH || mine.y + 8 < y) continue;
                alive[r][c] = false;
                mine.on = false;
                count--;
                score += (ROWS - r) * 10;
                sfx(400, 60);
                flash(255, 120, 0, 80);
            }
    }
    for (auto& s : theirs) {
        if (!s.on) continue;
        s.y += (110 + 10 * wave) * dt;
        if (s.y > H || hitShield(s.x, s.y + 6)) { s.on = false; continue; }
        if (hitT <= 0 && s.y + 6 >= PY && s.y <= PY + 10 && fabsf(s.x - px) <= PW / 2) {
            s.on = false;
            hitT = 1.5f;
            sfx(150, 400);
            flash(255, 0, 0, 500);
            if (--lives <= 0) { draw(); return false; }
        }
    }
    marchFleet(dt);
    alienFire(dt);
    // Landing: the aliens reached the shields' line.
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (alive[r][c] && alienY(r) + AH >= SHIELD_Y) { lives = 0; draw(); return false; }
    if (count == 0) {
        wave++;
        sfx(1800, 250);
        flash(0, 255, 0, 400);
        newWave();
    }
    draw();
    return true;
}

int score_() { return score; }
}   // namespace

const Game GAME_INVADERS = { "Space Invaders", "Joystick: move. A: fire. Stop them before they land.", RGB(102, 187, 106), start, frame, score_, nullptr };
