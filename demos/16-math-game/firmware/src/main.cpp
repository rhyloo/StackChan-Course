// Demo 16 - Math game: Stack-chan asks sums, subtractions, times tables and divisions,
// and the child answers on the touch screen.
//
// 1. Home: choose +, -, x, ÷ or Mix. A round is ROUND questions.
// 2. Each question has four answers to tap. The wrong ones are the mistakes children
//    really make (7 x 8: 54, 56, 63, 15). Easy questions come with things to count.
// 3. Right: a happy face, a nod, green LEDs and a tune. Wrong: the right answer is
//    shown, Stack-chan tilts its head, and the question comes back a bit later.
// 4. Every answer is saved in the flash, per skill ("the 7 times table"). The game asks
//    more of what fails, and opens the next level when one is mastered (game.cpp).
// 5. Progress: the skills of each operation on the screen, and on a web page served by
//    the robot itself (web.cpp) if it reaches the Wi-Fi of config/wifi_secrets.h.
//
//   game.cpp  questions, skills, statistics     art.cpp  face, signs, pictures
//   web.cpp   the statistics page               main.cpp the screens and the robot
#include <M5Unified.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "board.h"
#include "game.h"
#include "art.h"
#include "web.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static constexpr int ROUND = 10;
static constexpr uint32_t RIGHT_MS = 1300, WRONG_MS = 2800, LEVEL_UP_MS = 3000;   // feedback, then the next question
static constexpr uint8_t BRIGHTNESS = 140, VOLUME = 110;

// The words on the screen, together, to translate them in one place.
static const char* const TXT_TITLE = "Math with Stack-chan";
static const char* const TXT_MIX = "Mix";
static const char* const TXT_PROGRESS = "Progress";
static const char* const TXT_BACK = "Back";
static const char* const TXT_HOME = "Home";
static const char* const TXT_AGAIN = "Play again";
static const char* const TXT_QUESTION = "Question %d of %d";
static const char* const TXT_RIGHT[] = { "Great!", "Well done!", "Super!", "Yes!", "Brilliant!" };
static const char* const TXT_WRONG = "Almost! Look:";
static const char* const TXT_LEVEL_UP = "New level:";
static const char* const TXT_SCORE = "%d of %d right";
static const char* const TXT_PRACTISE = "Let's practise:";
static const char* const TXT_ALL_GOOD = "You're doing great!";
static const char* const TXT_LOCKED = "locked";

static constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }
static constexpr uint16_t C_BG = RGB(255, 248, 231), C_TEXT = RGB(40, 40, 60), C_DIM = RGB(150, 144, 125);
static constexpr uint16_t C_CARD = TFT_WHITE, C_LINE = RGB(225, 215, 190);
static constexpr uint16_t C_RIGHT = RGB(67, 160, 71), C_WRONG = RGB(239, 83, 80), C_STAR = RGB(255, 193, 7);
static constexpr uint16_t C_OP[OPS] = { RGB(76, 175, 80), RGB(33, 150, 243), RGB(255, 152, 0), RGB(156, 39, 176) };
static constexpr uint16_t C_MIX = RGB(0, 150, 136);

static auto& lcd = M5.Display;
static M5Canvas s_c(&M5.Display);              // the whole screen: drawn, then pushed at once

enum class Screen { home, question, feedback, end, progress };
static Screen s_screen = Screen::home;
static Op s_op = Op::add;
static bool s_mix;
static int s_qNum, s_score, s_tapped = -1, s_levelUp = -1;
static bool s_results[ROUND];
static Question s_q;
static uint32_t s_qStart, s_feedbackAt, s_feedbackUntil;
static Op s_progressOp = Op::add;
static char s_name[20];
static bool s_webStarted;

struct Rect { int x, y, w, h; bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };
static const Rect HOME_OPS[5] = { { 6, 132, 58, 58 }, { 68, 132, 58, 58 }, { 130, 132, 58, 58 }, { 192, 132, 58, 58 }, { 254, 132, 60, 58 } };
static const Rect HOME_PROGRESS = { 6, 202, 120, 32 };
static const Rect ANSWERS[4] = { { 4, 168, 74, 66 }, { 83, 168, 74, 66 }, { 162, 168, 74, 66 }, { 241, 168, 74, 66 } };
static const Rect END_AGAIN = { 20, 196, 136, 38 }, END_HOME = { 164, 196, 136, 38 };
static const Rect PROGRESS_TABS[4] = { { 4, 4, 52, 32 }, { 60, 4, 52, 32 }, { 116, 4, 52, 32 }, { 172, 4, 52, 32 } };
static const Rect PROGRESS_BACK = { 232, 4, 84, 32 };

// ---------------------------------------------------------------- sound, LEDs, head
// Small sequences played from loop() without blocking it.
struct Note { uint16_t hz, ms; };
static const Note TUNE_RIGHT[] = { { 880, 90 }, { 1319, 150 } };
static const Note TUNE_WRONG[] = { { 392, 160 }, { 330, 240 } };
static const Note TUNE_LEVEL[] = { { 523, 110 }, { 659, 110 }, { 784, 110 }, { 1047, 300 } };
static const Note TUNE_TAP[] = { { 1500, 25 } };
static const Note* s_tune;
static int s_tuneLen, s_tuneIdx;
static uint32_t s_tuneNext;

struct Pose { int8_t pan, tilt; uint16_t ms; };   // degrees; tilt + = looking up
static const Pose MOVE_NOD[] = { { 0, 14, 260 }, { 0, 2, 260 }, { 0, 14, 260 }, { 0, 0, 300 } };
static const Pose MOVE_TILT[] = { { 12, 10, 700 }, { 0, 0, 500 } };   // "hmm?", not "no"
static const Pose MOVE_CHEER[] = { { -20, 16, 300 }, { 20, 16, 300 }, { -20, 16, 300 }, { 20, 16, 300 }, { 0, 0, 400 } };
static const Pose* s_move;
static int s_moveLen, s_moveIdx;
static uint32_t s_moveNext, s_ledsOff;

template <size_t N> static void play(const Note (&t)[N]) { s_tune = t; s_tuneLen = N; s_tuneIdx = 0; s_tuneNext = 0; }
template <size_t N> static void move(const Pose (&m)[N]) { s_move = m; s_moveLen = N; s_moveIdx = 0; s_moveNext = 0; }

static void leds(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
    setBaseLeds(r, g, b);                        // capped at LED_MAX_LEVEL inside
    s_ledsOff = millis() + ms;
}

static void runEffects(uint32_t now) {
    if (s_tune && (int32_t)(now - s_tuneNext) >= 0) {
        if (s_tuneIdx < s_tuneLen) {
            M5.Speaker.tone(s_tune[s_tuneIdx].hz, s_tune[s_tuneIdx].ms);
            s_tuneNext = now + s_tune[s_tuneIdx].ms + 20;
            s_tuneIdx++;
        } else s_tune = nullptr;
    }
    if (s_move && (int32_t)(now - s_moveNext) >= 0) {
        if (s_moveIdx < s_moveLen) {
            servoSetTarget(s_move[s_moveIdx].pan * DEG_TO_RAD, s_move[s_moveIdx].tilt * DEG_TO_RAD);
            s_moveNext = now + s_move[s_moveIdx].ms;
            s_moveIdx++;
        } else s_move = nullptr;
    }
    if (s_ledsOff && (int32_t)(now - s_ledsOff) >= 0) {
        s_ledsOff = 0;
        setBaseLeds(0, 0, 0);
    }
}

// ---------------------------------------------------------------- drawing helpers
static void button(const Rect& r, uint16_t bg, uint16_t border, uint16_t fg, const char* text, const lgfx::IFont* font) {
    s_c.fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
    s_c.drawRoundRect(r.x, r.y, r.w, r.h, 10, border);
    s_c.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 9, border);
    if (!text) return;
    s_c.setFont(font);
    s_c.setTextColor(fg);
    s_c.setTextDatum(middle_center);
    s_c.drawString(text, r.x + r.w / 2, r.y + r.h / 2 + 1);
}

static void text(const char* s, int x, int y, uint16_t color, const lgfx::IFont* font, textdatum_t datum = middle_center) {
    s_c.setFont(font);
    s_c.setTextColor(color);
    s_c.setTextDatum(datum);
    s_c.drawString(s, x, y);
}

static uint16_t opColor() { return s_mix ? C_MIX : C_OP[(int)s_op]; }

// ---------------------------------------------------------------- screens
static void drawHome() {
    s_c.fillSprite(C_BG);
    text(TXT_TITLE, 160, 18, C_TEXT, &fonts::FreeSansBold12pt7b);
    s_c.fillRoundRect(100, 34, 120, 88, 16, RGB(40, 40, 60));
    artFace(s_c, 160, 80, 110, Mood::happy, TFT_WHITE);
    for (int i = 0; i < OPS; i++) {
        button(HOME_OPS[i], C_OP[i], C_OP[i], TFT_WHITE, nullptr, nullptr);
        artOp(s_c, (Op)i, HOME_OPS[i].x + HOME_OPS[i].w / 2, HOME_OPS[i].y + HOME_OPS[i].h / 2, 30, TFT_WHITE);
    }
    button(HOME_OPS[4], C_MIX, C_MIX, TFT_WHITE, TXT_MIX, &fonts::FreeSansBold12pt7b);
    button(HOME_PROGRESS, C_CARD, C_LINE, C_TEXT, TXT_PROGRESS, &fonts::efontJA_16);
    char net[48];
    if (WiFi.status() == WL_CONNECTED) snprintf(net, sizeof(net), "http://%s", WiFi.localIP().toString().c_str());
    else snprintf(net, sizeof(net), "Wi-Fi: %s...", WIFI_SSID);
    text(net, 314, 218, C_DIM, &fonts::efontJA_12, middle_right);
    s_c.pushSprite(0, 0);
}

// The top bar of a round: which question, and a star for each right answer so far.
static void drawRoundBar() {
    s_c.fillRect(0, 0, 320, 24, opColor());
    char buf[32];
    snprintf(buf, sizeof(buf), TXT_QUESTION, s_qNum + 1, ROUND);
    text(buf, 8, 12, TFT_WHITE, &fonts::efontJA_16, middle_left);
    for (int i = 0; i < ROUND; i++) {
        const int cx = 314 - (ROUND - 1 - i) * 15 - 6;
        if (i < s_qNum) artStar(s_c, cx, 12, 6, s_results[i] ? C_STAR : RGB(255, 255, 255));
        else s_c.drawCircle(cx, 12, 3, TFT_WHITE);
    }
}

static void drawQuestion() {
    s_c.fillSprite(C_BG);
    drawRoundBar();
    const bool fb = s_screen == Screen::feedback;
    const bool right = fb && s_q.options[s_tapped] == s_q.answer;
    artEquation(s_c, s_q, 160, 52, fb ? s_q.answer : -1, C_TEXT, C_RIGHT);

    // The middle: things to count, or Stack-chan; during the feedback, its reaction.
    const int my = 80, mh = 82;
    if (!fb) {
        if (!artPicture(s_c, s_q, 8, my, 304, mh)) {
            s_c.fillRoundRect(110, my + 4, 100, mh - 8, 14, RGB(40, 40, 60));
            artFace(s_c, 160, my + mh / 2, 90, Mood::thinking, TFT_WHITE);
        }
    } else {
        s_c.fillRoundRect(16, my + 4, 92, mh - 8, 14, RGB(40, 40, 60));
        artFace(s_c, 62, my + mh / 2, 80, right ? Mood::happy : Mood::oops, TFT_WHITE);
        if (s_levelUp >= 0) {
            text(TXT_LEVEL_UP, 214, my + 26, C_TEXT, &fonts::efontJA_16);
            text(SKILLS[s_levelUp].name, 214, my + 50, opColor(), &fonts::efontJA_16);
            for (int i = 0; i < 3; i++) artStar(s_c, 170 + i * 44, my + 72, 7, C_STAR);
        } else if (right) {
            text(TXT_RIGHT[(s_qNum + s_q.a) % 5], 214, my + mh / 2, C_RIGHT, &fonts::FreeSansBold18pt7b);
        } else {
            text(TXT_WRONG, 214, my + 26, C_TEXT, &fonts::efontJA_16);
            char buf[16];
            snprintf(buf, sizeof(buf), "= %d", s_q.answer);
            text(buf, 214, my + 56, C_RIGHT, &fonts::FreeSansBold18pt7b);
        }
    }

    for (int i = 0; i < 4; i++) {
        uint16_t bg = C_CARD, border = opColor(), fg = C_TEXT;
        if (fb && s_q.options[i] == s_q.answer) { bg = C_RIGHT; border = C_RIGHT; fg = TFT_WHITE; }
        else if (fb && i == s_tapped) { bg = C_WRONG; border = C_WRONG; fg = TFT_WHITE; }
        else if (fb) { fg = C_DIM; border = C_LINE; }
        const String n(s_q.options[i]);
        button(ANSWERS[i], bg, border, fg, n.c_str(), &fonts::FreeSansBold18pt7b);
    }
    s_c.pushSprite(0, 0);
}

static void drawEnd() {
    s_c.fillSprite(C_BG);
    const bool good = s_score >= ROUND * 7 / 10;
    s_c.fillRoundRect(110, 8, 100, 72, 14, RGB(40, 40, 60));
    artFace(s_c, 160, 44, 90, good ? Mood::happy : Mood::neutral, TFT_WHITE);
    char buf[32];
    snprintf(buf, sizeof(buf), TXT_SCORE, s_score, ROUND);
    text(buf, 160, 100, C_TEXT, &fonts::FreeSansBold18pt7b);
    for (int i = 0; i < ROUND; i++) {
        const int cx = 160 + (i - (ROUND - 1) / 2.0f) * 28;
        if (s_results[i]) artStar(s_c, cx, 134, 11, C_STAR);
        else s_c.drawCircle(cx, 134, 6, C_DIM);
    }
    const int weak = gameWeakest(s_mix, s_op);
    if (weak >= 0) {
        text(TXT_PRACTISE, 160, 162, C_DIM, &fonts::efontJA_16);
        text(SKILLS[weak].name, 160, 180, C_OP[(int)SKILLS[weak].op], &fonts::efontJA_16);
    } else {
        text(TXT_ALL_GOOD, 160, 172, C_RIGHT, &fonts::efontJA_16);
    }
    button(END_AGAIN, opColor(), opColor(), TFT_WHITE, TXT_AGAIN, &fonts::efontJA_16);
    button(END_HOME, C_CARD, C_LINE, C_TEXT, TXT_HOME, &fonts::efontJA_16);
    s_c.pushSprite(0, 0);
}

static void drawProgress() {
    s_c.fillSprite(C_BG);
    for (int i = 0; i < OPS; i++) {
        const bool sel = (Op)i == s_progressOp;
        button(PROGRESS_TABS[i], sel ? C_OP[i] : C_CARD, C_OP[i], 0, nullptr, nullptr);
        artOp(s_c, (Op)i, PROGRESS_TABS[i].x + PROGRESS_TABS[i].w / 2, PROGRESS_TABS[i].y + PROGRESS_TABS[i].h / 2, 18,
              sel ? TFT_WHITE : C_OP[i]);
    }
    button(PROGRESS_BACK, C_CARD, C_LINE, C_TEXT, TXT_BACK, &fonts::efontJA_16);

    int rows = 0;
    for (int i = 0; i < SKILL_COUNT; i++) rows += SKILLS[i].op == s_progressOp;
    const int rowH = min(26, 196 / max(rows, 1));
    int y = 42;
    for (int i = 0; i < SKILL_COUNT; i++) {
        if (SKILLS[i].op != s_progressOp) continue;
        const SkillStats& s = skillStats(i);
        const bool open = skillUnlocked(i);
        const int cy = y + rowH / 2;
        text(SKILLS[i].name, 8, cy, open ? C_TEXT : C_DIM, &fonts::efontJA_16, middle_left);
        if (s.mastered) artStar(s_c, 196, cy, 7, C_STAR);
        if (!open) {
            text(TXT_LOCKED, 312, cy, C_DIM, &fonts::efontJA_12, middle_right);
        } else {
            s_c.fillRoundRect(210, cy - 5, 70, 10, 5, C_LINE);
            if (s.recentN) {
                const int pct = recentRight(i) * 100 / s.recentN;
                const uint16_t col = pct >= 80 ? C_RIGHT : pct >= 50 ? C_OP[2] : C_WRONG;
                s_c.fillRoundRect(210, cy - 5, max(10, 70 * pct / 100), 10, 5, col);
                char buf[8];
                snprintf(buf, sizeof(buf), "%d/%d", recentRight(i), s.recentN);
                text(buf, 314, cy, C_TEXT, &fonts::efontJA_12, middle_right);
            }
        }
        y += rowH;
    }
    s_c.pushSprite(0, 0);
}

// ---------------------------------------------------------------- game flow
static void nextQuestion() {
    s_q = gameNext(s_op, s_mix);
    s_screen = Screen::question;
    s_tapped = s_levelUp = -1;
    s_qStart = millis();
    drawQuestion();
}

static void startRound(Op op, bool mix) {
    s_op = op;
    s_mix = mix;
    s_qNum = s_score = 0;
    Serial.printf("[math] round: %s\n", mix ? "mix" : (const char*[]){ "add", "subtract", "multiply", "divide" }[(int)op]);
    nextQuestion();
}

static void answer(int i) {
    const uint32_t ms = millis() - s_qStart;
    s_tapped = i;
    const bool right = s_q.options[i] == s_q.answer;
    const bool mastered = gameAnswer(s_q, s_q.options[i], ms);
    s_results[s_qNum] = right;
    s_score += right;
    s_levelUp = -1;
    if (mastered) {   // the next skill of this operation opens
        for (int k = s_q.skill + 1; k < SKILL_COUNT && SKILLS[k].op == SKILLS[s_q.skill].op; k++) { s_levelUp = k; break; }
    }
    Serial.printf("[math] %s: %d %c %d = %d, answered %d in %.1f s%s\n", SKILLS[s_q.skill].name, s_q.a,
                  "+-x/"[(int)SKILLS[s_q.skill].op], s_q.b, s_q.answer, s_q.options[i], ms / 1000.0f,
                  right ? "" : "  (wrong)");
    if (s_levelUp >= 0) {
        play(TUNE_LEVEL); move(MOVE_CHEER); leds(255, 200, 0, 2500);
        Serial.printf("[math] mastered '%s': '%s' opens\n", SKILLS[s_q.skill].name, SKILLS[s_levelUp].name);
    } else if (right) {
        play(TUNE_RIGHT); move(MOVE_NOD); leds(0, 255, 0, 900);
    } else {
        play(TUNE_WRONG); move(MOVE_TILT); leds(255, 120, 0, 900);
    }
    s_screen = Screen::feedback;
    s_feedbackAt = millis();
    s_feedbackUntil = millis() + (s_levelUp >= 0 ? LEVEL_UP_MS : right ? RIGHT_MS : WRONG_MS);
    drawQuestion();
}

static void afterFeedback() {
    if (++s_qNum < ROUND) { nextQuestion(); return; }
    s_screen = Screen::end;
    if (s_score >= ROUND * 7 / 10) { play(TUNE_LEVEL); move(MOVE_CHEER); }
    Serial.printf("[math] round over: %d of %d\n", s_score, ROUND);
    drawEnd();
}

static void onTap(int x, int y) {
    switch (s_screen) {
    case Screen::home:
        for (int i = 0; i < 5; i++) {
            if (!HOME_OPS[i].hit(x, y)) continue;
            play(TUNE_TAP);
            startRound(i < OPS ? (Op)i : Op::add, i == OPS);
            return;
        }
        if (HOME_PROGRESS.hit(x, y)) { play(TUNE_TAP); s_screen = Screen::progress; drawProgress(); }
        break;
    case Screen::question:
        for (int i = 0; i < 4; i++) if (ANSWERS[i].hit(x, y)) { answer(i); return; }
        break;
    case Screen::feedback:   // a tap skips the wait
        if (millis() - s_feedbackAt > 400) afterFeedback();
        break;
    case Screen::end:
        if (END_AGAIN.hit(x, y)) { play(TUNE_TAP); startRound(s_op, s_mix); }
        else if (END_HOME.hit(x, y)) { play(TUNE_TAP); s_screen = Screen::home; drawHome(); }
        break;
    case Screen::progress:
        for (int i = 0; i < OPS; i++) if (PROGRESS_TABS[i].hit(x, y)) { s_progressOp = (Op)i; drawProgress(); return; }
        if (PROGRESS_BACK.hit(x, y)) { play(TUNE_TAP); s_screen = Screen::home; drawHome(); }
        break;
    }
}

// ---------------------------------------------------------------- setup / loop
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    lcd.setBrightness(BRIGHTNESS);
    M5.Speaker.setVolume(VOLUME);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_name, sizeof(s_name), "stackchan-%02X%02X", mac[4], mac[5]);

    if (!boardInit()) Serial.println("Base K151 not answering (PY32)");
    setBaseLeds(0, 0, 0);
    servoInit();
    servoStartTask();
    gameBegin();

    s_c.setPsram(true);
    s_c.setColorDepth(16);
    s_c.createSprite(lcd.width(), lcd.height());

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(s_name);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("\n[math] %s ready. Statistics on http://%s.local once on the Wi-Fi '%s'\n", s_name, s_name, WIFI_SSID);
    drawHome();
}

void loop() {
    M5.update();
    const uint32_t now = millis();

    if (!s_webStarted && WiFi.status() == WL_CONNECTED) {
        s_webStarted = true;
        MDNS.begin(s_name);
        MDNS.addService("http", "tcp", 80);
        webStart();
        Serial.printf("[math] statistics: http://%s.local  (http://%s)\n", s_name, WiFi.localIP().toString().c_str());
        if (s_screen == Screen::home) drawHome();
    }
    if (s_webStarted) webPoll();

    const auto d = M5.Touch.getDetail();
    if (d.wasPressed()) onTap(d.x, d.y);
    if (s_screen == Screen::feedback && (int32_t)(now - s_feedbackUntil) >= 0) afterFeedback();
    runEffects(now);

    if (Serial.available()) {   // "reset" on the serial monitor forgets the statistics
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line == "reset") { gameReset(); Serial.println("[math] statistics reset"); }
    }
    delay(5);
}
