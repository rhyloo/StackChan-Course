// Demo 17 - Arcade: classic games on Stack-chan, played with a joystick and two buttons.
//
// main.cpp is the cabinet: the menu, the controls screen, the pause, the game over screen
// and the high scores (kept in the flash). Each game is one file in games/ (see arcade.h).
// Every frame is drawn on a full-screen canvas and pushed at once: no flicker.
//
//   Menu        joystick up/down and A, or tap a game
//   Playing     the game's controls; SW (joystick click) or a tap: pause
//   Pause       A: go on, B: back to the menu
//   Game over   A: again, B: menu
#include <M5Unified.h>
#include <Preferences.h>
#include "board.h"
#include "arcade.h"

static constexpr uint32_t FRAME_MS = 33;        // ~30 fps: pushing 320x240 over SPI takes most of it
static constexpr uint8_t BRIGHTNESS = 150, VOLUME = 90;

static const Game* const GAMES[] = { &GAME_PLATFORMER, &GAME_DINO, &GAME_SNAKE, &GAME_BREAKOUT, &GAME_TETRIS, &GAME_INVADERS };
static constexpr int GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);
static constexpr int MENU_ITEMS = GAME_COUNT + 1;   // + the controls screen

M5Canvas gfx(&M5.Display);
static Preferences s_prefs;
static int s_best[GAME_COUNT];

enum class Mode { menu, controls, play, pause, over };
static Mode s_mode = Mode::menu;
static int s_sel, s_game, s_lastScore;
static bool s_newBest;
static uint32_t s_ledsOff;

int rnd(int lo, int hi) { return lo + (int)(esp_random() % (uint32_t)(hi - lo + 1)); }

void sfx(int hz, int ms) { M5.Speaker.tone(hz, ms); }

void flash(uint8_t r, uint8_t g, uint8_t b, int ms) {
    setBaseLeds(r, g, b);                        // capped at LED_MAX_LEVEL inside
    s_ledsOff = millis() + ms;
}

void makeSprite(M5Canvas& c, const char* const rows[], int w, int h, uint16_t (*color)(char), bool mirror) {
    c.setColorDepth(16);
    c.createSprite(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) c.drawPixel(mirror ? w - 1 - x : x, y, color(rows[y][x]));
}

void hudText(const char* left, const char* right, int y) {
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(TFT_WHITE);
    gfx.setTextDatum(top_left);
    if (left) gfx.drawString(left, 4, y);
    gfx.setTextDatum(top_right);
    if (right) gfx.drawString(right, W - 4, y);
}

static void text(const char* s, int x, int y, uint16_t color, const lgfx::IFont* font, textdatum_t datum = middle_center) {
    gfx.setFont(font);
    gfx.setTextColor(color);
    gfx.setTextDatum(datum);
    gfx.drawString(s, x, y);
}

// ---------------------------------------------------------------- screens
// The menu: VISIBLE rows at a time; it scrolls (smoothly) to keep the selection in sight.
static constexpr int MENU_Y = 34, ROW_H = 46, VISIBLE = 4, MENU_H = VISIBLE * ROW_H, BAR_X = 298;
static int s_top;                                // first row in sight
static float s_scroll;                           // drawn position, chasing s_top * ROW_H

static const char* itemName(int i) { return i < GAME_COUNT ? GAMES[i]->name : "Controls"; }
static uint16_t itemColor(int i) { return i < GAME_COUNT ? GAMES[i]->color : RGB(120, 120, 140); }

static void select(int i) {
    s_sel = (i + MENU_ITEMS) % MENU_ITEMS;
    if (s_sel < s_top) s_top = s_sel;
    if (s_sel >= s_top + VISIBLE) s_top = s_sel - VISIBLE + 1;
}

static void drawMenu() {
    gfx.fillSprite(RGB(16, 18, 32));
    text("STACK-CHAN ARCADE", W / 2, 17, RGB(255, 214, 0), &fonts::FreeSansBold12pt7b);
    if (!inputHasJoystick()) text("no joystick", W - 4, 6, RGB(255, 120, 80), &fonts::efontJA_12, top_right);

    s_scroll += (s_top * ROW_H - s_scroll) * 0.35f;
    if (fabsf(s_top * ROW_H - s_scroll) < 0.5f) s_scroll = s_top * ROW_H;
    gfx.setClipRect(0, MENU_Y, BAR_X - 2, MENU_H);
    for (int i = 0; i < MENU_ITEMS; i++) {
        const int y = MENU_Y + i * ROW_H - (int)s_scroll;
        if (y + ROW_H < MENU_Y || y > MENU_Y + MENU_H) continue;
        const bool sel = i == s_sel;
        const uint16_t color = itemColor(i), fg = sel ? TFT_BLACK : TFT_WHITE;
        gfx.fillRoundRect(8, y + 3, BAR_X - 16, ROW_H - 6, 10, sel ? color : RGB(34, 38, 60));
        if (sel) gfx.drawRoundRect(8, y + 3, BAR_X - 16, ROW_H - 6, 10, TFT_WHITE);
        // A badge with the initial, in the game's color.
        gfx.fillRoundRect(16, y + 9, 28, 28, 7, sel ? RGB(16, 18, 32) : color);
        const char initial[2] = { itemName(i)[0], 0 };
        text(initial, 30, y + 24, sel ? color : TFT_BLACK, &fonts::FreeSansBold12pt7b);
        text(itemName(i), 54, y + (i < GAME_COUNT && s_best[i] ? 17 : ROW_H / 2), fg, &fonts::FreeSansBold9pt7b, middle_left);
        if (i < GAME_COUNT && s_best[i]) {
            char buf[24];
            snprintf(buf, sizeof(buf), "best %d", s_best[i]);
            text(buf, 54, y + 33, sel ? RGB(40, 40, 40) : RGB(150, 150, 170), &fonts::Font2, middle_left);
        }
    }
    gfx.clearClipRect();

    // Scroll bar with arrows: they light up when there is more that way. Tapping them scrolls.
    const bool more_up = s_top > 0, more_down = s_top + VISIBLE < MENU_ITEMS;
    const int cx = BAR_X + 10;
    gfx.fillTriangle(cx - 7, MENU_Y + 14, cx + 7, MENU_Y + 14, cx, MENU_Y + 4, more_up ? TFT_WHITE : RGB(60, 60, 80));
    gfx.fillTriangle(cx - 7, MENU_Y + MENU_H - 14, cx + 7, MENU_Y + MENU_H - 14, cx, MENU_Y + MENU_H - 4,
                     more_down ? TFT_WHITE : RGB(60, 60, 80));
    const int track = MENU_H - 40, thumb = max(16, track * VISIBLE / MENU_ITEMS);
    const int ty = MENU_Y + 20 + (MENU_ITEMS > VISIBLE ? (int)((track - thumb) * s_scroll / ((MENU_ITEMS - VISIBLE) * ROW_H)) : 0);
    gfx.fillRoundRect(cx - 2, MENU_Y + 20, 4, track, 2, RGB(40, 44, 70));
    gfx.fillRoundRect(cx - 3, ty, 6, thumb, 3, RGB(180, 180, 200));

    const char* hint = s_sel < GAME_COUNT ? GAMES[s_sel]->hint : "Check the joystick and the buttons";
    text(hint, W / 2, H - 14, RGB(180, 180, 200), &fonts::efontJA_12);
}

static void drawControls(const Input& in) {
    gfx.fillSprite(RGB(16, 18, 32));
    text("Controls", W / 2, 16, TFT_WHITE, &fonts::efontJA_16);
    const int cx = 100, cy = 120, r = 70;
    gfx.drawCircle(cx, cy, r, RGB(80, 80, 110));
    gfx.drawFastHLine(cx - r, cy, 2 * r, RGB(50, 50, 70));
    gfx.drawFastVLine(cx, cy - r, 2 * r, RGB(50, 50, 70));
    gfx.fillCircle(cx + in.x * r, cy + in.y * r, 10, in.x || in.y ? RGB(255, 214, 0) : RGB(120, 120, 140));
    int xmv, ymv;
    inputRaw(&xmv, &ymv);
    char buf[48];
    snprintf(buf, sizeof(buf), "X %4d mV  %+.2f", xmv, in.x);
    text(buf, 190, 60, TFT_WHITE, &fonts::Font2, middle_left);
    snprintf(buf, sizeof(buf), "Y %4d mV  %+.2f", ymv, in.y);
    text(buf, 190, 80, TFT_WHITE, &fonts::Font2, middle_left);
    const char* names[] = { "A", "B", "SW" };
    const bool on[] = { in.a, in.b, in.sw };
    for (int i = 0; i < 3; i++) {
        gfx.fillCircle(210 + i * 40, 130, 15, on[i] ? RGB(76, 175, 80) : RGB(50, 50, 70));
        text(names[i], 210 + i * 40, 131, TFT_WHITE, &fonts::Font2);
    }
    text("Right: + X, down: + Y. If not, see SWAP/INVERT in input.cpp", W / 2, 206, RGB(180, 180, 200), &fonts::efontJA_12);
    text("Tap the screen to go back", W / 2, 226, RGB(120, 120, 140), &fonts::efontJA_12);
}

static void drawOverlay(const char* title, const char* line1, const char* line2) {
    gfx.fillRoundRect(40, 60, W - 80, 120, 12, RGB(16, 18, 32));
    gfx.drawRoundRect(40, 60, W - 80, 120, 12, GAMES[s_game]->color);
    text(title, W / 2, 88, TFT_WHITE, &fonts::FreeSansBold12pt7b);
    text(line1, W / 2, 124, RGB(255, 214, 0), &fonts::efontJA_16);
    text(line2, W / 2, 156, RGB(180, 180, 200), &fonts::efontJA_12);
}

// ---------------------------------------------------------------- flow
static void startGame(int i) {
    s_game = i;
    s_mode = Mode::play;
    GAMES[i]->start();
    sfx(1200, 50);
    Serial.printf("[arcade] playing %s\n", GAMES[i]->name);
}

static void gameOver() {
    s_lastScore = GAMES[s_game]->score();
    s_newBest = s_lastScore > s_best[s_game];
    if (s_newBest) {
        s_best[s_game] = s_lastScore;
        char key[8];
        snprintf(key, sizeof(key), "best%d", s_game);
        s_prefs.putInt(key, s_lastScore);
        flash(255, 214, 0, 1500);
    }
    sfx(220, 400);
    s_mode = Mode::over;
    char buf[32], best[32];
    snprintf(buf, sizeof(buf), s_newBest ? "NEW RECORD: %d" : "Score: %d", s_lastScore);
    snprintf(best, sizeof(best), "best %d   A: again   B: menu", s_best[s_game]);
    drawOverlay(GAMES[s_game]->endTitle ? GAMES[s_game]->endTitle() : "GAME OVER", buf, best);
    Serial.printf("[arcade] %s over: %d%s\n", GAMES[s_game]->name, s_lastScore, s_newBest ? " (record)" : "");
}

static void step(const Input& in, float dt) {
    switch (s_mode) {
    case Mode::menu: {
        if (in.upP) { select(s_sel - 1); sfx(900, 20); }
        if (in.downP) { select(s_sel + 1); sfx(900, 20); }
        int pick = in.aP || in.swP ? s_sel : -1;
        if (in.touchP && in.ty >= MENU_Y && in.ty < MENU_Y + MENU_H) {
            if (in.tx >= BAR_X - 2) {           // the scroll bar: the upper half scrolls up, the lower half down
                const bool up = in.ty < MENU_Y + MENU_H / 2;
                s_top = constrain(s_top + (up ? -1 : 1), 0, max(0, MENU_ITEMS - VISIBLE));
                s_sel = constrain(s_sel, s_top, s_top + VISIBLE - 1);
                sfx(900, 20);
            } else {
                const int i = (in.ty - MENU_Y + (int)s_scroll) / ROW_H;
                if (i < MENU_ITEMS) { s_sel = i; pick = i; }
            }
        }
        if (pick >= 0 && pick < GAME_COUNT) { startGame(pick); return; }
        if (pick == GAME_COUNT) { s_mode = Mode::controls; return; }
        drawMenu();
        break;
    }
    case Mode::controls:
        if (in.touchP) { s_mode = Mode::menu; drawMenu(); return; }
        drawControls(in);
        break;
    case Mode::play:
        if (in.swP || in.touchP) {
            s_mode = Mode::pause;
            drawOverlay("PAUSE", GAMES[s_game]->name, "A: go on   B: menu");
            return;
        }
        if (!GAMES[s_game]->frame(in, dt)) gameOver();
        break;
    case Mode::pause:
        if (in.aP || in.swP || in.touchP) { s_mode = Mode::play; return; }
        if (in.bP) { s_mode = Mode::menu; drawMenu(); }
        break;
    case Mode::over:
        if (in.aP) startGame(s_game);
        else if (in.bP || in.touchP) { s_mode = Mode::menu; drawMenu(); }
        break;
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    M5.Display.setBrightness(BRIGHTNESS);
    M5.Speaker.setVolume(VOLUME);
    if (!boardInit(false)) Serial.println("Base K151 not answering (PY32)");   // servos off: the head doesn't play
    setBaseLeds(0, 0, 0);
    inputBegin();

    s_prefs.begin("arcade", false);
    for (int i = 0; i < GAME_COUNT; i++) {
        char key[8];
        snprintf(key, sizeof(key), "best%d", i);
        s_best[i] = s_prefs.getInt(key, 0);
    }
    gfx.setPsram(true);
    gfx.setColorDepth(16);
    gfx.createSprite(W, H);
    drawMenu();
    gfx.pushSprite(0, 0);
}

void loop() {
    static uint32_t last = millis(), fpsFrames = 0, fpsSince = millis(), busy = 0;
    const uint32_t t0 = millis();
    M5.update();
    const Input in = inputRead();
    const float dt = min(0.1f, (t0 - last) / 1000.0f);   // after a pause or a slow frame, not a big jump
    last = t0;
    step(in, dt);
    gfx.pushSprite(0, 0);
    if (s_ledsOff && (int32_t)(millis() - s_ledsOff) >= 0) { s_ledsOff = 0; setBaseLeds(0, 0, 0); }

    // How fast it really goes: once every 10 s on the serial monitor.
    busy += millis() - t0;
    if (++fpsFrames, millis() - fpsSince >= 10000) {
        Serial.printf("[arcade] %.1f fps, %lu ms of work per frame\n", fpsFrames * 1000.0f / (millis() - fpsSince),
                      (unsigned long)(busy / fpsFrames));
        fpsFrames = busy = 0;
        fpsSince = millis();
    }
    const uint32_t spent = millis() - t0;
    if (spent < FRAME_MS) delay(FRAME_MS - spent);
}
