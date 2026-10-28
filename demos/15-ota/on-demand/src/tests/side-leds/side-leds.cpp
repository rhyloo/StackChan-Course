// Test "side-leds" (OTA on demand): the base's 12 LEDs, one color each.
// They are two rows of 6: LEDs 0-5 and 6-11. Pattern "chase" lights them one by one
// and says which one is lit, to see where each row is on your robot.
// Tap the screen: next pattern. Hold it 3 s: the OTA menu.
//
// setBaseLeds() only does one color for all of them, so this test writes the LED
// registers of the base's coprocessor itself (docs/ROBOT.md), with the same brightness
// cap (LED_MAX_LEVEL): at full power the PMIC cut the robot's power.
#include "ota.h"
#include "board.h"

static constexpr uint8_t PY32_ADDR = 0x6F, REG_LED_CFG = 0x24, REG_LED_RAM = 0x30;
static constexpr int NUM_LEDS = 12, ROW = 6;
static constexpr uint32_t STEP_MS = 30;

struct Rgb { uint8_t r, g, b; };
static Rgb s_led[NUM_LEDS];

struct Pattern { const char* name; const char* what; };
static const Pattern PATTERNS[] = {
    { "sides",     "LEDs 0-5 red, LEDs 6-11 blue" },
    { "chase",     "one LED at a time: which one is it?" },
    { "alternate", "one row, then the other" },
    { "breathe",   "both rows breathe, out of step" },
};
static constexpr int NUM_PATTERNS = sizeof(PATTERNS) / sizeof(PATTERNS[0]);

static auto& lcd = M5.Display;
static int s_pattern;
static uint32_t s_patternSince;

// Like setBaseLeds(), with one color per LED: RGB565, low byte first, then latch.
static void showLeds() {
    uint8_t buf[NUM_LEDS * 2];
    for (int i = 0; i < NUM_LEDS; i++) {
        const uint8_t r = s_led[i].r * LED_MAX_LEVEL / 255, g = s_led[i].g * LED_MAX_LEVEL / 255,
                      b = s_led[i].b * LED_MAX_LEVEL / 255;
        const uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        buf[i * 2] = c & 0xFF;
        buf[i * 2 + 1] = c >> 8;
    }
    I2CGuard lock;
    M5.In_I2C.writeRegister(PY32_ADDR, REG_LED_RAM, buf, sizeof(buf), 100000);
    uint8_t cfg = 0;
    if (M5.In_I2C.readRegister(PY32_ADDR, REG_LED_CFG, &cfg, 1, 100000))
        M5.In_I2C.writeRegister8(PY32_ADDR, REG_LED_CFG, cfg | 0x40, 100000);   // latch
}

static void setRow(int row, Rgb c) {
    for (int i = row * ROW; i < (row + 1) * ROW; i++) s_led[i] = c;
}

// The pattern at time t (ms since it started) into s_led.
static void compute(uint32_t t) {
    static const Rgb OFF = { 0, 0, 0 };
    switch (s_pattern) {
    case 0:
        setRow(0, { 255, 0, 0 });
        setRow(1, { 0, 0, 255 });
        break;
    case 1:
        for (auto& l : s_led) l = OFF;
        s_led[(t / 400) % NUM_LEDS] = { 0, 255, 255 };
        break;
    case 2: {
        const bool first = (t / 400) % 2 == 0;
        setRow(0, first ? Rgb{ 255, 0, 0 } : OFF);
        setRow(1, first ? OFF : Rgb{ 0, 0, 255 });
        break;
    }
    case 3: {
        const float a = (1 - cosf(t * 2 * PI / 3000)) / 2;     // 0..1 every 3 s
        setRow(0, { 0, (uint8_t)(255 * a), 0 });
        setRow(1, { (uint8_t)(255 * (1 - a)), 0, (uint8_t)(255 * (1 - a)) });
        break;
    }
    }
}

// The LEDs on the screen too, at full color, in two columns.
static void drawLeds() {
    for (int i = 0; i < NUM_LEDS; i++) {
        const int x = i < ROW ? 100 : 220, y = 66 + (i % ROW) * 26;
        lcd.fillCircle(x, y, 10, lcd.color565(s_led[i].r, s_led[i].g, s_led[i].b));
        lcd.drawCircle(x, y, 11, TFT_DARKGREY);
    }
    if (s_pattern == 1) {
        char txt[24];
        snprintf(txt, sizeof(txt), "  LED %d  ", (int)((millis() - s_patternSince) / 400 % NUM_LEDS));
        lcd.setTextColor(TFT_CYAN, TFT_BLACK);
        lcd.setTextDatum(middle_center);
        lcd.drawString(txt, 160, 120);
    }
}

static void drawScreen() {
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextDatum(top_center);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString(String(s_pattern + 1) + "/" + NUM_PATTERNS + "  " + PATTERNS[s_pattern].name, 160, 8);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd.drawString(PATTERNS[s_pattern].what, 160, 28);
    lcd.drawString("0-5", 100, 44 - 4);
    lcd.drawString("6-11", 220, 44 - 4);
    lcd.setTextDatum(bottom_center);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("tap: next  -  hold 3 s: OTA menu", 160, 238);
    drawLeds();
}

static void startPattern(int p) {
    s_pattern = p;
    s_patternSince = millis();
    Serial.printf("[side-leds] pattern %s\n", PATTERNS[p].name);
    compute(0);
    showLeds();
    drawScreen();
}

void testSetup() {
    Serial.println("[side-leds] tap for the next pattern, hold 3 s for the OTA menu");
    startPattern(0);
}

void testLoop() {
    static uint32_t last = 0;
    if (M5.Touch.getDetail().wasClicked()) startPattern((s_pattern + 1) % NUM_PATTERNS);
    if (millis() - last < STEP_MS) return;
    last = millis();
    compute(millis() - s_patternSince);
    showLeds();
    drawLeds();
}

void testStop() {
    for (auto& l : s_led) l = { 0, 0, 0 };
    showLeds();
}

void testRedraw() { drawScreen(); }
