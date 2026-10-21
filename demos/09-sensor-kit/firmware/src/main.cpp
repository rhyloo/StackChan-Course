// Demo 09 - The modules of the 37-sensor kit (KY-0xx) on PORT.B.
//
// PORT.B: black GND, red 5 V, white G9, yellow G8. The modules run on 5 V,
// but the ESP32 pins only take 3.3 V: every signal that GOES INTO the robot
// passes through a divider (10 kOhm in series and 20 kOhm to GND), which turns 5 V into 3.3 V.
// See the README: the single-wire modules (DHT11, DS18B20) are wired differently.
//
// On the screen you pick the module type:
//   Digital      0/1 on G9: tilt, vibration, Hall, reed, touch, flame, obstacle...
//   Analog       voltage on G9 with a graph: photoresistor, KY-028, linear Hall, A0 microphone...
//   DHT11        temperature and humidity (one wire, its own protocol)
//   DS18B20      temperature of the KY-001 (1-Wire bus)
//   Encoder      KY-040: CLK on G9, DT on G8
//   Joystick     KY-023: X on G9, Y on G8
//   Outputs      LED, laser, relay, buzzers on G9 and G8 (no divider: here the robot drives)
//   RGB LED      KY-009 / KY-016: R on G9, G on G8, B on G17 (PORT.C), colours mixed with PWM
//   RF 433 MHz   receiver (XY-MK-5V) on G9, transmitter (FS1000A) on G8: remote codes with rc-switch
#include <M5Unified.h>
#include <OneWire.h>
#include <RCSwitch.h>
#include "board.h"

static constexpr int      PIN_A       = 9;       // PORT.B, white
static constexpr int      PIN_B       = 8;       // PORT.B, yellow
static constexpr int      PIN_C       = 17;      // PORT.C, only for the blue of the RGB LED
static constexpr float    DIVIDER     = 1.5f;    // 10k + 20k: the sensor's volts = the pin's volts x 1.5
static constexpr uint8_t  BRIGHTNESS  = 179;     // 70 %
static constexpr uint8_t  LED_ON      = 40;      // well under LED_MAX_LEVEL
static constexpr int      W = 320, H = 240, TOP = 26, FOOT = 22;
static constexpr int      BODY_H = H - TOP - FOOT;
static constexpr int      TONE_CH = 0;
static constexpr int      RGB_CH  = 2;       // LEDC channels 2, 3, 4: timers apart from the tone's

// Menu order: the modes already tested on the robot come first.
// Analog and Encoder go last: they still don't work well on the robot.
enum Mode { M_MENU, M_DIGITAL, M_DHT, M_OUTPUTS, M_RGB, M_DS18B20, M_JOYSTICK, M_RF, M_ANALOG, M_ENCODER, M_MODE_COUNT };
struct ModeInfo { const char* name; const char* menu; const char* wiring; uint16_t color; };
static const ModeInfo MODES[M_MODE_COUNT] = {
    { "Kit sensors - PORT.B: pick a type", "", "", TFT_DARKGREY },
    { "Digital", "Digital", "S -> divider -> white (G9)", TFT_GREEN },
    { "Temp & humidity (DHT11)", "Temp & humidity", "S to white (G9), 20k to GND", TFT_ORANGE },
    { "Outputs", "Outputs", "S to white (G9) / yellow (G8)", TFT_YELLOW },
    { "RGB LED", "RGB LED", "R white(G9) G yellow(G8) B G17 - GND", TFT_PINK },
    { "Digital temp (DS18B20)", "Digital temp", "S to white (G9), 10k to GND", TFT_ORANGE },
    { "Joystick", "Joystick", "VRx -div-> white(G9), VRy -> yellow(G8)", TFT_MAGENTA },
    { "RF 433 MHz", "RF 433 MHz", "RX -div-> white(G9), TX -> yellow(G8)", TFT_SKYBLUE },
    { "Analog", "Analog", "S or A0 -> divider -> white (G9)", TFT_CYAN },
    { "Encoder", "Encoder", "CLK white(G9), DT yellow(G8), 20k GND", TFT_MAGENTA },
};
static constexpr int MENU_ROWS  = M_MODE_COUNT / 2;              // two columns, the menu itself not counted
static constexpr int MENU_ROW_H = (H - TOP - 4) / MENU_ROWS;

static auto& lcd = M5.Display;
static M5Canvas s_body(&M5.Display);
static Mode s_mode = M_MENU;

// ---------------------------------------------------------------- per-mode state
static int s_lastDigital = -1;
static uint32_t s_changes = 0, s_lastChangeMs = 0;
static int16_t s_graph[W];                 // analog: last W samples, pin mV
static int s_graphPos = 0;
static float s_dhtT = NAN, s_dhtH = NAN, s_dsT = NAN;
static String s_sensorMsg;
static OneWire* s_ow = nullptr;
static uint32_t s_dsStartedMs = 0;
static volatile int32_t s_encCount = 0;
static volatile uint8_t s_encState = 0;
static bool s_out[2] = { false, false };
static bool s_melody = false;
static uint8_t s_rgb[3] = { 0, 0, 0 };   // RGB LED: PWM duty of R, G and B
// RF 433 MHz. Two instances: send() switches off its own instance's receiver while it transmits,
// so a separate one keeps listening and the robot hears its own transmitter.
static RCSwitch s_rfRx, s_rfTx;
static bool s_rfOn = false;
static unsigned long s_rfCode = 0;
static unsigned s_rfBits = 24, s_rfProto = 1, s_rfPulse = 350;
static uint32_t s_rfCount = 0, s_rfMs = 0;
static unsigned long s_rfRemote = 0;      // last code from a remote (not our own), to replay it
static unsigned long s_rfNext = 1001, s_rfSent = 0;   // our sends: a new number each time
static uint32_t s_rfSends = 0, s_rfHeard = 0;
static bool s_rfSentHeard = false;
static uint32_t s_melodyMs = 0;
static int s_melodyNote = 0;

// ---------------------------------------------------------------- helpers
static int pinMv(int pin) { return analogReadMilliVolts(pin); }
static float sensorV(int mv) { return mv * DIVIDER / 1000.0f; }

static void ledsLevel(float f) {   // base LEDs follow a 0..1 level, in the mode's color
    const uint16_t c = MODES[s_mode].color;
    const float k = constrain(f, 0.0f, 1.0f) * LED_ON;
    setBaseLeds(((c >> 11) & 0x1F) * k / 31, ((c >> 5) & 0x3F) * k / 63, (c & 0x1F) * k / 31);
}

static bool s_irqAttached = false;

static void releasePins() {
    if (s_irqAttached) { detachInterrupt(PIN_A); detachInterrupt(PIN_B); s_irqAttached = false; }
    if (s_rfOn) { s_rfRx.disableReceive(); s_rfTx.disableTransmit(); s_rfOn = false; }
    ledcDetachPin(PIN_A);
    ledcDetachPin(PIN_B);
    ledcDetachPin(PIN_C);
    delete s_ow;
    s_ow = nullptr;
    pinMode(PIN_A, INPUT);
    pinMode(PIN_B, INPUT);
    pinMode(PIN_C, INPUT);
    s_melody = false;
    setBaseLeds(0, 0, 0);
}

// KY-040 quadrature: a table on the (previous, current) state of CLK and DT
// discards contact bounce instead of counting it.
static void IRAM_ATTR onEncoder() {
    static const int8_t STEP[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
    s_encState = ((s_encState << 2) | (digitalRead(PIN_A) << 1) | digitalRead(PIN_B)) & 0x0F;
    s_encCount += STEP[s_encState];
}

// DHT11: the robot pulls the line low 20 ms, then the sensor answers with 40
// bits; a short high pulse (~27 µs) is a 0, a long one (~70 µs) a 1.
// Returns nullptr on success, or where it failed (for the screen).
// startUs: how long the robot holds the line low to wake the sensor. The DHT11
// datasheet asks for >= 18 ms; DHT22/AM2302 and many clones want ~1 ms.
static const char* readDht11(float& t, float& h, uint32_t startUs, bool& dht22) {
    uint8_t data[5] = {};
    pinMode(PIN_A, INPUT);
    delayMicroseconds(50);
    if (digitalRead(PIN_A) == LOW) return "Line at 0 V: S not connected or no pull-up";
    auto waitWhile = [](int level, uint32_t timeoutUs) -> int32_t {
        const uint32_t t0 = micros();
        while (digitalRead(PIN_A) == level)
            if (micros() - t0 > timeoutUs) return -1;
        return micros() - t0;
    };
    pinMode(PIN_A, OUTPUT);
    digitalWrite(PIN_A, LOW);
    if (startUs >= 2000) delay(startUs / 1000);
    else delayMicroseconds(startUs);
    pinMode(PIN_A, INPUT);   // release: the module's pull-up takes the line high
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    const char* err = nullptr;
    int bits = 0;
    portENTER_CRITICAL(&mux);   // ~5 ms with no interruptions: the timing is in microseconds
    if (waitWhile(HIGH, 1000) < 0) err = "Sensor not answering: check + and -";
    else if (waitWhile(LOW, 120) < 0 || waitWhile(HIGH, 120) < 0) err = "Odd answer on wake-up";
    for (; !err && bits < 40; bits++) {
        if (waitWhile(LOW, 100) < 0) { err = "Cut off in the middle of the data"; break; }
        const int32_t high = waitWhile(HIGH, 120);
        if (high < 0) { err = "Cut off in the middle of the data"; break; }
        data[bits / 8] = (data[bits / 8] << 1) | (high > 45);
    }
    portEXIT_CRITICAL(&mux);
    if (err) return err;
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) return "Corrupted data (checksum)";
    // DHT22/AM2302: 16-bit values in tenths, so humidity's high byte is 0-3 (up to 1000 = 100.0 %).
    // DHT11: whole percent in the first byte, never below 20 %.
    dht22 = data[0] <= 3;
    if (dht22) {
        h = ((data[0] << 8) | data[1]) / 10.0f;
        t = (((data[2] & 0x7F) << 8) | data[3]) / 10.0f;
    } else {
        h = data[0] + data[1] * 0.1f;
        t = (data[2] & 0x7F) + (data[3] & 0x0F) * 0.1f;
    }
    if (data[2] & 0x80) t = -t;
    return nullptr;
}

// ---------------------------------------------------------------- drawing
static void drawFrame() {
    const ModeInfo& m = MODES[s_mode];
    lcd.fillScreen(TFT_BLACK);
    lcd.fillRect(0, 0, W, TOP, m.color);
    lcd.setTextColor(TFT_BLACK, m.color);
    lcd.setTextDatum(middle_left);
    lcd.drawString(m.name, 8, TOP / 2);
    if (s_mode != M_MENU) {
        lcd.fillRoundRect(W - 74, 2, 70, TOP - 4, 6, TFT_BLACK);
        lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        lcd.setTextDatum(middle_center);
        lcd.drawString("Menu", W - 39, TOP / 2);
        lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        lcd.setTextDatum(middle_left);
        lcd.drawString(m.wiring, 6, H - FOOT / 2);
    }
    lcd.setTextDatum(top_left);
}

static void drawMenu() {
    drawFrame();
    for (int i = 1; i < M_MODE_COUNT; i++) {
        const int col = (i - 1) % 2, row = (i - 1) / 2;
        const int x = col * 160, y = TOP + 4 + row * MENU_ROW_H;
        lcd.fillRoundRect(x + 4, y, 152, MENU_ROW_H - 4, 8, MODES[i].color);
        lcd.setTextColor(TFT_BLACK, MODES[i].color);
        lcd.setTextDatum(middle_center);
        lcd.drawString(MODES[i].menu, x + 80, y + (MENU_ROW_H - 4) / 2);
    }
    lcd.setTextDatum(top_left);
}

static void bodyText(const char* s, int y, uint16_t color = TFT_WHITE) {
    s_body.setFont(&fonts::efontJA_16);
    s_body.setTextDatum(middle_center);
    s_body.setTextColor(color);
    s_body.drawString(s, W / 2, y);
}

static void bodyBig(const String& s, int y, uint16_t color) {
    s_body.setFont(&fonts::FreeSansBold24pt7b);
    s_body.setTextDatum(middle_center);
    s_body.setTextColor(color);
    s_body.drawString(s, W / 2, y);
}

// ---------------------------------------------------------------- modes
static void enterMode(Mode m) {
    releasePins();
    s_mode = m;
    s_lastDigital = -1;
    s_changes = 0;
    s_graphPos = 0;
    memset(s_graph, 0, sizeof(s_graph));
    s_dhtT = s_dhtH = s_dsT = NAN;
    s_sensorMsg = "";
    s_encCount = 0;
    s_out[0] = s_out[1] = false;
    memset(s_rgb, 0, sizeof(s_rgb));
    switch (m) {
        // Internal pull-down (~45 kOhm): with nothing connected the pin reads a steady 0
        // instead of picking up mains hum. A module on 5 V through the divider still gives ~2.9 V.
        case M_DIGITAL: pinMode(PIN_A, INPUT_PULLDOWN); break;
        case M_ANALOG:
        case M_JOYSTICK:
            analogSetPinAttenuation(PIN_A, ADC_11db);   // 0..3.1 V
            analogSetPinAttenuation(PIN_B, ADC_11db);
            break;
        case M_DS18B20: s_ow = new OneWire(PIN_A); s_dsStartedMs = 0; break;
        case M_ENCODER:
            pinMode(PIN_A, INPUT_PULLDOWN);
            pinMode(PIN_B, INPUT_PULLDOWN);
            attachInterrupt(PIN_A, onEncoder, CHANGE);
            attachInterrupt(PIN_B, onEncoder, CHANGE);
            s_irqAttached = true;
            break;
        case M_OUTPUTS:
            pinMode(PIN_A, OUTPUT); digitalWrite(PIN_A, LOW);
            pinMode(PIN_B, OUTPUT); digitalWrite(PIN_B, LOW);
            break;
        case M_RF:
            pinMode(PIN_A, INPUT);
            s_rfRx.enableReceive(digitalPinToInterrupt(PIN_A));
            s_rfTx.enableTransmit(PIN_B);
            s_rfOn = true;
            s_rfCode = s_rfRemote = s_rfSent = 0; s_rfBits = 24; s_rfProto = 1; s_rfPulse = 350;
            s_rfCount = s_rfSends = s_rfHeard = 0;
            break;
        case M_RGB: {
            const int pins[3] = { PIN_A, PIN_B, PIN_C };
            for (int i = 0; i < 3; i++) {
                ledcSetup(RGB_CH + i, 5000, 8);   // 5 kHz, duty 0..255
                ledcAttachPin(pins[i], RGB_CH + i);
                ledcWrite(RGB_CH + i, 0);
            }
            break;
        }
        default: break;
    }
    if (m == M_MENU) drawMenu();
    else drawFrame();
    Serial.printf("[MODE] %s\n", MODES[m].name);
}

static void updateDigital() {
    const int v = digitalRead(PIN_A);
    if (v != s_lastDigital) {
        if (s_lastDigital >= 0) { s_changes++; s_lastChangeMs = millis(); }
        s_lastDigital = v;
        ledsLevel(v);
        Serial.printf("G9 = %d (changes %u)\n", v, (unsigned)s_changes);
    }
    s_body.fillSprite(TFT_BLACK);
    s_body.fillCircle(80, BODY_H / 2 - 10, 50, v ? TFT_GREEN : TFT_DARKGREY);
    s_body.setFont(&fonts::FreeSansBold24pt7b);
    s_body.setTextDatum(middle_center);
    s_body.setTextColor(v ? TFT_BLACK : TFT_WHITE);
    s_body.drawString(String(v), 80, BODY_H / 2 - 10);
    s_body.setFont(&fonts::efontJA_16);
    s_body.setTextDatum(middle_left);
    s_body.setTextColor(TFT_WHITE);
    s_body.drawString(v ? "HIGH (3.3 V)" : "LOW (0 V)", 150, 50);
    s_body.drawString("Changes: " + String(s_changes), 150, 80);
    if (s_changes)
        s_body.drawString(String((millis() - s_lastChangeMs) / 1000.0f, 1) + " s ago", 150, 110);
    s_body.setTextColor(TFT_DARKGREY);
    s_body.drawString("Many modules give 0 when they detect", 10, BODY_H - 14);
}

static void updateAnalog() {
    const int mv = pinMv(PIN_A);
    s_graph[s_graphPos] = mv;
    s_graphPos = (s_graphPos + 1) % W;
    ledsLevel(mv / 3100.0f);
    static uint32_t lastLog = 0;
    const int mvB = pinMv(PIN_B);   // G8 too, to tell which Grove wire is which
    if (millis() - lastLog > 500) { lastLog = millis(); Serial.printf("G9 %d mV (sensor %.2f V)  G8 %d mV\n", mv, sensorV(mv), mvB); }

    s_body.fillSprite(TFT_BLACK);
    bodyBig(String(sensorV(mv), 2) + " V", 26, TFT_CYAN);
    bodyText(("G9: " + String(mv) + " mV   G8: " + String(mvB) + " mV").c_str(), 58, TFT_LIGHTGREY);
    const int gy = 72, gh = BODY_H - gy - 4;
    s_body.drawRect(0, gy, W, gh, TFT_DARKGREY);
    for (int x = 1; x < W; x++) {   // oldest on the left
        const int a = s_graph[(s_graphPos + x - 1) % W], b = s_graph[(s_graphPos + x) % W];
        s_body.drawLine(x - 1, gy + gh - 1 - a * (gh - 2) / 3300, x, gy + gh - 1 - b * (gh - 2) / 3300, TFT_CYAN);
    }
}

static void updateDht() {
    static uint32_t last = 0;
    if (!last || millis() - last > 2000) {   // the DHT11 needs ~1 s between readings
        last = millis();
        float t, h;
        bool dht22 = false;
        static uint32_t startUs = 20000;   // remembers the wake-up pulse that worked
        const char* err = readDht11(t, h, startUs, dht22);
        if (err && strcmp(err, "Sensor not answering: check + and -") == 0) {
            const uint32_t other = startUs == 20000 ? 1200 : 20000;   // try the other wake-up pulse
            delay(50);
            if (!readDht11(t, h, other, dht22)) { startUs = other; err = nullptr; }
        }
        if (!err) {
            s_dhtT = t; s_dhtH = h;
            Serial.printf("%s %.1f C %.1f %% (wake-up %u us)\n", dht22 ? "DHT22" : "DHT11", t, h, (unsigned)startUs);
        }
        else if (s_sensorMsg != err) Serial.printf("DHT11: %s\n", err);   // only when it changes
        s_sensorMsg = err ? err : "";
        ledsLevel(isnan(s_dhtH) ? 0 : s_dhtH / 100);
    }
    s_body.fillSprite(TFT_BLACK);
    bodyBig(isnan(s_dhtT) ? String("--") : String(s_dhtT, 1) + " C", 40, TFT_ORANGE);
    bodyBig(isnan(s_dhtH) ? String("--") : String(s_dhtH, 0) + " %", 105, TFT_CYAN);
    bodyText("Temperature (C) and relative humidity", 145, TFT_LIGHTGREY);
    if (s_sensorMsg.length()) bodyText(s_sensorMsg.c_str(), BODY_H - 14, TFT_RED);
}

static void updateDs18b20() {
    // 1-Wire: reset, "skip ROM" (only one sensor), convert, wait 750 ms, read the scratchpad.
    if (!s_dsStartedMs) {
        if (s_ow->reset()) {
            s_ow->write(0xCC); s_ow->write(0x44);
            s_dsStartedMs = millis();
        } else {
            s_sensorMsg = "No answer: check S, + and -";
        }
    } else if (millis() - s_dsStartedMs > 800) {
        uint8_t d[9];
        s_ow->reset(); s_ow->write(0xCC); s_ow->write(0xBE);
        for (auto& b : d) b = s_ow->read();
        if (OneWire::crc8(d, 8) == d[8] && !(d[0] == 0xFF && d[1] == 0xFF)) {
            s_dsT = (int16_t)((d[1] << 8) | d[0]) / 16.0f;
            s_sensorMsg = "";
            Serial.printf("DS18B20 %.2f C\n", s_dsT);
            ledsLevel((s_dsT - 15) / 20);
        } else {
            s_sensorMsg = "Corrupted reading (CRC)";
        }
        s_dsStartedMs = 0;
    }
    s_body.fillSprite(TFT_BLACK);
    bodyBig(isnan(s_dsT) ? String("--") : String(s_dsT, 2) + " C", 60, TFT_ORANGE);
    bodyText("Temperature (C), resolution 0.0625 C", 110, TFT_LIGHTGREY);
    if (s_sensorMsg.length()) bodyText(s_sensorMsg.c_str(), BODY_H - 14, TFT_RED);
}

static void updateEncoder() {
    const int32_t steps = s_encCount / 4;   // KY-040: 4 transitions per click
    static int32_t lastSteps = 0;
    if (steps != lastSteps) { Serial.printf("Encoder %d\n", (int)steps); lastSteps = steps; }
    s_body.fillSprite(TFT_BLACK);
    const int cx = W / 2, cy = BODY_H / 2 - 6, r = 60;
    s_body.drawCircle(cx, cy, r, TFT_DARKGREY);
    const float a = steps * (TWO_PI / 20) - HALF_PI;   // 20 clicks per turn
    s_body.drawWideLine(cx, cy, cx + cos(a) * (r - 8), cy + sin(a) * (r - 8), 6, TFT_MAGENTA);
    s_body.setFont(&fonts::FreeSansBold24pt7b);
    s_body.setTextDatum(middle_left);
    s_body.setTextColor(TFT_WHITE);
    s_body.drawString(String(steps), cx + r + 14, cy);
    bodyText("Turn the knob: + to the right, - to the left", BODY_H - 14, TFT_LIGHTGREY);
    ledsLevel((steps % 20 + 20) % 20 / 20.0f);
}

static void updateJoystick() {
    const int x = pinMv(PIN_A), y = pinMv(PIN_B);
    static uint32_t lastLog = 0;
    if (millis() - lastLog > 300) { lastLog = millis(); Serial.printf("Joystick X %d mV  Y %d mV\n", x, y); }
    s_body.fillSprite(TFT_BLACK);
    const int side = BODY_H - 10, x0 = 10, y0 = 5;
    s_body.drawRect(x0, y0, side, side, TFT_DARKGREY);
    s_body.drawLine(x0 + side / 2, y0, x0 + side / 2, y0 + side, TFT_DARKGREY);
    s_body.drawLine(x0, y0 + side / 2, x0 + side, y0 + side / 2, TFT_DARKGREY);
    s_body.fillCircle(x0 + x * side / 3300, y0 + y * side / 3300, 10, TFT_MAGENTA);
    s_body.setFont(&fonts::efontJA_16);
    s_body.setTextDatum(top_left);
    s_body.setTextColor(TFT_WHITE);
    s_body.drawString("X: " + String(sensorV(x), 2) + " V", side + 24, 30);
    s_body.drawString("Y: " + String(sensorV(y), 2) + " V", side + 24, 60);
    s_body.setTextColor(TFT_LIGHTGREY);
    s_body.drawString("At rest,", side + 24, 110);
    s_body.drawString("~2.5 V each axis", side + 24, 130);
    ledsLevel(fabsf(x - 1650) / 1650.0f);
}

// Passive buzzer: a short tune with LEDC tones on G9.
static const int MELODY[][2] = { { 523, 200 }, { 659, 200 }, { 784, 200 }, { 1047, 400 }, { 0, 200 }, { 784, 200 }, { 1047, 600 } };

static void updateOutputs() {
    if (s_melody && millis() - s_melodyMs >= (uint32_t)MELODY[s_melodyNote][1]) {
        s_melodyNote++;
        s_melodyMs = millis();
        if (s_melodyNote >= (int)(sizeof(MELODY) / sizeof(MELODY[0]))) {
            s_melody = false;
            ledcWriteTone(TONE_CH, 0);
            ledcDetachPin(PIN_A);
            pinMode(PIN_A, OUTPUT);
            digitalWrite(PIN_A, s_out[0]);
        } else {
            ledcWriteTone(TONE_CH, MELODY[s_melodyNote][0]);
        }
    }
    s_body.fillSprite(TFT_BLACK);
    const char* labels[2] = { "white (G9)", "yellow (G8)" };
    for (int i = 0; i < 2; i++) {
        const uint16_t c = s_out[i] ? TFT_YELLOW : TFT_DARKGREY;
        s_body.fillRoundRect(10 + i * 155, 8, 145, 80, 10, c);
        s_body.setFont(&fonts::efontJA_16);
        s_body.setTextDatum(middle_center);
        s_body.setTextColor(TFT_BLACK);
        s_body.drawString(labels[i], 82 + i * 155, 32);
        s_body.drawString(s_out[i] ? "ON" : "off", 82 + i * 155, 62);
    }
    const uint16_t mc = s_melody ? TFT_RED : TFT_BLUE;
    s_body.fillRoundRect(10, 100, 300, 56, 10, mc);
    s_body.setTextColor(TFT_WHITE);
    s_body.drawString(s_melody ? "Playing..." : "Tune on white (G9), passive buzzer", W / 2, 128);
    s_body.setTextColor(TFT_LIGHTGREY);
    s_body.drawString("LED, laser, relay, active buzzer: on/off", W / 2, BODY_H - 12);
}

static void touchOutputs(int x, int y) {
    const int by = y - TOP;
    if (by >= 8 && by < 88) {
        const int i = x < W / 2 ? 0 : 1;
        if (i == 0 && s_melody) return;
        s_out[i] = !s_out[i];
        digitalWrite(i == 0 ? PIN_A : PIN_B, s_out[i]);
        Serial.printf("G%d = %d\n", i == 0 ? PIN_A : PIN_B, s_out[i]);
        ledsLevel(s_out[0] || s_out[1]);
    } else if (by >= 100 && by < 156 && !s_melody) {
        ledcSetup(TONE_CH, 1000, 10);
        ledcAttachPin(PIN_A, TONE_CH);
        s_melody = true;
        s_melodyNote = 0;
        s_melodyMs = millis();
        ledcWriteTone(TONE_CH, MELODY[0][0]);
    }
}

// RGB LED: three sliders (R, G, B) set the PWM duty of G9, G8 and G17.
static constexpr int SLIDER_X = 44, SLIDER_W = 196, SLIDER_H = 40, SLIDER_GAP = 50;

static void updateRgb() {
    static const uint16_t colors[3] = { TFT_RED, TFT_GREEN, TFT_BLUE };
    static const char* labels[3] = { "R", "G", "B" };
    s_body.fillSprite(TFT_BLACK);
    s_body.setFont(&fonts::efontJA_16);
    for (int i = 0; i < 3; i++) {
        const int y = 8 + i * SLIDER_GAP;
        s_body.setTextDatum(middle_center);
        s_body.setTextColor(colors[i]);
        s_body.drawString(labels[i], 20, y + SLIDER_H / 2);
        s_body.drawRoundRect(SLIDER_X, y, SLIDER_W, SLIDER_H, 6, TFT_DARKGREY);
        s_body.fillRoundRect(SLIDER_X, y, s_rgb[i] * SLIDER_W / 255, SLIDER_H, 6, colors[i]);
        s_body.setTextColor(TFT_WHITE);
        s_body.drawString(String(s_rgb[i] * 100 / 255) + " %", SLIDER_X + SLIDER_W / 2, y + SLIDER_H / 2);
    }
    const uint16_t mix = lcd.color565(s_rgb[0], s_rgb[1], s_rgb[2]);
    s_body.fillRoundRect(250, 8, 62, 2 * SLIDER_GAP + SLIDER_H, 10, mix);
    s_body.drawRoundRect(250, 8, 62, 2 * SLIDER_GAP + SLIDER_H, 10, TFT_DARKGREY);
    s_body.setTextDatum(middle_center);
    s_body.setTextColor(TFT_LIGHTGREY);
    s_body.drawString("Drag the bars. Resistors: R 150, G/B 100 ohm", W / 2, BODY_H - 12);
}

static void touchRgb(int x, int y) {
    const int by = y - TOP - 8;
    const int i = by / SLIDER_GAP;
    if (by < 0 || i > 2 || by % SLIDER_GAP >= SLIDER_H) return;
    const uint8_t v = constrain((x - SLIDER_X) * 255 / SLIDER_W, 0, 255);
    if (v == s_rgb[i]) return;
    s_rgb[i] = v;
    ledcWrite(RGB_CH + i, v);
    setBaseLeds(s_rgb[0] * LED_ON / 255, s_rgb[1] * LED_ON / 255, s_rgb[2] * LED_ON / 255);
    static uint32_t lastLog = 0;
    if (millis() - lastLog > 200) { lastLog = millis(); Serial.printf("RGB %u %u %u\n", s_rgb[0], s_rgb[1], s_rgb[2]); }
}

// RF 433 MHz: rc-switch decodes the common remote and socket protocols (PT2262, EV1527...).
// The receiver's DATA goes through the divider to G9; the transmitter's DATA, direct on G8.
static void updateRf() {
    if (s_rfRx.available()) {
        const unsigned long code = s_rfRx.getReceivedValue();
        if (code) {
            s_rfCode = code;
            s_rfCount++;
            s_rfMs = millis();
            ledsLevel(1);
            const bool own = code == s_rfSent;
            if (own && !s_rfSentHeard) { s_rfSentHeard = true; s_rfHeard++; }   // the 8 repeats count once
            if (!own) {   // a remote: keep it to replay it
                s_rfBits = s_rfRx.getReceivedBitlength();
                s_rfProto = s_rfRx.getReceivedProtocol();
                s_rfPulse = s_rfRx.getReceivedDelay();
                s_rfRemote = code;
            }
            Serial.printf("RF code %lu (0x%lX) %u bits, protocol %u, pulse %u us%s\n", code, code,
                          s_rfRx.getReceivedBitlength(), s_rfRx.getReceivedProtocol(), s_rfRx.getReceivedDelay(),
                          own ? " (own transmitter)" : "");
        }
        s_rfRx.resetAvailable();
    }
    if (s_rfCount && millis() - s_rfMs > 300) ledsLevel(0);

    s_body.fillSprite(TFT_BLACK);
    if (s_rfCount) {
        bodyBig(String(s_rfCode), 26, TFT_SKYBLUE);
        bodyText(s_rfCode == s_rfSent ? "from the robot's own transmitter" : "from a remote", 58);
        bodyText(("Received " + String((millis() - s_rfMs) / 1000.0f, 1) + " s ago").c_str(), 80, TFT_LIGHTGREY);
    } else {
        bodyText("Press a 433 MHz remote button", 30);
        bodyText("or send a number with the robot", 52);
        bodyText("Nothing received yet", 80, TFT_LIGHTGREY);
    }
    s_body.setTextDatum(middle_center);
    s_body.fillRoundRect(10, 98, 145, 62, 10, TFT_BLUE);
    s_body.setTextColor(TFT_WHITE);
    s_body.drawString("Send", 82, 116);
    s_body.drawString(String(s_rfNext), 82, 140);
    const uint16_t rc = s_rfRemote ? TFT_DARKGREEN : TFT_DARKGREY;
    s_body.fillRoundRect(165, 98, 145, 62, 10, rc);
    s_body.drawString("Replay remote", 237, 116);
    s_body.drawString(s_rfRemote ? String(s_rfRemote) : String("none yet"), 237, 140);
    s_body.setTextColor(TFT_LIGHTGREY);
    const String heard = "Own sends heard: " + String(s_rfHeard) + " of " + String(s_rfSends);
    s_body.drawString(heard.c_str(), W / 2, BODY_H - 12);
}

static void rfSend(unsigned long code, unsigned bits, unsigned proto, unsigned pulse) {
    s_rfTx.setProtocol(proto);
    s_rfTx.setPulseLength(pulse);
    s_rfTx.setRepeatTransmit(8);
    s_rfTx.send(code, bits);   // blocks ~0.3 s
    Serial.printf("RF sent %lu, %u bits, protocol %u\n", code, bits, proto);
}

static void touchRf(int x, int y) {
    const int by = y - TOP;
    if (by < 98 || by >= 160) return;
    if (x < W / 2) {   // a new number each time, to tell the sends apart
        s_rfSent = s_rfNext++;
        s_rfSentHeard = false;
        s_rfSends++;
        rfSend(s_rfSent, 24, 1, 350);
    } else if (s_rfRemote) {
        rfSend(s_rfRemote, s_rfBits, s_rfProto, s_rfPulse);
    }
}

// ---------------------------------------------------------------- main
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    lcd.setBrightness(BRIGHTNESS);
    lcd.setFont(&fonts::efontJA_16);   // 16 px font for the screen texts
    // 5 V to the Grove ports; servos unpowered: the head does not move here and their
    // boost converter only adds noise to the 5 V the modules run on.
    if (!boardInit(false)) Serial.println("Base K151 not answering (PY32)");
    setBaseLeds(0, 0, 0);
    s_body.setPsram(true);
    s_body.setColorDepth(16);
    s_body.createSprite(W, BODY_H);
    Serial.println("\n[Demo 09 sensor kit] PORT.B: white (G9), yellow (G8)");
    enterMode(M_MENU);
}

void loop() {
    M5.update();
    if (M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) {
        const int x = M5.Touch.getDetail().x, y = M5.Touch.getDetail().y;
        if (s_mode == M_MENU) {
            if (y >= TOP + 4 && y < TOP + 4 + MENU_ROWS * MENU_ROW_H) {
                const int i = 1 + ((y - TOP - 4) / MENU_ROW_H) * 2 + (x >= 160);
                if (i < M_MODE_COUNT) enterMode((Mode)i);
            }
        } else if (y < TOP && x > W - 80) {
            enterMode(M_MENU);
        } else if (s_mode == M_OUTPUTS) {
            touchOutputs(x, y);
        } else if (s_mode == M_RF) {
            touchRf(x, y);
        } else if (s_mode == M_ENCODER) {
            s_encCount = 0;   // tap: back to zero
        }
    }
    if (s_mode == M_RGB && M5.Touch.getCount() && M5.Touch.getDetail().isPressed()) {
        const auto& t = M5.Touch.getDetail();   // sliders follow the finger
        if (t.y >= TOP) touchRgb(t.x, t.y);
    }

    static uint32_t lastDraw = 0;
    if (s_mode != M_MENU && millis() - lastDraw >= 40) {   // 25 frames per second
        lastDraw = millis();
        switch (s_mode) {
            case M_DIGITAL: updateDigital(); break;
            case M_ANALOG: updateAnalog(); break;
            case M_DHT: updateDht(); break;
            case M_DS18B20: updateDs18b20(); break;
            case M_ENCODER: updateEncoder(); break;
            case M_JOYSTICK: updateJoystick(); break;
            case M_OUTPUTS: updateOutputs(); break;
            case M_RGB: updateRgb(); break;
            case M_RF: updateRf(); break;
            default: break;
        }
        s_body.pushSprite(0, TOP);
    }
    delay(2);
}
