// Test "leds": the base's 12 LEDs go round the color wheel. Tap to pause.
// setBaseLeds() caps the brightness (LED_MAX_LEVEL): at full power the PMIC cut the power.
#include "ota.h"
#include "board.h"

static constexpr uint32_t STEP_MS = 40;
static auto& lcd = M5.Display;
static float s_hue;
static bool s_paused;

// Hue 0..360 -> RGB at full saturation.
static void hueToRgb(float h, uint8_t* r, uint8_t* g, uint8_t* b) {
    const float x = 1 - fabsf(fmodf(h / 60, 2) - 1);
    float rf = 0, gf = 0, bf = 0;
    if (h < 60) { rf = 1; gf = x; } else if (h < 120) { rf = x; gf = 1; } else if (h < 180) { gf = 1; bf = x; }
    else if (h < 240) { gf = x; bf = 1; } else if (h < 300) { rf = x; bf = 1; } else { rf = 1; bf = x; }
    *r = rf * 255; *g = gf * 255; *b = bf * 255;
}

void testSetup() {
    lcd.fillRect(0, OTA_BAR_H, lcd.width(), lcd.height() - OTA_BAR_H, TFT_BLACK);
    Log.println("[leds] color wheel on the base");
}

void testLoop() {
    static uint32_t last = 0;
    if (M5.Touch.getDetail().wasPressed()) {
        s_paused = !s_paused;
        Log.printf("[leds] %s at hue %d\n", s_paused ? "paused" : "running", (int)s_hue);
    }
    if (s_paused || millis() - last < STEP_MS) return;
    last = millis();
    s_hue = fmodf(s_hue + 3, 360);
    uint8_t r, g, b;
    hueToRgb(s_hue, &r, &g, &b);
    setBaseLeds(r, g, b);
    lcd.fillCircle(160, 130, 70, lcd.color565(r, g, b));
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(middle_center);
    lcd.setFont(&fonts::efontJA_16);
    lcd.drawString(String("  hue ") + (int)s_hue + "  ", 160, 220);
}

void testStop() { setBaseLeds(0, 0, 0); }
