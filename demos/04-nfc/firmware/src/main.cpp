// Demo 04 - NFC reader on Stack-chan.
//
// The K151 base has an ST25R3916 (I2C 0x50), on the touch panel board. It is an
// NFC reader/writer: it can read and write tags. This demo only reads:
//
// 1. Looks for NFC-A tags (ISO 14443A: NTAG, MIFARE Ultralight/Classic/DESFire,
//    most stickers and cards) every POLL_MS.
// 2. When you hold one up, it beeps, turns the base LEDs green and shows
//    its UID, type and memory on screen.
// 3. If the tag holds an NDEF message (what the phone writes with NFC Tools),
//    it draws each record: text, URL or MIME type.
// 4. If there is no NDEF, it prints a memory dump on the serial port.
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include "board.h"
#include "stackchan_limits.h"

using namespace m5::nfc;
using namespace m5::nfc::a;
using namespace m5::nfc::ndef;

static constexpr uint8_t  NFC_ADDR      = 0x50;
static constexpr uint32_t POLL_MS       = 200;
static constexpr uint32_t SAME_TAG_MS   = 1500;   // same UID again within this window = still on the reader
static constexpr uint8_t  BRIGHTNESS    = 179;    // 70 %

static m5::unit::UnitUnified s_units;
static m5::unit::UnitNFC s_unit{NFC_ADDR};
static m5::nfc::NFCLayerA s_nfc{s_unit};

static auto& lcd = M5.Display;
static std::string s_lastUid;
static uint32_t s_lastSeenMs = 0;

static void drawHeader(const char* msg, uint16_t color) {
    lcd.fillScreen(TFT_BLACK);
    lcd.fillRect(0, 0, lcd.width(), 24, color);
    lcd.setTextColor(TFT_BLACK, color);
    lcd.setCursor(6, 4);
    lcd.print(msg);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(0, 30);
}

static void drawWaiting() {
    drawHeader("NFC: bring a tag close", TFT_CYAN);
    lcd.println("Hold the card or sticker");
    lcd.println("1-2 cm from the robot's base.");
    lcd.println();
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.println("Reads NFC-A: NTAG, MIFARE");
    lcd.println("Ultralight / Classic / DESFire.");
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
}

static const char* tnfName(TNF t) {
    switch (t) {
        case TNF::Empty:     return "empty";
        case TNF::Wellknown: return "well-known";
        case TNF::MIMEMedia: return "MIME";
        case TNF::URI:       return "URI";
        case TNF::External:  return "external";
        default:             return "other";
    }
}

// Prints the NDEF records of the activated tag. Returns false on a read error.
static bool showNdef() {
    bool valid = false;
    if (!s_nfc.ndefIsValidFormat(valid)) return false;
    if (!valid) {
        lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
        lcd.println("No NDEF data (empty or custom format).");
        lcd.println("Memory dump: serial monitor.");
        Serial.println("No NDEF data on this tag");
        s_nfc.dump();   // raw memory to the serial log
        return true;
    }
    TLV msg{};
    if (!s_nfc.ndefRead(msg)) return false;
    if (!msg.isMessageTLV() || msg.records().empty()) {
        lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
        lcd.println("NDEF format, but no message.");
        return true;
    }
    int i = 1;
    for (auto&& r : msg.records()) {
        lcd.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
        lcd.printf("#%d %s %s (%u B)\n", i, tnfName(r.tnf()), r.type(), (unsigned)r.payloadSize());
        lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        if (r.tnf() == TNF::Wellknown || r.tnf() == TNF::URI || r.tnf() == TNF::External) {
            const auto text = r.payloadAsString();
            lcd.println(text.c_str());
            Serial.printf("Record %d TNF=%u type=%s: %s\n", i, (unsigned)r.tnf(), r.type(), text.c_str());
        } else {
            Serial.printf("Record %d TNF=%u type=%s (%u bytes)\n", i, (unsigned)r.tnf(), r.type(), (unsigned)r.payloadSize());
        }
        i++;
    }
    return true;
}

static void onTag(PICC& picc) {
    const bool known = s_nfc.identify(picc) && s_nfc.reactivate(picc);
    M5.Speaker.tone(3000, 40);
    setBaseLeds(0, LED_MAX_LEVEL, 0);

    drawHeader("NFC: tag read", TFT_GREEN);
    lcd.printf("UID:  %s\n", picc.uidAsString().c_str());
    lcd.printf("Type: %s\n", picc.typeAsString().c_str());
    lcd.printf("Memory: %u B user / %u B\n", (unsigned)picc.userAreaSize(), (unsigned)picc.totalSize());
    lcd.println();
    Serial.printf("Tag UID=%s type=%s ATQA=%04X SAK=%02X user=%u total=%u\n", picc.uidAsString().c_str(),
                  picc.typeAsString().c_str(), picc.atqa, picc.sak, (unsigned)picc.userAreaSize(),
                  (unsigned)picc.totalSize());

    if (!known) {
        lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
        lcd.println("Could not identify it: hold the");
        lcd.println("tag still and try again.");
    } else if (picc.isMifareClassic()) {
        // Classic sectors need a key; the dump uses the default one and prints what it can.
        lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
        lcd.println("MIFARE Classic: its sectors need a");
        lcd.println("key. Dump: serial monitor.");
        s_nfc.dump();
    } else if (!picc.supportsNDEF()) {
        lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
        lcd.println("This tag does not support NDEF.");
    } else if (!showNdef()) {
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.println("Read error: the tag moved.");
    }
    s_nfc.deactivate();
    setBaseLeds(0, 0, 0);
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    M5.Display.setBrightness(BRIGHTNESS);
    lcd.setFont(&fonts::efontJA_16);   // covers accents and ñ
    lcd.setTextWrap(true);

    if (!boardInit()) Serial.println("Base K151 not answering (PY32)");
    setBaseLeds(0, 0, 0);

    if (!s_units.add(s_unit, M5.In_I2C) || !s_units.begin()) {
        drawHeader("NFC: reader not found", TFT_RED);
        lcd.println("The ST25R3916 (I2C 0x50) does not answer.");
        lcd.println("Is the base seated properly?");
        Serial.println("ST25R3916 init failed");
        for (;;) delay(1000);
    }
    Serial.printf("NFC ready\n%s\n", s_units.debugInfo().c_str());
    drawWaiting();
}

void loop() {
    static uint32_t lastPoll = 0;
    M5.update();
    s_units.update();
    if (millis() - lastPoll < POLL_MS) { delay(5); return; }
    lastPoll = millis();

    PICC picc{};
    if (!s_nfc.detect(picc)) {
        // Back to the waiting screen a while after the tag is gone; a tap on the screen also clears it.
        if (M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) { s_lastUid.clear(); drawWaiting(); }
        return;
    }
    const auto uid = picc.uidAsString();
    const bool same = uid == s_lastUid && millis() - s_lastSeenMs < SAME_TAG_MS;
    s_lastSeenMs = millis();
    if (same) { s_nfc.deactivate(); return; }
    s_lastUid = uid;
    onTag(picc);
}
