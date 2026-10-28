// Test "screen-colors" (OTA on demand): the whole screen in one color.
// Tap a swatch at the bottom to choose it, tap above to go to the next one, and
// "auto" goes round them by itself. Hold the screen 3 s: the OTA menu.
// The one to copy for your own tests: setup, loop, taps and redraw.
#include "ota.h"

struct Swatch { const char* name; uint16_t color; };
static const Swatch SWATCHES[] = {
    { "Red",     TFT_RED },
    { "Orange",  TFT_ORANGE },
    { "Yellow",  TFT_YELLOW },
    { "Green",   TFT_GREEN },
    { "Cyan",    TFT_CYAN },
    { "Blue",    TFT_BLUE },
    { "Magenta", TFT_MAGENTA },
};
static constexpr int NUM_SWATCHES = sizeof(SWATCHES) / sizeof(SWATCHES[0]);
static constexpr int CELL = 40, BAR_Y = 240 - CELL;     // 7 swatches + "auto" = 320 px
static constexpr uint32_t AUTO_MS = 1000;

static auto& lcd = M5.Display;
static int s_color;
static bool s_auto;

// Black text on light colors, white on dark ones.
static uint16_t textOn(uint16_t c) {
    const int r = (c >> 11) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
    return r * 3 + g * 6 + b > 1280 ? TFT_BLACK : TFT_WHITE;
}

static void drawColor() {
    const uint16_t bg = SWATCHES[s_color].color, fg = textOn(bg);
    lcd.fillRect(0, 0, lcd.width(), BAR_Y, bg);
    lcd.setTextColor(fg, bg);
    lcd.setTextDatum(middle_center);
    lcd.setFont(&fonts::FreeSansBold18pt7b);
    lcd.drawString(SWATCHES[s_color].name, 160, 80);
    lcd.setFont(&fonts::efontJA_16);
    char hex[24];
    snprintf(hex, sizeof(hex), "RGB565 0x%04X", bg);
    lcd.drawString(hex, 160, 125);
    lcd.drawString("hold 3 s: OTA menu", 160, 175);
}

static void drawBar() {
    for (int i = 0; i < NUM_SWATCHES; i++) {
        lcd.fillRect(i * CELL, BAR_Y, CELL, CELL, SWATCHES[i].color);
        if (i == s_color) lcd.drawRect(i * CELL + 2, BAR_Y + 2, CELL - 4, CELL - 4, textOn(SWATCHES[i].color));
    }
    const int x = NUM_SWATCHES * CELL;
    const uint16_t bg = s_auto ? TFT_WHITE : TFT_DARKGREY;
    lcd.fillRect(x, BAR_Y, CELL, CELL, bg);
    lcd.setFont(&fonts::efontJA_12);
    lcd.setTextColor(TFT_BLACK, bg);
    lcd.setTextDatum(middle_center);
    lcd.drawString("auto", x + CELL / 2, BAR_Y + CELL / 2);
}

static void choose(int i) {
    s_color = i;
    Serial.printf("[screen-colors] %s%s\n", SWATCHES[i].name, s_auto ? " (auto)" : "");
    drawColor();
    drawBar();
}

void testSetup() {
    Serial.println("[screen-colors] tap a color, hold 3 s for the OTA menu");
    choose(0);
}

void testLoop() {
    static uint32_t last = 0;
    const auto d = M5.Touch.getDetail();
    if (d.wasClicked()) {
        if (d.y < BAR_Y) choose((s_color + 1) % NUM_SWATCHES);
        else if (d.x / CELL < NUM_SWATCHES) choose(d.x / CELL);
        else {
            s_auto = !s_auto;
            Serial.printf("[screen-colors] auto %s\n", s_auto ? "on" : "off");
            drawBar();
        }
        last = millis();
    }
    if (s_auto && millis() - last >= AUTO_MS) {
        last = millis();
        choose((s_color + 1) % NUM_SWATCHES);
    }
    delay(10);
}

void testRedraw() {
    drawColor();
    drawBar();
}
