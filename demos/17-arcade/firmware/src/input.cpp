#include "input.h"
#include <M5Unified.h>

static constexpr int PIN_X = 9, PIN_Y = 8, PIN_A = 17, PIN_B = 18, PIN_SW = 2;
// If the joystick is held turned, change these: right must give + x, down + y.
static constexpr bool SWAP_XY = false, INVERT_X = false, INVERT_Y = false;
static constexpr int FULL_MV = 3100;               // 5 V through the divider: 3.3 V, the ADC reads up to ~3.1 V
static constexpr float DEAD = 0.18f;               // a joystick at rest is never exactly centered
static constexpr float ON = 0.5f, OFF = 0.35f;     // direction thresholds, with hysteresis
static constexpr uint32_t REPEAT_DELAY_MS = 220, REPEAT_MS = 70;

static int s_cx, s_cy, s_rawX, s_rawY;
static bool s_joystick;
static Input s_last;
static uint32_t s_since[4], s_next[4];

static int readMv(int pin) { return (analogReadMilliVolts(pin) + analogReadMilliVolts(pin)) / 2; }

void inputBegin() {
    analogSetPinAttenuation(PIN_X, ADC_11db);
    analogSetPinAttenuation(PIN_Y, ADC_11db);
    pinMode(PIN_A, INPUT_PULLUP);
    pinMode(PIN_B, INPUT_PULLUP);
    pinMode(PIN_SW, INPUT_PULLUP);
    long sx = 0, sy = 0;
    for (int i = 0; i < 16; i++) { sx += readMv(PIN_X); sy += readMv(PIN_Y); delay(2); }
    s_cx = sx / 16;
    s_cy = sy / 16;
    s_joystick = s_cx > 300 && s_cy > 300;         // unwired pins sit near 0 V
    Serial.printf("[arcade] joystick %s (center X %d mV, Y %d mV)\n", s_joystick ? "found" : "NOT found", s_cx, s_cy);
}

bool inputHasJoystick() { return s_joystick; }

void inputRaw(int* xMv, int* yMv) { *xMv = s_rawX; *yMv = s_rawY; }

// mV -> -1..1 around the center measured at start, with a dead zone.
static float axis(int mv, int center) {
    float v = mv >= center ? (float)(mv - center) / max(1, FULL_MV - center) : (float)(mv - center) / max(1, center);
    v = constrain(v, -1.0f, 1.0f);
    if (fabsf(v) < DEAD) return 0;
    return (v - copysignf(DEAD, v)) / (1 - DEAD);
}

// Pressed now, and repeated while held.
static bool repeat(int i, bool held, uint32_t now) {
    if (!held) { s_since[i] = 0; return false; }
    if (!s_since[i]) { s_since[i] = now; s_next[i] = now + REPEAT_DELAY_MS; return true; }
    if ((int32_t)(now - s_next[i]) >= 0) { s_next[i] = now + REPEAT_MS; return true; }
    return false;
}

Input inputRead() {
    Input in = {};
    const uint32_t now = millis();
    if (s_joystick) {
        s_rawX = readMv(PIN_X);
        s_rawY = readMv(PIN_Y);
        float x = axis(s_rawX, s_cx), y = axis(s_rawY, s_cy);
        if (SWAP_XY) std::swap(x, y);
        in.x = INVERT_X ? -x : x;
        in.y = INVERT_Y ? -y : y;
    }
    in.left = in.x < -(s_last.left ? OFF : ON);
    in.right = in.x > (s_last.right ? OFF : ON);
    in.up = in.y < -(s_last.up ? OFF : ON);
    in.down = in.y > (s_last.down ? OFF : ON);
    in.leftP = repeat(0, in.left, now);
    in.rightP = repeat(1, in.right, now);
    in.upP = repeat(2, in.up, now);
    in.downP = repeat(3, in.down, now);
    in.a = digitalRead(PIN_A) == LOW;
    in.b = digitalRead(PIN_B) == LOW;
    in.sw = digitalRead(PIN_SW) == LOW;
    in.aP = in.a && !s_last.a;
    in.bP = in.b && !s_last.b;
    in.swP = in.sw && !s_last.sw;
    const auto t = M5.Touch.getDetail();
    if (t.wasPressed()) { in.touchP = true; in.tx = t.x; in.ty = t.y; }
    s_last = in;
    return in;
}
