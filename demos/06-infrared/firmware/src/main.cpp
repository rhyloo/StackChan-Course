// Demo 06 - Infrared: the robot reads remote controls and repeats them.
//
// The K151 base has an IRM-56384 receiver (G10) and an emitter LED (G5), both
// at 38 kHz, the frequency of almost every TV, air conditioner or stereo remote.
//
// 1. Point any remote control at the robot and press a button: the screen shows
//    the protocol (NEC, Sony, Samsung...), the address, the command and the code.
// 2. The robot keeps the last code. Tap the screen and it sends it again through
//    its LED: now the robot is the remote (point its base at the TV).
// 3. Each button paints the screen and the base LEDs a color: the numbers on the
//    Samsung remote have their own (BUTTONS table, extend it with your remotes) and
//    the rest get a color made from their code, always the same for the same button.
// 4. The serial monitor prints the code ready to paste into your program.
#include <M5Unified.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <IRac.h>
#include "board.h"
#include "stackchan_limits.h"

static constexpr uint16_t IR_RX_PIN   = 10;
static constexpr uint16_t IR_TX_PIN   = 5;
static constexpr uint16_t CAPTURE_BUF = 1024;   // air conditioners send long frames
static constexpr uint8_t  FRAME_GAP_MS  = 50;     // gap that ends a frame; 15 is enough for TVs, ACs need more
static constexpr uint16_t KHZ         = 38;
static constexpr uint8_t  BRIGHTNESS  = 179;    // 70 %
static constexpr uint8_t  LED_ON      = 40;     // well under LED_MAX_LEVEL
static constexpr int      HISTORY     = 4;
static constexpr uint32_t DAMAGED_MS  = 1000;   // unknown frame this soon after a good one = the same one, damaged
static constexpr uint16_t MIN_UNKNOWN_PULSES = 40;   // shorter unknown bursts are noise (lamps, reflections); real remotes send more

// Buttons with a name and a color of their own. Add yours: copy the protocol and
// the code from the serial monitor ("Protocol : NEC", "Code : 0x20DF10EF").
struct Button {
    decode_type_t protocol;
    uint64_t code;
    const char* name;
    uint8_t r, g, b;
};
static const Button BUTTONS[] = {
    { SAMSUNG, 0xE0E020DF, "1", 255,   0,   0 },   // red
    { SAMSUNG, 0xE0E0A05F, "2", 255, 128,   0 },   // orange
    { SAMSUNG, 0xE0E0609F, "3", 255, 230,   0 },   // yellow
    { SAMSUNG, 0xE0E010EF, "4",   0, 200,   0 },   // green
    { SAMSUNG, 0xE0E0906F, "5",   0, 200, 255 },   // cyan
    { SAMSUNG, 0xE0E050AF, "6",   0,  60, 255 },   // blue
    { SAMSUNG, 0xE0E030CF, "7", 150,   0, 255 },   // purple
    { SAMSUNG, 0xE0E0B04F, "8", 255,   0, 150 },   // pink
    { SAMSUNG, 0xE0E0708F, "9", 140,  70,  20 },   // brown
    { SAMSUNG, 0xE0E08877, "0", 255, 255, 255 },   // white
};
static constexpr uint32_t SAME_BUTTON_MS = 400;   // a held button repeats its frame: don't redraw for each one

static IRrecv s_rx(IR_RX_PIN, CAPTURE_BUF, FRAME_GAP_MS, true);
static IRsend s_tx(IR_TX_PIN);
static decode_results s_res;
static auto& lcd = M5.Display;

// The learned code: what a tap sends back.
struct Learned {
    bool valid = false;
    decode_type_t type = UNKNOWN;
    uint64_t value = 0;
    uint16_t bits = 0;
    uint8_t state[kStateSizeMax] = {};   // air conditioners: the whole state, not a single value
    uint16_t* raw = nullptr;              // unknown protocols: the raw timings
    uint16_t rawLen = 0;
};
static Learned s_learned;
static String s_history[HISTORY];
static uint32_t s_count = 0, s_ledOffMs = 0;
static uint8_t s_color[3] = { 0, 0, 0 };   // color of the last button: screen and base LEDs

static String codeLabel(const decode_results& r) {
    String s = typeToString(r.decode_type, false);
    if (r.decode_type == UNKNOWN) s += " (" + String(getCorrectedRawLength(&r)) + " pulses)";
    else if (hasACState(r.decode_type)) s += " " + String(r.bits) + " bits";
    else s += " 0x" + uint64ToString(r.value, 16);
    return s;
}

static const Button* findButton(const decode_results& r) {
    for (const auto& b : BUTTONS)
        if (b.protocol == r.decode_type && b.code == r.value) return &b;
    return nullptr;
}

// Unlisted buttons: a hue from the code, so the same button always gets the same color.
static void colorFromCode(const decode_results& r, uint8_t rgb[3]) {
    const uint32_t h = (uint32_t)(r.value ^ (r.value >> 32) ^ r.decode_type) * 2654435761u;
    const int sector = (h >> 24) * 6 / 256, f = ((h >> 24) * 6) % 256;
    const uint8_t up = f, down = 255 - f;
    const uint8_t table[6][3] = { {255, up, 0}, {down, 255, 0}, {0, 255, up}, {0, down, 255}, {up, 0, 255}, {255, 0, down} };
    memcpy(rgb, table[sector], 3);
}

static void baseLedsFromColor() {
    setBaseLeds(s_color[0] * LED_ON / 255, s_color[1] * LED_ON / 255, s_color[2] * LED_ON / 255);
}

static void drawScreen(const decode_results* r) {
    lcd.fillScreen(TFT_BLACK);
    lcd.fillRect(0, 0, lcd.width(), 24, TFT_MAGENTA);
    lcd.setTextColor(TFT_BLACK, TFT_MAGENTA);
    lcd.setCursor(6, 4);
    lcd.print("Infrared: point a remote");
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(0, 30);
    if (!r) {
        lcd.println("Point a remote control at the robot");
        lcd.println("and press any button.");
        lcd.println();
        lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
        lcd.println("Then tap the screen: the robot");
        lcd.println("repeats the last button.");
        return;
    }
    // The button's color fills the screen; its name (or its code) goes in big letters.
    const Button* b = findButton(*r);
    uint8_t rgb[3];
    String big;
    if (b) { rgb[0] = b->r; rgb[1] = b->g; rgb[2] = b->b; big = b->name; }
    else {
        colorFromCode(*r, rgb);
        big = r->decode_type == UNKNOWN ? String("?") : "0x" + uint64ToString(r->command ? r->command : r->value, 16);
    }
    memcpy(s_color, rgb, 3);
    const uint16_t bg = lcd.color565(rgb[0], rgb[1], rgb[2]);
    const uint16_t fg = rgb[0] * 3 + rgb[1] * 6 + rgb[2] > 1100 ? TFT_BLACK : TFT_WHITE;   // dark text on light colors
    lcd.fillScreen(bg);
    lcd.setTextColor(fg, bg);
    lcd.setTextDatum(middle_center);
    lcd.setFont(&fonts::FreeSansBold24pt7b);
    lcd.setTextSize(big.length() <= 2 ? 3 : 1);
    lcd.drawString(big, lcd.width() / 2, 100);
    lcd.setTextSize(1);
    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextDatum(top_left);
    lcd.setCursor(6, 178);
    lcd.printf("%s  %s\n", codeLabel(*r).c_str(), r->repeat ? "(repeat)" : "");
    lcd.setCursor(6, lcd.height() - 20);
    lcd.print("Tap the screen to send it again");
}

static void learn(const decode_results& r) {
    s_learned.type = r.decode_type;
    s_learned.bits = r.bits;
    s_learned.value = r.value;
    if (hasACState(r.decode_type)) memcpy(s_learned.state, r.state, sizeof(s_learned.state));
    free(s_learned.raw);
    s_learned.raw = nullptr;
    s_learned.rawLen = 0;
    if (r.decode_type == UNKNOWN) {
        s_learned.raw = resultToRawArray(&r);   // malloc'ed by the library
        s_learned.rawLen = getCorrectedRawLength(&r);
    }
    s_learned.valid = true;
}

static bool sendLearned() {
    if (!s_learned.valid) return false;
    s_rx.disableIRIn();   // otherwise the robot hears its own LED
    bool ok = true;
    if (s_learned.type == UNKNOWN) s_tx.sendRaw(s_learned.raw, s_learned.rawLen, KHZ);
    else if (hasACState(s_learned.type)) ok = s_tx.send(s_learned.type, s_learned.state, s_learned.bits / 8);
    else ok = s_tx.send(s_learned.type, s_learned.value, s_learned.bits);
    s_rx.enableIRIn();
    return ok;
}

static void flashLeds(uint8_t r, uint8_t g, uint8_t b) {   // brief flash, then back to the button's color
    setBaseLeds(r, g, b);
    s_ledOffMs = millis() + 200;
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    lcd.setBrightness(BRIGHTNESS);
    lcd.setFont(&fonts::efontJA_16);   // accents and ñ
    lcd.setTextWrap(true);

    if (!boardInit()) Serial.println("Base K151 not answering (PY32)");
    setBaseLeds(0, 0, 0);

    s_rx.setUnknownThreshold(MIN_UNKNOWN_PULSES);
    s_rx.setTolerance(kTolerance);
    s_rx.enableIRIn();
    s_tx.begin();
    Serial.printf("\n[Demo 06 IR] receiver G%u, emitter G%u, %u kHz\n", IR_RX_PIN, IR_TX_PIN, KHZ);
    drawScreen(nullptr);
}

void loop() {
    M5.update();
    if (s_ledOffMs && (int32_t)(millis() - s_ledOffMs) > 0) { s_ledOffMs = 0; baseLedsFromColor(); }

    if (s_rx.decode(&s_res)) {
        if (s_res.overflow) Serial.printf("WARNING: frame longer than the %u-entry buffer\n", CAPTURE_BUF);
        // An unknown frame right after a good one is almost always that same button
        // arriving damaged (remote tilted, out of range): don't let it replace the good one.
        static uint32_t lastKnownMs = 0;
        const bool damaged = s_res.decode_type == UNKNOWN && s_learned.valid && millis() - lastKnownMs < DAMAGED_MS;
        if (s_res.decode_type != UNKNOWN) lastKnownMs = millis();
        if (damaged) {
            Serial.printf("Ignored a damaged frame (%u pulses)\n", (unsigned)getCorrectedRawLength(&s_res));
            s_rx.resume();
            return;
        }
        // A held button sends "repeat" frames: show them but keep the real code.
        if (!s_res.repeat) {
            s_count++;
            for (int i = HISTORY - 1; i > 0; i--) s_history[i] = s_history[i - 1];
            s_history[0] = codeLabel(s_res);
            learn(s_res);
            M5.Speaker.tone(2500, 30);
        }
        // Held button: the same frame every ~100 ms. Redraw only when it changes.
        static uint64_t lastValue = 0;
        static uint32_t lastDrawMs = 0;
        if (s_res.value != lastValue || millis() - lastDrawMs > SAME_BUTTON_MS) {
            drawScreen(&s_res);
            baseLedsFromColor();
        }
        lastValue = s_res.value;
        lastDrawMs = millis();

        Serial.print(resultToHumanReadableBasic(&s_res));
        const String ac = IRAcUtils::resultAcToString(&s_res);
        if (ac.length()) Serial.println("Message: " + ac);
        Serial.println(resultToSourceCode(&s_res));   // ready to paste: uint16_t rawData[...] = {...};
        s_rx.resume();
    }

    if (M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) {
        if (sendLearned()) {
            M5.Speaker.tone(4000, 40);
            flashLeds(LED_ON, 0, LED_ON);
            Serial.printf("Sent %s\n", s_history[0].c_str());
            const uint16_t bg = lcd.color565(s_color[0], s_color[1], s_color[2]);
            lcd.fillRect(0, lcd.height() - 20, lcd.width(), 20, bg);
            lcd.setTextColor(s_color[0] * 3 + s_color[1] * 6 + s_color[2] > 1100 ? TFT_BLACK : TFT_WHITE, bg);
            lcd.setCursor(6, lcd.height() - 20);
            lcd.printf("Sent: %s", s_history[0].c_str());
        } else {
            M5.Speaker.tone(500, 120);
            Serial.println(s_learned.valid ? "Send failed" : "Nothing learned yet");
        }
    }
    delay(5);
}
