// Dino Run: the endless runner of the browser with no connection (original art here).
// Jump the cacti, duck under the birds. It goes faster and faster; every 700 points
// day turns into night and back.
// A or joystick up: jump (hold it: higher). Joystick down: duck, or fall faster in the air.
#include "arcade.h"

namespace {
// palette: x=535353 k=202124 w=F7F7F7 l=9AA0A6 c=5B8C3A C=3B6127 b=6D6D6D
const char* const DINO[][19] = {
    {   // run 1
        "..........xxxxxx....", ".........xxxxxxxx...", "........xxxwwxxxxx..", "........xxxwkxxxxxx.",
        "........xxxxxxxxxxx.", "....x...xxxxxxxxxxx.", "...xx...xxxxxxkkkk..", "..xxx..xxxxxxxxxx...",
        ".xxxx.xxxxxxxx......", ".xxxxxxxxxxxxxxx....", "xxxxxxxxxxxxxx.xx...", "xxxxxxxxxxxxxx......",
        ".xxxxxllllxxxx......", "..xxxxllllxxx.......", "...xxxxxxxxxx.......", "....xxxxxxxx........",
        ".....xx...xx........", ".....xx....xx.......", ".....xxx............" },
    {   // run 2
        "..........xxxxxx....", ".........xxxxxxxx...", "........xxxwwxxxxx..", "........xxxwkxxxxxx.",
        "........xxxxxxxxxxx.", "....x...xxxxxxxxxxx.", "...xx...xxxxxxkkkk..", "..xxx..xxxxxxxxxx...",
        ".xxxx.xxxxxxxx......", ".xxxxxxxxxxxxxxx....", "xxxxxxxxxxxxxx.xx...", "xxxxxxxxxxxxxx......",
        ".xxxxxllllxxxx......", "..xxxxllllxxx.......", "...xxxxxxxxxx.......", "....xxxxxxxx........",
        ".....xx...xx........", "....xx....xx........", "..........xxx......." },
    {   // jump: both feet down, mouth open
        "..........xxxxxx....", ".........xxxxxxxx...", "........xxxwwxxxxx..", "........xxxwkxxxxxx.",
        "........xxxxxxxxxxx.", "....x...xxxxxxxxxxx.", "...xx...xxxxxxxkkk..", "..xxx..xxxxxxxk.....",
        ".xxxx.xxxxxxxxxkk...", ".xxxxxxxxxxxxxxx....", "xxxxxxxxxxxxxx.xx...", "xxxxxxxxxxxxxx......",
        ".xxxxxllllxxxx......", "..xxxxllllxxx.......", "...xxxxxxxxxx.......", "....xxxxxxxx........",
        ".....xx...xx........", ".....xx...xx........", ".....xxx..xxx......." },
    {   // hit: eyes crossed
        "..........xxxxxx....", ".........xxxxxxxx...", "........xxwxwxxxxx..", "........xxxwxxxxxxx.",
        "........xxwxwxxxxxx.", "....x...xxxxxxxxxxx.", "...xx...xxxxxxxkkk..", "..xxx..xxxxxxxk.k...",
        ".xxxx.xxxxxxxxxkk...", ".xxxxxxxxxxxxxxx....", "xxxxxxxxxxxxxx.xx...", "xxxxxxxxxxxxxx......",
        ".xxxxxllllxxxx......", "..xxxxllllxxx.......", "...xxxxxxxxxx.......", "....xxxxxxxx........",
        ".....xx...xx........", ".....xx...xx........", ".....xxx..xxx......." },
};
const char* const DUCK[][12] = {
    {   "..................xxxxxx..", ".................xxxxxxxx.", "x..x..xxxxxxxxxxxxxwwxxxxx", "xxxxxxxxxxxxxxxxxxxwkxxxxx",
        "xxxxxxxxxxxxxxxxxxxxxxxxxx", ".xxxxxxxxxxxxxxxxxxxxkkkk.", "..xxxxxxllllllxxxxxxxx....", "...xxxxxllllllxxx.xx......",
        "....xxxxxxxxxxxx..........", ".....xx...xx..............", ".....xx....xx.............", ".....xxx.................." },
    {   "..................xxxxxx..", ".................xxxxxxxx.", "x..x..xxxxxxxxxxxxxwwxxxxx", "xxxxxxxxxxxxxxxxxxxwkxxxxx",
        "xxxxxxxxxxxxxxxxxxxxxxxxxx", ".xxxxxxxxxxxxxxxxxxxxkkkk.", "..xxxxxxllllllxxxxxxxx....", "...xxxxxllllllxxx.xx......",
        "....xxxxxxxxxxxx..........", ".....xx...xx..............", "....xx....xx..............", "..........xxx............." },
};
const char* const CACTUS_S[] = {
    "...cc...", "..cccC..", "..cccC..", "c.cccC..", "c.cccC.c", "cccccC.c", ".ccccCcc", "..cccCc.",
    "..cccC..", "..cccC..", "..cccC..", "..cccC..", "..cccC..", "..cccC..", "..cccC..", "..cccC..",
};
const char* const CACTUS_L[] = {
    "....cccc....", "...ccccCC...", "...ccccCC...", ".c.ccccCC...", "cc.ccccCC.c.", "cc.ccccCC.cc",
    "cc.ccccCC.cc", "cc.ccccCC.cc", "ccccccccC.cc", ".cccccccCccc", "...ccccCCcc.", "...ccccCC...",
    "...ccccCC...", "...ccccCC...", "...ccccCC...", "...ccccCC...", "...ccccCC...", "...ccccCC...",
    "...ccccCC...", "...ccccCC...", "...ccccCC...", "...ccccCC...",
};
const char* const BIRD[][12] = {
    {   ".....bb.............", "....bbbb............", "..b.bbbbb...........", ".bbbbbbbbbbbbbbbb...", "bbbbbbbbbbbbbbbbbbbb",
        "......bbbbbbbbbbbb..", ".......bbbbbbbb.....", "........bbbbb.......", "........bbbb........",
        ".........bbb........", "....................", "...................." },
    {   "....................", "....................", "..b.................", ".bbb................", "bbbbbbbbbbbbbbbbb...",
        "....bbbbbbbbbbbbbbbb", "......bbbbbbbbbb....", "......bbbbbbb.......", ".....bbbbb..........",
        "....bbbb............", "...bbb..............", "...................." },
};

uint16_t color(char ch) {
    switch (ch) {
    case 'x': return RGB(83, 83, 83);
    case 'k': return RGB(32, 33, 36);
    case 'w': return RGB(247, 247, 247);
    case 'l': return RGB(154, 160, 166);
    case 'c': return RGB(91, 140, 58);
    case 'C': return RGB(59, 97, 39);
    case 'b': return RGB(109, 109, 109);
    default:  return KEY_COLOR;
    }
}

M5Canvas dinoS[4], duckS[2], cactusS, cactusL, birdS[2];
bool built;

void buildArt() {
    for (int i = 0; i < 4; i++) makeSprite(dinoS[i], DINO[i], 20, 19, color);
    for (int i = 0; i < 2; i++) makeSprite(duckS[i], DUCK[i], 26, 12, color);
    makeSprite(cactusS, CACTUS_S, 8, 16, color);
    makeSprite(cactusL, CACTUS_L, 12, 22, color);
    for (int i = 0; i < 2; i++) makeSprite(birdS[i], BIRD[i], 20, 12, color);
    built = true;
}

// ---------------------------------------------------------------- the game
constexpr int GY = 196, DX = 30;                 // ground line; the dino's x
constexpr float GRAV = 2600, GRAV_HOLD = 1500, JUMP = 720, START_SPEED = 230, MAX_SPEED = 560;
constexpr int NIGHT_EVERY = 700;
constexpr int SCALE = 2;                         // the art is drawn at 2x: chunky pixels

enum Kind : uint8_t { CACTUS_SMALL, CACTUS_LARGE, BIRD_ };
struct Obstacle { bool on; Kind kind; float x; int y, n, w, h; };
Obstacle obs[4];
struct Cloud { float x; int y; } clouds[4];
float dy, vy, speed, dist, nextGap, anim, nightMix;
bool ducking, dead;
int score, lastMilestone;

void start() {
    if (!built) buildArt();
    for (auto& o : obs) o.on = false;
    for (int i = 0; i < 4; i++) clouds[i] = { (float)(40 + i * 90), 40 + rnd(0, 50) };
    dy = vy = 0;
    speed = START_SPEED;
    dist = 0;
    nextGap = 300;
    anim = 0;
    nightMix = 0;
    ducking = dead = false;
    score = lastMilestone = 0;
}

void spawn() {
    for (auto& o : obs) {
        if (o.on) continue;
        const bool bird = score > 300 && rnd(0, 3) == 0;
        if (bird) {
            const int heights[] = { 0, 26, 52 };      // low: jump; middle: duck or jump; high: run under
            o = { true, BIRD_, (float)W + 10, GY - heights[rnd(0, 2)] - 12 * SCALE, 1, 20 * SCALE, 12 * SCALE };
        } else {
            const bool large = rnd(0, 2) == 0;
            const int n = rnd(1, score > 200 ? 3 : 2);
            const int w = (large ? 12 : 8) * SCALE, h = (large ? 22 : 16) * SCALE;
            o = { true, large ? CACTUS_LARGE : CACTUS_SMALL, (float)W + 10, GY - h, n, w * n, h };
        }
        // The gap to the next one grows with the speed: there's always time to react.
        nextGap = o.w + speed * (0.55f + rnd(0, 60) / 100.0f);
        return;
    }
}

bool overlaps(int ax, int ay, int aw, int ah, const Obstacle& o) {
    constexpr int IN = 4;                         // a little forgiveness on every side
    return ax + aw - IN > o.x + IN && ax + IN < o.x + o.w - IN && ay + ah - IN > o.y + IN && ay + IN < o.y + o.h - IN;
}

uint16_t mix(uint16_t day, uint16_t night) {
    const float t = nightMix;
    const int r = ((day >> 11) * (1 - t) + (night >> 11) * t), g = (((day >> 5) & 63) * (1 - t) + ((night >> 5) & 63) * t),
              b = ((day & 31) * (1 - t) + (night & 31) * t);
    return (r << 11) | (g << 5) | b;
}

void draw() {
    const uint16_t sky = mix(RGB(247, 247, 247), RGB(32, 33, 46)), line = mix(RGB(83, 83, 83), RGB(170, 170, 180));
    gfx.fillSprite(sky);
    if (nightMix > 0.5f) {                        // night: moon and stars
        gfx.fillCircle(250, 50, 12, RGB(240, 240, 220));
        gfx.fillCircle(256, 46, 11, sky);
        for (int i = 0; i < 14; i++) gfx.drawPixel((i * 71 + (int)(dist / 30)) % W, 20 + (i * 37) % 110, RGB(220, 220, 230));
    }
    for (auto& c : clouds) {
        const uint16_t cc = mix(RGB(220, 222, 226), RGB(70, 72, 90));
        gfx.fillRoundRect(c.x, c.y, 46, 12, 6, cc);
        gfx.fillRoundRect(c.x + 10, c.y - 7, 24, 12, 6, cc);
    }
    gfx.drawFastHLine(0, GY - 2, W, line);
    for (int i = 0; i < 24; i++) {                // pebbles and bumps going by
        const int x = (int)((i * 53 - dist) ) % W;
        gfx.drawFastHLine(x < 0 ? x + W : x, GY + 2 + (i * 7) % 12, 2 + i % 3, line);
    }
    for (auto& o : obs) {
        if (!o.on) continue;
        if (o.kind == BIRD_) birdS[(int)(anim * 6) & 1].pushRotateZoom(&gfx, o.x + o.w / 2, o.y + o.h / 2, 0, SCALE, SCALE, KEY_COLOR);
        else
            for (int i = 0; i < o.n; i++) {
                M5Canvas& c = o.kind == CACTUS_LARGE ? cactusL : cactusS;
                const int cw = o.w / o.n;
                c.pushRotateZoom(&gfx, o.x + i * cw + cw / 2, o.y + o.h / 2, 0, SCALE, SCALE, KEY_COLOR);
            }
    }
    const int frame = dead ? 3 : dy < 0 ? 2 : ((int)(anim * 12) & 1);
    if (ducking && !dead) duckS[(int)(anim * 12) & 1].pushRotateZoom(&gfx, DX + 26, GY - 12 + dy, 0, SCALE, SCALE, KEY_COLOR);
    else dinoS[frame].pushRotateZoom(&gfx, DX + 20, GY - 19 + dy, 0, SCALE, SCALE, KEY_COLOR);

    char buf[32];
    snprintf(buf, sizeof(buf), "%05d", score);
    gfx.setFont(&fonts::Font4);
    gfx.setTextDatum(top_right);
    // At every 100 the score blinks for a moment, like the original.
    const bool blink = score - lastMilestone < 25 && lastMilestone && ((int)(anim * 8) & 1);
    gfx.setTextColor(blink ? sky : line);
    gfx.drawString(buf, W - 8, 6);
}

bool frame(const Input& in, float dt) {
    anim += dt;
    if (dead) { draw(); return false; }
    const bool onGround = dy >= 0;
    const bool jump = in.aP || (in.upP && onGround);
    if (jump && onGround) { vy = -JUMP; sfx(880, 30); }
    ducking = in.down && onGround;
    const float g = in.down ? GRAV * 3 : ((in.a || in.up) && vy < 0) ? GRAV_HOLD : GRAV;
    vy += g * dt;
    dy += vy * dt;
    if (dy >= 0) { dy = 0; vy = 0; }

    speed = min(MAX_SPEED, speed + 7 * dt);
    const float step = speed * dt;
    dist += step;
    score = (int)(dist / 12);
    if (score / 100 > lastMilestone / 100) { lastMilestone = score / 100 * 100; sfx(1400, 60); }
    // Day <-> night every NIGHT_EVERY points, fading over a second or so.
    const bool night = (score / NIGHT_EVERY) & 1;
    nightMix = constrain(nightMix + (night ? dt : -dt), 0.0f, 1.0f);

    for (auto& c : clouds) { c.x -= step * 0.2f; if (c.x < -50) { c.x = W + rnd(0, 80); c.y = 30 + rnd(0, 60); } }
    nextGap -= step;
    if (nextGap <= 0) spawn();
    for (auto& o : obs) {
        if (!o.on) continue;
        o.x -= step * (o.kind == BIRD_ ? 1.12f : 1);   // birds fly towards you a bit faster
        if (o.x + o.w < 0) { o.on = false; continue; }
        const bool hit = ducking ? overlaps(DX, GY - 12 * SCALE + (int)dy, 26 * SCALE, 12 * SCALE, o)
                                 : overlaps(DX + 8, GY - 19 * SCALE + (int)dy, 12 * SCALE, 19 * SCALE, o);
        if (hit) { dead = true; sfx(200, 300); flash(255, 0, 0, 500); }
    }
    draw();
    return !dead;
}

int score_() { return score; }
}   // namespace

const Game GAME_DINO = { "Dino Run", "A or up: jump (hold: higher). Down: duck.", RGB(154, 160, 166), start, frame, score_, nullptr };
