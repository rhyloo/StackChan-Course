// Super Stack-chan: a side-scrolling platform level, a homage to the 8-bit classics
// (original art: the hero is a little Stack-chan).
//
// The leaf gives Stack-chan raccoon ears and a tail:
//   - run with B held on the ground until the P meter is full, then keep tapping A in
//     the air: it flies for a few seconds;
//   - without a full P meter, tapping A while falling glides: it falls slowly;
//   - B on its own swings the tail: it knocks enemies over and breaks bricks.
// Without the tail, a hit costs a life; with it, the tail. Stomp on enemies to beat them.
// Joystick: walk (analog). A: jump (hold it: higher). B: run / tail swing.
#include "arcade.h"
#include "platformer_art.h"

namespace {
// ---------------------------------------------------------------- the level
constexpr int T = 16, LW = 240, LHT = 24, LH = LHT * T, GROUND = 21;   // tiles; the ground's top row
enum Tile : uint8_t { EMPTY, GRASS, DIRT, BRICK, QCOIN, QLEAF, USED, BLOCK, PIPE_TL, PIPE_TR, PIPE_L, PIPE_R,
                      CLOUD, COIN, POLE, POLE_TOP, BUSH_L, BUSH_R, TILES };   // from BUSH_L: scenery
uint8_t map_[LHT][LW];

bool solid(uint8_t t) { return t == GRASS || t == DIRT || t == BRICK || t == QCOIN || t == QLEAF || t == USED || t == BLOCK || (t >= PIPE_TL && t <= PIPE_R); }

void ground(int x0, int x1) {
    for (int x = x0; x <= x1; x++) { map_[GROUND][x] = GRASS; for (int y = GROUND + 1; y < LHT; y++) map_[y][x] = DIRT; }
}
void put(int x, int y, Tile t) { map_[y][x] = t; }
void row(int x0, int x1, int y, Tile t) { for (int x = x0; x <= x1; x++) map_[y][x] = t; }
void pipe(int x, int h) {
    const int top = GROUND - h;
    put(x, top, PIPE_TL); put(x + 1, top, PIPE_TR);
    for (int y = top + 1; y < GROUND; y++) { put(x, y, PIPE_L); put(x + 1, y, PIPE_R); }
}
void stairs(int x, int n, bool up) {
    for (int i = 0; i < n; i++)
        for (int h = 0; h <= (up ? i : n - 1 - i); h++) put(x + i, GROUND - 1 - h, BLOCK);
}

struct Spawn { int16_t x, y; uint8_t kind; };
enum Kind : uint8_t { WALKER, BEE, LEAF, POP, BIT, DUST };   // WALKER: the slime
const Spawn SPAWNS[] = {
    { 22, 20, WALKER }, { 30, 20, WALKER }, { 48, 20, WALKER }, { 56, 20, WALKER }, { 59, 20, WALKER },
    { 90, 20, WALKER }, { 104, 20, WALKER }, { 118, 20, WALKER }, { 112, 9, BEE }, { 128, 5, BEE },
    { 148, 20, WALKER }, { 156, 20, WALKER }, { 160, 20, WALKER }, { 175, 20, WALKER }, { 195, 20, WALKER },
};
constexpr int GOAL_X = 224, CHECKPOINT_X = 96;

void buildLevel() {
    memset(map_, 0, sizeof(map_));
    // 1. The start: blocks to learn, the first leaf, two pipes and a pit.
    ground(0, 35);
    put(10, 17, QCOIN);
    put(14, 17, BRICK); put(15, 17, QLEAF); put(16, 17, BRICK); put(17, 17, QCOIN); put(18, 17, BRICK);
    put(16, 13, QCOIN);
    pipe(26, 2);
    pipe(32, 3);
    // 2. Bricks with coins over them, a tall pipe, stairs over a pit.
    ground(39, 67);
    row(44, 50, 17, BRICK); put(47, 17, QCOIN);
    row(44, 50, 15, COIN);
    pipe(62, 4);
    stairs(64, 4, true);
    ground(72, 134);
    stairs(72, 4, false);
    // 3. The runway: long and flat to fill the P meter, and the sky above it with
    //    clouds and coins that only a flying Stack-chan reaches.
    row(98, 110, 7, CLOUD); row(99, 109, 6, COIN);
    for (int i = 0; i < 12; i++) put(113 + i, 3 + abs(i - 6) / 2, COIN);   // an arc of coins
    row(126, 134, 4, CLOUD); row(127, 133, 3, COIN);
    put(131, 2, QLEAF);
    row(84, 88, 17, COIN);
    // 4. A pit, bricks to break with the tail, the second leaf.
    ground(138, 180);
    row(142, 146, 17, BRICK); put(144, 17, QLEAF);
    row(150, 156, 16, BRICK); row(150, 156, 15, COIN);
    pipe(168, 3);
    // 5. Clouds over a long pit, the last stairs and the goal.
    row(182, 188, 18, CLOUD);
    ground(192, LW - 1);
    stairs(208, 8, true);
    row(208, 215, GROUND - 9, COIN);
    for (int y = 9; y < GROUND; y++) put(GOAL_X, y, POLE);
    put(GOAL_X, 8, POLE_TOP);
    // Bushes on the ground wherever there's room for one.
    for (int x : { 4, 20, 41, 53, 78, 93, 121, 140, 162, 176, 196, 203, 230 })
        if (!map_[GROUND - 1][x] && !map_[GROUND - 1][x + 1] && map_[GROUND][x] == GRASS && map_[GROUND][x + 1] == GRASS) {
            put(x, GROUND - 1, BUSH_L);
            put(x + 1, GROUND - 1, BUSH_R);
        }
}

// ---------------------------------------------------------------- art (platformer_art.h)
uint16_t color(char ch) {
    switch (ch) {
    case 'k': return RGB(0x18, 0x14, 0x2A); case 'w': return RGB(0xFC, 0xFC, 0xFC); case 'l': return RGB(0xC8, 0xCC, 0xDC);
    case 'g': return RGB(0x82, 0x86, 0xA0); case 's': return RGB(0x24, 0x28, 0x3F); case 'c': return RGB(0x6E, 0xE6, 0xFF);
    case 'C': return RGB(0x28, 0x96, 0xC8); case 'r': return RGB(0xE4, 0x34, 0x34); case 'R': return RGB(0x96, 0x18, 0x28);
    case 'o': return RGB(0xF8, 0x94, 0x28); case 'O': return RGB(0xC4, 0x5C, 0x14); case 'y': return RGB(0xFF, 0xD6, 0x40);
    case 'Y': return RGB(0xD6, 0x8C, 0x14); case 'n': return RGB(0xF8, 0xC8, 0x96); case 'b': return RGB(0xB0, 0x68, 0x30);
    case 'B': return RGB(0x6C, 0x3C, 0x1C); case 'd': return RGB(0x84, 0x4C, 0x24); case 'G': return RGB(0x50, 0xB8, 0x48);
    case 'D': return RGB(0x20, 0x78, 0x38); case 'L': return RGB(0xA0, 0xE4, 0x70); case 'u': return RGB(0x48, 0x80, 0xF0);
    case 'U': return RGB(0xB4, 0xD8, 0xFF); case 'f': return RGB(0x96, 0x68, 0x48); case 'F': return RGB(0x5A, 0x3C, 0x28);
    case 'm': return RGB(0xFA, 0xE8, 0xC8); case 'p': return RGB(0xFA, 0x8C, 0xAA); case 'v': return RGB(0x9A, 0x50, 0xE0);
    case 'V': return RGB(0x5E, 0x2A, 0x9C); case 'P': return RGB(0xD2, 0xAA, 0xF8);
    default:  return KEY_COLOR;
    }
}

constexpr uint16_t SKY = RGB(96, 160, 252);
M5Canvas tiles[TILES], qblock[3], coinS[4];
M5Canvas heroS[2][HF_COUNT], earsS[2], tailS[2];          // [0] looking right, [1] left
M5Canvas slimeS[2], slimeFlat, beeS[2][2], leafS;
bool built;

void buildArt() {
    const struct { uint8_t t; const char* const* art; } TILE_ART[] = {
        { GRASS, TILE_GRASS }, { DIRT, TILE_DIRT }, { BRICK, TILE_BRICK }, { USED, TILE_USED }, { BLOCK, TILE_BLOCK },
        { PIPE_TL, TILE_PIPE_TL }, { PIPE_TR, TILE_PIPE_TR }, { PIPE_L, TILE_PIPE_L }, { PIPE_R, TILE_PIPE_R },
        { CLOUD, TILE_CLOUD }, { POLE, TILE_POLE }, { POLE_TOP, TILE_POLE_TOP }, { BUSH_L, TILE_BUSH_L }, { BUSH_R, TILE_BUSH_R },
    };
    for (const auto& a : TILE_ART) makeSprite(tiles[a.t], a.art, T, T, color);
    for (int i = 0; i < 3; i++) makeSprite(qblock[i], TILE_QBLOCK[i], T, T, color);
    for (int i = 0; i < 4; i++) makeSprite(coinS[i], COIN_FRAMES[i], T, T, color);
    for (int f = 0; f < 2; f++) {
        for (int i = 0; i < HF_COUNT; i++) makeSprite(heroS[f][i], HERO[i], 16, 22, color, f);
        makeSprite(earsS[f], EARS, 16, 5, color, f);
        makeSprite(tailS[f], TAIL, 12, 8, color, f);
        for (int i = 0; i < 2; i++) makeSprite(beeS[f][i], BEE_ART[i], 16, 16, color, !f);   // the bee art looks left
    }
    for (int i = 0; i < 2; i++) makeSprite(slimeS[i], SLIME[i], 16, 16, color);
    makeSprite(slimeFlat, SLIME_FLAT, 16, 16, color);
    makeSprite(leafS, LEAF_ART, 16, 16, color);
    built = true;
}

// ---------------------------------------------------------------- the hero
constexpr float WALK = 95, RUN = 165, PSPEED = 205, ACC = 420, AIR_ACC = 300, FRICTION = 520;
constexpr float GRAV = 1300, GRAV_HOLD = 650, JUMP = 300, MAXFALL = 380;
constexpr float FLY_VY = -170, GLIDE_VY = 55, FLY_TIME = 4.0f, P_FILL_S = 0.9f;
constexpr int PW = 12, PH = 19;                 // the hitbox; the drawing is 16x22

struct Hero {
    float x, y, vx, vy;
    bool ground, left, tail;
    float p, fly, glide, spin, hurt, dead, anim;
    float sx, sy;                                // squash and stretch: 1 = normal
} h;
float camX, camY, timeLeft, clearT;
int coins, score, lives, checkpoint;
bool cleared;

struct Thing { uint8_t kind; bool on, squashed; float x, y, vx, vy, t, x0, y0; };
Thing things[40];
struct Bump { int x, y; float t; } bump;

Thing* spawn(uint8_t kind, float x, float y) {
    for (auto& e : things)
        if (!e.on) { e = { kind, true, false, x, y, 0, 0, 0, x, y }; return &e; }
    return nullptr;
}

uint8_t tileAt(float px, float py) {
    const int tx = (int)floorf(px / T), ty = (int)floorf(py / T);
    if (tx < 0 || tx >= LW) return BLOCK;       // the ends of the level are walls
    if (ty < 0 || ty >= LHT) return EMPTY;
    return map_[ty][tx];
}

void respawn() {
    h = {};
    h.sx = h.sy = 1;
    h.x = checkpoint * T;
    h.y = (GROUND - 2) * T;
    camX = max(0.0f, h.x - 140);
    camY = LH - H;
    timeLeft = max(timeLeft, 100.0f);
}

void start() {
    if (!built) buildArt();
    buildLevel();
    for (auto& e : things) e.on = false;
    for (const Spawn& s : SPAWNS) {
        Thing* e = spawn(s.kind, s.x * T, s.y * T);
        if (e && s.kind == WALKER) e->vx = -35;
    }
    coins = score = 0;
    lives = 3;
    checkpoint = 2;
    cleared = false;
    clearT = 0;
    timeLeft = 300;
    bump.t = 0;
    respawn();
}

void addCoin() {
    coins++;
    score += 100;
    sfx(1976, 40);
    if (coins % 50 == 0) { lives++; sfx(2500, 150); }
}

// The hero's head hit the block at (tx, ty) from below (or the tail did, from the side).
void hitBlock(int tx, int ty, bool byTail) {
    const uint8_t t = map_[ty][tx];
    if (t == QCOIN || t == QLEAF) {
        map_[ty][tx] = USED;
        bump = { tx, ty, 0.15f };
        if (t == QCOIN) { addCoin(); spawn(POP, tx * T, (ty - 1) * T); }
        else { Thing* l = spawn(LEAF, tx * T, (ty - 1) * T); if (l) { l->vy = -90; l->t = 0; } sfx(880, 80); }
    } else if (t == BRICK) {
        if (h.tail || byTail) {
            map_[ty][tx] = EMPTY;
            score += 50;
            for (int i = 0; i < 4; i++) {
                Thing* b = spawn(BIT, tx * T + (i & 1) * 8, ty * T + (i / 2) * 8);
                if (b) { b->vx = (i & 1 ? 60 : -60); b->vy = (i < 2 ? -260 : -180); }
            }
            sfx(180, 60);
            flash(255, 100, 0, 80);
        } else {
            bump = { tx, ty, 0.15f };
            sfx(250, 30);
        }
    }
    // An enemy standing on a bumped block gets knocked over.
    for (auto& e : things)
        if (e.on && e.kind == WALKER && !e.squashed && fabsf(e.x + 8 - (tx * T + 8)) < 14 && fabsf(e.y + 16 - ty * T) < 4) {
            e.squashed = true; e.t = 0.5f; score += 100;
        }
}

void hurt() {
    if (h.hurt > 0 || h.dead > 0) return;
    if (h.tail) { h.tail = false; h.hurt = 2; sfx(300, 200); flash(255, 120, 0, 300); return; }
    h.dead = 2;
    h.vy = -350;
    h.vx = 0;
    sfx(150, 600);
    flash(255, 0, 0, 800);
}

void moveHero(const Input& in, float dt) {
    const bool running = in.b;
    // P meter: running flat out on the ground fills it; flying uses it up.
    if (h.ground) {
        if (running && fabsf(h.vx) >= RUN * 0.92f) h.p = min(1.0f, h.p + dt / P_FILL_S);
        else h.p = max(0.0f, h.p - dt * 1.5f);
    }
    const float maxS = running ? (h.p >= 1 ? PSPEED : RUN) : WALK;
    const float target = in.x * maxS;
    const float acc = h.ground ? (in.x ? ACC : FRICTION) : AIR_ACC;
    if (h.vx < target) h.vx = min(target, h.vx + acc * dt);
    else if (h.vx > target) h.vx = max(target, h.vx - acc * dt);
    if (in.x < -0.1f) h.left = true;
    if (in.x > 0.1f) h.left = false;

    if (in.aP && h.ground) {
        h.vy = -(JUMP + fabsf(h.vx) * 0.3f);
        h.ground = false;
        h.sx = 0.8f; h.sy = 1.22f;               // stretch up as it takes off
        sfx(660, 40);
    } else if (in.aP && h.tail) {
        if (h.p >= 1 || h.fly > 0) {             // flying: every tap goes up
            if (h.fly <= 0) h.fly = FLY_TIME;
            h.vy = FLY_VY;
            sfx(500, 30);
        } else {
            h.glide = 0.28f;                     // gliding: falls slowly for a moment
        }
    }
    if (in.bP && h.tail && h.spin <= 0) {        // tail swing
        h.spin = 0.3f;
        sfx(400, 40);
        const int tx = (int)((h.x + PW / 2 + (h.left ? -14 : 14)) / T), ty = (int)((h.y + PH / 2) / T);
        if (ty >= 0 && ty < LHT && tx >= 0 && tx < LW) hitBlock(tx, ty, true);
        for (auto& e : things)
            if (e.on && (e.kind == WALKER || e.kind == BEE) && !e.squashed && fabsf(e.x + 8 - (h.x + PW / 2)) < 26 &&
                fabsf(e.y + 8 - (h.y + PH / 2)) < 14) {
                e.squashed = true; e.t = 0.5f; e.vy = -200; score += 200; sfx(1200, 40);
            }
    }
    if (h.fly > 0) { h.fly -= dt; if (h.fly <= 0) h.p = 0; }
    if (h.glide > 0) h.glide -= dt;
    if (h.spin > 0) h.spin -= dt;

    const float g = (in.a && h.vy < 0) ? GRAV_HOLD : GRAV;   // hold A: a higher jump
    h.vy = min(MAXFALL, h.vy + g * dt);
    if (h.glide > 0 && h.vy > GLIDE_VY) h.vy = GLIDE_VY;

    // Horizontal, then vertical, each against the tiles.
    h.x += h.vx * dt;
    for (float yy : { h.y + 1, h.y + PH - 1 }) {
        if (h.vx > 0 && solid(tileAt(h.x + PW, yy))) { h.x = floorf((h.x + PW) / T) * T - PW - 0.01f; h.vx = 0; }
        if (h.vx < 0 && solid(tileAt(h.x, yy))) { h.x = (floorf(h.x / T) + 1) * T; h.vx = 0; }
    }
    const float oldBottom = h.y + PH, fallSpeed = h.vy;
    const bool wasGround = h.ground;
    h.y += h.vy * dt;
    h.ground = false;
    if (h.vy >= 0) {
        for (float xx : { h.x + 1, h.x + PW - 1 }) {
            const uint8_t t = tileAt(xx, h.y + PH);
            const float top = floorf((h.y + PH) / T) * T;
            if (solid(t) || (t == CLOUD && oldBottom <= top + 1)) { h.y = top - PH; h.vy = 0; h.ground = true; }
        }
    } else {
        // The head: the tile over its middle is the one it bumps.
        const float cx = h.x + PW / 2;
        if (solid(tileAt(h.x + 1, h.y)) || solid(tileAt(h.x + PW - 1, h.y))) {
            const int ty = (int)floorf(h.y / T);
            int tx = (int)floorf(cx / T);
            if (!solid(tileAt(cx, h.y))) tx = solid(tileAt(h.x + 1, h.y)) ? (int)floorf((h.x + 1) / T) : (int)floorf((h.x + PW - 1) / T);
            h.y = (ty + 1) * T;
            h.vy = 0;
            if (ty >= 0) hitBlock(tx, ty, false);
        }
    }
    if (h.ground && !wasGround && fallSpeed > 180) {   // landing: squash, and a puff of dust
        h.sx = 1.3f; h.sy = 0.72f;
        for (int s = -1; s <= 1; s += 2) {
            Thing* d = spawn(DUST, h.x + PW / 2 + s * 6, h.y + PH - 4);
            if (d) d->vx = s * 40;
        }
    }
    // Skidding: pushing against the way it runs, fast, on the ground.
    if (h.ground && fabsf(h.vx) > WALK && ((h.vx > 0 && in.x < -0.3f) || (h.vx < 0 && in.x > 0.3f)) && ((int)(h.anim * 20) % 3 == 0)) {
        Thing* d = spawn(DUST, h.x + PW / 2, h.y + PH - 4);
        if (d) d->vx = h.vx > 0 ? -20 : 20;
    }
    if (h.y > LH) { h.tail = false; h.dead = 2; h.vy = 0; sfx(150, 600); }

    // Coins it touches.
    for (float yy : { h.y + 2, h.y + PH - 2 })
        for (float xx : { h.x + 2, h.x + PW - 2 }) {
            const int tx = (int)(xx / T), ty = (int)(yy / T);
            if (tx >= 0 && tx < LW && ty >= 0 && ty < LHT && map_[ty][tx] == COIN) { map_[ty][tx] = EMPTY; addCoin(); }
        }
    if (h.x / T > CHECKPOINT_X) checkpoint = CHECKPOINT_X;
    if (h.x + PW >= GOAL_X * T + 7 && !cleared) {   // the goal: the higher, the more points
        cleared = true;
        clearT = 3;
        const int heightBonus = max(0, (GROUND - (int)(h.y / T))) * 400;
        score += heightBonus + (int)timeLeft * 10;
        sfx(1568, 500);
        flash(0, 255, 0, 2000);
    }
}

void moveThings(float dt) {
    for (auto& e : things) {
        if (!e.on) continue;
        // Enemies far from the screen wait: the level wakes up as you walk.
        if ((e.kind == WALKER || e.kind == BEE) && fabsf(e.x - camX - W / 2) > W) continue;
        e.t += dt;
        switch (e.kind) {
        case WALKER:
            if (e.squashed) { e.t -= 2 * dt; e.vy += GRAV * dt; e.y += e.vy * dt; if (e.t < -0.6f || e.y > LH) e.on = false; break; }
            e.vy = min(MAXFALL, e.vy + GRAV * dt);
            e.x += e.vx * dt;
            if (solid(tileAt(e.vx < 0 ? e.x : e.x + 15, e.y + 8))) { e.vx = -e.vx; e.x += e.vx * dt * 2; }
            e.y += e.vy * dt;
            if (solid(tileAt(e.x + 8, e.y + 16))) { e.y = floorf((e.y + 16) / T) * T - 16; e.vy = 0; }
            if (e.y > LH) e.on = false;
            break;
        case BEE:
            if (e.squashed) { e.vy += GRAV * dt; e.y += e.vy * dt; if (e.y > LH) e.on = false; break; }
            e.x = e.x0 + 40 * sinf(e.t * 1.3f);
            e.y = e.y0 + 18 * sinf(e.t * 2.6f);
            break;
        case LEAF:                                   // out of the block, then down, swinging
            if (e.t < 0.35f) e.y += e.vy * dt;
            else { e.y += 35 * dt; e.x = e.x0 + 22 * sinf((e.t - 0.35f) * 3); }
            if (e.y > LH) e.on = false;
            if (fabsf(e.x + 8 - (h.x + PW / 2)) < 13 && fabsf(e.y + 8 - (h.y + PH / 2)) < 14) {
                e.on = false;
                h.tail = true;
                score += 1000;
                sfx(1319, 200);
                flash(255, 160, 0, 500);
            }
            break;
        case POP:
            e.y -= 120 * dt;
            if (e.t > 0.35f) e.on = false;
            break;
        case BIT:
            e.vy += GRAV * dt; e.x += e.vx * dt; e.y += e.vy * dt;
            if (e.y > LH) e.on = false;
            break;
        case DUST:
            e.x += e.vx * dt; e.y -= 12 * dt;
            if (e.t > 0.35f) e.on = false;
            break;
        }
        // Touching an enemy: from above it's a stomp, otherwise it hurts.
        if ((e.kind == WALKER || e.kind == BEE) && !e.squashed && h.dead <= 0 &&
            h.x + PW > e.x + 2 && h.x < e.x + 14 && h.y + PH > e.y + 2 && h.y < e.y + 15) {
            if (h.vy > 0 && h.y + PH - e.y < 10) {
                e.squashed = true;
                e.t = 0.5f;
                h.vy = -260;
                score += 100;
                sfx(900, 40);
            } else hurt();
        }
    }
}

// ---------------------------------------------------------------- drawing
void drawSky() {
    gfx.fillSprite(SKY);
    // Far hills, then big pixel clouds: both move slower than the level (depth).
    const int base = (int)(LH - camY) - 2 * T;
    for (int i = -1; i < 5; i++) {
        const int x = i * 150 - ((int)(camX * 0.25f) % 150) + 70, r = 58 + (i & 1) * 22;
        gfx.fillEllipse(x, base, r, r * 3 / 4 + 10, RGB(128, 208, 120));
        gfx.drawEllipse(x, base, r, r * 3 / 4 + 10, RGB(32, 120, 56));
        gfx.drawEllipse(x, base, r - 1, r * 3 / 4 + 9, RGB(32, 120, 56));
        for (int d = 0; d < 3; d++)                // a few darker spots
            gfx.fillRect(x - r / 3 + d * r / 3, base - r / 2 + (d & 1) * 10, 4, 6, RGB(80, 168, 88));
    }
    for (int i = -1; i < 4; i++) {
        const int x = i * 170 - ((int)(camX * 0.45f) % 170) + 40, y = 36 + (i & 1) * 34 - (int)(camY * 0.35f);
        tiles[CLOUD].pushRotateZoom(&gfx, x, y, 0, 2, 2, KEY_COLOR);
        tiles[CLOUD].pushRotateZoom(&gfx, x + 26, y + 4, 0, 2, 2, KEY_COLOR);
    }
}

void drawTiles() {
    const int tx0 = (int)(camX / T), ty0 = (int)(camY / T);
    // The star blocks gleam now and then; the coins turn.
    static const uint8_t GLEAM[] = { 0, 0, 0, 0, 0, 0, 1, 2 };
    const int gleam = GLEAM[(int)(h.anim * 10) % 8], coinF = (int)(h.anim * 8) % 4;
    for (int ty = ty0; ty <= ty0 + H / T + 1 && ty < LHT; ty++)
        for (int tx = tx0; tx <= tx0 + W / T + 1 && tx < LW; tx++) {
            const uint8_t t = map_[ty][tx];
            if (!t) continue;
            const int sx = tx * T - (int)camX;
            int sy = ty * T - (int)camY;
            if (bump.t > 0 && bump.x == tx && bump.y == ty) sy -= (int)(sinf(bump.t / 0.15f * PI) * 5);
            M5Canvas& c = t == QCOIN || t == QLEAF ? qblock[gleam] : t == COIN ? coinS[coinF] : tiles[t];
            c.pushSprite(&gfx, sx, sy, KEY_COLOR);
        }
}

void drawThings() {
    for (auto& e : things) {
        if (!e.on) continue;
        const int x = (int)(e.x - camX), y = (int)(e.y - camY);
        if (x < -20 || x > W + 4 || y < -20 || y > H + 4) continue;
        switch (e.kind) {
        case WALKER:
            if (e.squashed && e.vy == 0) slimeFlat.pushSprite(&gfx, x, y, KEY_COLOR);
            else if (e.squashed) slimeS[0].pushRotateZoom(&gfx, x + 8, y + 8, 180, 1, 1, KEY_COLOR);   // knocked over
            else slimeS[(int)(e.t * 4) & 1].pushSprite(&gfx, x, y, KEY_COLOR);
            break;
        case BEE: {
            const bool right = cosf(e.t * 1.3f) > 0;          // the way it is flying now
            if (e.squashed) beeS[right][0].pushRotateZoom(&gfx, x + 8, y + 8, 180, 1, 1, KEY_COLOR);
            else beeS[right][(int)(e.t * 16) & 1].pushSprite(&gfx, x, y, KEY_COLOR);
            break;
        }
        case LEAF:
            leafS.pushRotateZoom(&gfx, x + 8, y + 8, e.t < 0.35f ? 0 : 25 * sinf((e.t - 0.35f) * 3), 1, 1, KEY_COLOR);
            break;
        case POP:
            coinS[(int)(e.t * 16) % 4].pushSprite(&gfx, x, y, KEY_COLOR);
            break;
        case BIT:
            gfx.fillRect(x, y, 6, 6, RGB(0xC4, 0x5C, 0x14));
            gfx.drawRect(x, y, 6, 6, RGB(0x6C, 0x3C, 0x1C));
            break;
        case DUST: {
            const int r = 2 + (int)(e.t * 14);
            gfx.fillCircle(x, y, r, e.t < 0.2f ? RGB(240, 240, 240) : RGB(200, 210, 230));
            break;
        }
        }
    }
}

// A sprite drawn with the hero's squash and stretch, its feet at (cx, bottom).
void pushScaled(M5Canvas& c, float cx, float bottom, float top, int h_, float angle = 0) {
    const bool plain = fabsf(h.sx - 1) < 0.04f && fabsf(h.sy - 1) < 0.04f && angle == 0;
    const float cy = bottom - (22 - top - h_ / 2.0f) * h.sy;
    if (plain) c.pushSprite(&gfx, (int)lroundf(cx - c.width() / 2.0f), (int)lroundf(cy - h_ / 2.0f), KEY_COLOR);
    else c.pushRotateZoom(&gfx, cx, cy, angle, h.sx, h.sy, KEY_COLOR);
}

void drawHero() {
    if (h.hurt > 0 && ((int)(h.hurt * 15) & 1)) return;   // blinking: can't be hurt now
    const float cx = h.x + PW / 2 - camX, bottom = h.y + PH - camY;
    const int f = h.left ? 1 : 0;
    int frame = HF_IDLE;
    if (h.dead > 0) frame = HF_DEAD;
    else if (h.fly > 0 || h.glide > 0) frame = HF_FLY;
    else if (!h.ground) frame = h.vy < 0 ? HF_JUMP : HF_FALL;
    else if (fabsf(h.vx) > 10) {
        static const uint8_t RUN[] = { HF_RUN1, HF_RUN2, HF_RUN3, HF_RUN2 };
        frame = h.p >= 1 ? ((int)(h.anim * 16) & 1 ? HF_DASH1 : HF_DASH2)
                         : RUN[(int)(h.anim * (6 + fabsf(h.vx) / 14)) % 4];
    } else if (fmodf(h.anim, 3.0f) > 2.85f) frame = HF_BLINK;
    if (h.tail) {   // the tail behind, wagging when it glides, flies or swings
        const float wag = h.spin > 0 ? sinf(h.spin * 40) * 40 : (h.glide > 0 || h.fly > 0) ? sinf(h.anim * 30) * 25 : sinf(h.anim * 4) * 6;
        const float tx = cx + (h.left ? 9 : -9);
        tailS[f].pushRotateZoom(&gfx, tx, bottom - 7 * h.sy, h.left ? -wag : wag, h.sx, h.sy, KEY_COLOR);
    }
    pushScaled(heroS[f][frame], cx, bottom, 0, 22);
    if (h.tail) pushScaled(earsS[f], cx, bottom, 0, 5);
    if (h.spin > 0) gfx.drawArc(cx, bottom - 10, 16, 14, h.left ? 90 : 270, h.left ? 270 : 450, RGB(255, 255, 255));
}

void drawHud() {
    gfx.fillRect(0, 0, W, 17, RGB(24, 20, 42));
    gfx.drawFastHLine(0, 17, W, RGB(10, 8, 20));
    char buf[48];
    snprintf(buf, sizeof(buf), "%06d", score);
    hudText(buf, nullptr);
    coinS[0].pushRotateZoom(&gfx, 66, 8, 0, 0.75f, 0.75f, KEY_COLOR);
    snprintf(buf, sizeof(buf), "x%02d", coins);
    gfx.setTextDatum(top_left);
    gfx.drawString(buf, 74, 0);
    // P meter: six arrows and a P that blinks when full.
    for (int i = 0; i < 6; i++) {
        const bool lit = h.p * 6 > i + 0.5f || h.fly > 0;
        const int x = 112 + i * 9;
        gfx.fillTriangle(x, 4, x, 12, x + 6, 8, lit ? RGB(255, 235, 59) : RGB(80, 80, 100));
    }
    const bool full = h.p >= 1 || h.fly > 0;
    gfx.setTextColor(full && ((millis() / 120) & 1) ? RGB(255, 82, 82) : RGB(80, 80, 100));
    gfx.drawString("P", 168, 0);
    snprintf(buf, sizeof(buf), "LIVES %d  TIME %03d", lives, max(0, (int)timeLeft));
    hudText(nullptr, buf);
}

void draw() {
    drawSky();
    drawTiles();
    drawThings();
    drawHero();
    drawHud();
    if (cleared) {
        gfx.setFont(&fonts::FreeSansBold18pt7b);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(RGB(24, 20, 42));
        gfx.drawString("COURSE CLEAR!", W / 2 + 2, 92);
        gfx.setTextColor(RGB(255, 235, 59));
        gfx.drawString("COURSE CLEAR!", W / 2, 90);
    }
}

bool frame(const Input& in, float dt) {
    if (bump.t > 0) bump.t -= dt;
    h.anim += dt;
    const float ease = min(1.0f, dt * 12);       // squash and stretch go back to normal
    h.sx += (1 - h.sx) * ease;
    h.sy += (1 - h.sy) * ease;
    if (cleared) {                              // walk off to the right, then the end
        h.vx = WALK;
        h.x += h.vx * dt;
        h.vy = min(MAXFALL, h.vy + GRAV * dt);
        h.y += h.vy * dt;
        if (solid(tileAt(h.x + 6, h.y + PH))) { h.y = floorf((h.y + PH) / T) * T - PH; h.vy = 0; h.ground = true; }
        clearT -= dt;
        draw();
        return clearT > 0;
    }
    if (h.dead > 0) {                           // the fall off the screen, then try again
        h.dead -= dt;
        h.vy += GRAV * dt;
        h.y += h.vy * dt;
        draw();
        if (h.dead <= 0) {
            if (--lives <= 0) return false;
            respawn();
        }
        return true;
    }
    timeLeft -= dt;
    if (timeLeft <= 0) { h.tail = false; h.hurt = 0; hurt(); }   // out of time: a life, tail or not
    if (h.hurt > 0) h.hurt -= dt;
    moveHero(in, dt);
    moveThings(dt);

    // The camera: ahead of the hero, and up when it flies.
    const float wantX = h.x - (h.left ? 180 : 120);
    camX += (wantX - camX) * min(1.0f, dt * 6);
    camX = constrain(camX, 0.0f, (float)(LW * T - W));
    if (h.y - camY < 70) camY = h.y - 70;
    if (h.y - camY > 170) camY = h.y - 170;
    camY = constrain(camY, 0.0f, (float)(LH - H));
    draw();
    return true;
}

int score_() { return score; }
const char* title() { return cleared ? "COURSE CLEAR!" : "GAME OVER"; }
}   // namespace

const Game GAME_PLATFORMER = { "Super Stack-chan", "A: jump. B: run, tail. P full + A: fly!", RGB(255, 112, 67),
                               start, frame, score_, title };
