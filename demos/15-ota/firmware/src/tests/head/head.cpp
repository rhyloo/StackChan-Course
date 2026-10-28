// Test "head": the head looks slowly left and right, and nods. Tap to stop or go on.
// servoSetTarget() keeps the limits (stackchan_limits.h) and the speed limiter.
#include "ota.h"
#include "board.h"

static constexpr float PAN_AMPLITUDE  = 0.6f;   // rad (~35°) to each side
static constexpr float TILT_CENTER    = 0.25f;  // rad, looking a bit up: far from the chin
static constexpr float TILT_AMPLITUDE = 0.15f;
static constexpr float PERIOD_S       = 6;
static auto& lcd = M5.Display;
static bool s_moving = true;

static void draw() {
    lcd.fillRect(0, OTA_BAR_H, lcd.width(), lcd.height() - OTA_BAR_H, TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(middle_center);
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.drawString(s_moving ? "Looking around" : "Stopped", 160, 110);
    lcd.setFont(&fonts::efontJA_16);
    lcd.drawString("tap to stop / go on", 160, 200);
}

void testSetup() {
    servoInit();
    servoStartTask();
    Log.println("[head] looking around");
    draw();
}

void testLoop() {
    if (M5.Touch.getDetail().wasPressed()) {
        s_moving = !s_moving;
        if (!s_moving) servoSetTarget(0, 0);
        Log.printf("[head] %s\n", s_moving ? "moving" : "stopped");
        draw();
    }
    if (s_moving) {
        const float t = millis() / 1000.0f * 2 * PI / PERIOD_S;
        servoSetTarget(PAN_AMPLITUDE * sinf(t), TILT_CENTER + TILT_AMPLITUDE * sinf(2 * t));
    }
    delay(20);
}

void testStop() { servoSetTarget(0, 0); }
