// Test "hello": the first test, and the one to copy for your own.
// Change MESSAGE or COLORS, run `demos/15-ota/run.sh send hello` and see it change
// without a cable. Tap the screen: the color changes and a Log line goes to the laptop.
#include "ota.h"

static const char* MESSAGE = "Hello over the air!";
static const uint16_t COLORS[] = { TFT_BLUE, TFT_DARKGREEN, TFT_PURPLE, TFT_MAROON, TFT_DARKCYAN };

static auto& lcd = M5.Display;
static int s_color, s_taps;

static void draw() {
    const uint16_t bg = COLORS[s_color];
    lcd.fillRect(0, OTA_BAR_H, lcd.width(), lcd.height() - OTA_BAR_H, bg);
    lcd.setTextColor(TFT_WHITE, bg);
    lcd.setTextDatum(middle_center);
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.drawString(MESSAGE, 160, 110);
    lcd.setFont(&fonts::efontJA_16);
    lcd.drawString(String("taps: ") + s_taps, 160, 160);
    lcd.drawString("tap the screen", 160, 210);
}

void testSetup() {
    Log.printf("[hello] %s\n", MESSAGE);
    draw();
}

void testLoop() {
    if (M5.Touch.getDetail().wasPressed()) {
        s_taps++;
        s_color = (s_color + 1) % (sizeof(COLORS) / sizeof(COLORS[0]));
        Log.printf("[hello] tap %d\n", s_taps);
        draw();
    }
    delay(10);
}
