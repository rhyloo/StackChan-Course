// Test "crash": a firmware that fails on purpose, to see the rollback.
// 5 s after starting it writes through a null pointer: the chip panics and restarts.
// It hasn't been confirmed yet (the base waits 15 s), so the bootloader goes back to
// the firmware you had before, and `run.sh send crash` tells you so.
#include "ota.h"

static constexpr uint32_t CRASH_AFTER_MS = 5000;
static auto& lcd = M5.Display;
static uint32_t s_start;

void testSetup() {
    s_start = millis();
    lcd.fillRect(0, OTA_BAR_H, lcd.width(), lcd.height() - OTA_BAR_H, TFT_MAROON);
    lcd.setTextColor(TFT_WHITE, TFT_MAROON);
    lcd.setTextDatum(middle_center);
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.drawString("This test crashes in", 160, 90);
    Log.println("[crash] crashing in 5 s");
}

void testLoop() {
    static int lastShown = -1;
    const int left = ((int)CRASH_AFTER_MS - (int)(millis() - s_start) + 999) / 1000;
    if (left != lastShown) {
        lastShown = left;
        lcd.setFont(&fonts::FreeSansBold24pt7b);
        lcd.drawString(String(" ") + left + " ", 160, 150);
        Log.printf("[crash] %d\n", left);
    }
    if (left <= 0) {
        Log.println("[crash] now");
        delay(200);                   // time for the line to leave over the Wi-Fi
        volatile int* nothing = nullptr;
        *nothing = 42;                // StoreProhibited: panic and restart
    }
    delay(20);
}
