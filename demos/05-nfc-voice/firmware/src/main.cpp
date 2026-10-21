// Demo 05 - Write NFC cards with your voice.
//
// You tell the robot "write on the card: hello world". Gemini (in the laptop
// relay) calls the write_card function, the relay sends "nfc write hello world"
// and the robot waits for you to bring a card close, writes it as NDEF text
// and reads it back to check it.
//
//   mic --AUDIO_IN--> relay --> Gemini Live --> write_card("hello world")
//                                               └-> CMD "nfc write hello world" --> robot --> card
//   robot --NFC 0x28 {"event":"write","ok":true...}--> relay --> Gemini: "Done!"
//
// Apart from that, every new card you bring close is read, drawn on screen and
// reported to the relay, so you can also ask "what does the card say?".
// The audio is demo 02's (audio.cpp); the NFC is demo 04's.
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <WiFi.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "board.h"
#include "link.h"
#include "audio.h"
#include "stackchan_limits.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

using namespace m5::nfc;
using namespace m5::nfc::a;
using namespace m5::nfc::ndef;

static constexpr uint8_t  T_NFC         = 0x28;   // robot -> laptop: JSON with a tag read or a write result
static constexpr uint8_t  NFC_ADDR      = 0x50;
static constexpr uint32_t POLL_MS       = 250;
static constexpr uint32_t WRITE_WAIT_MS = 20000;  // how long a write waits for a card
static constexpr size_t   MAX_TEXT      = 160;    // NTAG213 has 144 bytes: longer texts only fit bigger tags
static constexpr uint8_t  BRIGHTNESS    = 179;    // 70 %
static constexpr uint8_t  LED_ON        = 40;     // well under LED_MAX_LEVEL

static m5::unit::UnitUnified s_units;
static m5::unit::UnitNFC s_unit{NFC_ADDR};
static m5::nfc::NFCLayerA s_nfc{s_unit};
static bool s_nfcOk = false;

static M5Canvas status(&M5.Display);
static auto& lcd = M5.Display;
static constexpr int PANEL_H = 240 - 44;
static bool s_linkStarted = false;

// Requests from the network task, applied on the loop task (I2C only from loop()).
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_reqText[MAX_TEXT + 1];
static volatile bool s_reqWrite = false, s_reqRead = false, s_reqCancel = false;

// Loop-task state.
static bool s_writing = false;
static char s_writeText[MAX_TEXT + 1];
static uint32_t s_writeUntil = 0;

// --------------------------------------------------------------------------- link
static bool onCommand(const char* cmd) {
    if (!strncmp(cmd, "nfc write ", 10)) {
        portENTER_CRITICAL(&s_mux);
        strlcpy(s_reqText, cmd + 10, sizeof(s_reqText));
        s_reqWrite = true;
        portEXIT_CRITICAL(&s_mux);
        return true;
    }
    if (!strcmp(cmd, "nfc read")) { s_reqRead = true; return true; }
    if (!strcmp(cmd, "nfc cancel")) { s_reqCancel = true; return true; }
    return audioCommand(cmd);
}

// Appends s to out as a JSON string body (quotes and control characters escaped).
static void jsonEscape(String& out, const char* s) {
    for (; *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if ((uint8_t)c < 0x20) out += ' ';
        else out += c;
    }
}

static void sendEvent(const String& json) {
    linkSend(T_NFC, (const uint8_t*)json.c_str(), json.length());
    Serial.printf("[NFC] %s\n", json.c_str());
}

// --------------------------------------------------------------------------- screen
static void panel(uint16_t color, const char* title) {
    lcd.fillRect(0, 0, lcd.width(), PANEL_H, TFT_BLACK);
    lcd.fillRect(0, 0, lcd.width(), 24, color);
    lcd.setTextColor(TFT_BLACK, color);
    lcd.setCursor(6, 4);
    lcd.print(title);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(0, 32);
}

static void drawIdle() {
    panel(TFT_CYAN, "Tell me what to write on the card");
    lcd.println("Example: \"write on the card:");
    lcd.println("  my name is Stack-chan\".");
    lcd.println();
    lcd.println("Or bring a card close and ask");
    lcd.println("\"what does the card say?\".");
}

static void drawStatus() {
    LinkStats st;
    linkGetStats(&st);
    status.fillSprite(TFT_BLACK);
    status.setTextColor(TFT_LIGHTGREY);
    status.setCursor(4, 2);
    if (WiFi.status() != WL_CONNECTED) {
        status.setTextColor(TFT_ORANGE);
        status.printf("WiFi: connecting to %s...", WIFI_SSID);
    } else {
        status.printf("%s  stackchan-%s", WiFi.localIP().toString().c_str(), robotId());
    }
    status.setCursor(4, 16);
    if (!s_nfcOk) {
        status.setTextColor(TFT_RED);
        status.print("ERROR: NFC reader not found");
    } else if (st.relayOnline) {
        status.setTextColor(TFT_GREEN);
        status.printf("Laptop %s", st.relayIp);
    } else {
        status.setTextColor(TFT_YELLOW);
        status.print("Looking for the laptop (run.sh)...");
    }
    status.setCursor(4, 30);
    status.setTextColor(audioMicOn() ? TFT_RED : audioSpeaking() ? TFT_CYAN : TFT_LIGHTGREY);
    const char* state = audioMuted() ? "mic muted (tap to unmute)"
                      : audioSpeaking() ? "speaking"
                      : audioMicOn() ? "listening"
                      : audioHandsFree() ? "..." : "tap to talk";
    status.printf("Bat %d%%  %s", (int)M5.Power.getBatteryLevel(), state);
    status.pushSprite(0, PANEL_H);
}

// --------------------------------------------------------------------------- NFC
// Field off and on: tags already on the reader (halted after a read) reset and answer again.
static void resetField() {
    s_unit.disableField();
    delay(20);
    s_unit.enableField();
    delay(10);
}

// Text of all the text/URI records of the activated tag, one per line. False on a read error.
static bool readTexts(PICC& picc, String& out, bool& ndef) {
    out = "";
    ndef = false;
    if (!picc.supportsNDEF() || picc.isMifareClassic()) return true;
    bool valid = false;
    if (!s_nfc.ndefIsValidFormat(valid)) return false;
    if (!valid) return true;
    ndef = true;
    TLV msg{};
    if (!s_nfc.ndefRead(msg)) return false;
    if (!msg.isMessageTLV()) return true;
    for (auto&& r : msg.records()) {
        if (r.tnf() != TNF::Wellknown && r.tnf() != TNF::URI) continue;
        if (out.length()) out += '\n';
        out += r.payloadAsString().c_str();
    }
    return true;
}

static void onTagRead(PICC& picc) {
    String texts;
    bool ndef = false;
    const bool ok = s_nfc.identify(picc) && s_nfc.reactivate(picc) && readTexts(picc, texts, ndef);
    s_nfc.deactivate();
    audioBeep(3000, 40);

    panel(ok ? TFT_GREEN : TFT_ORANGE, ok ? "Card read" : "Could not read it: bring it closer");
    lcd.printf("%s  %s\n\n", picc.typeAsString().c_str(), picc.uidAsString().c_str());
    if (ok) lcd.println(texts.length() ? texts.c_str() : ndef ? "(no text)" : "(empty or not NDEF formatted)");

    String j = "{\"event\":\"tag\",\"ok\":";
    j += ok ? "true" : "false";
    j += ",\"uid\":\"" + String(picc.uidAsString().c_str()) + "\",\"type\":\"";
    jsonEscape(j, picc.typeAsString().c_str());
    j += "\",\"ndef\":";
    j += ndef ? "true" : "false";
    j += ",\"text\":\"";
    jsonEscape(j, texts.substring(0, 600).c_str());
    j += "\"}";
    sendEvent(j);
}

// Writes s_writeText as one NDEF text record ("es") and reads it back. nullptr = ok, else the reason.
static const char* writeTag(PICC& picc) {
    if (!s_nfc.identify(picc) || !s_nfc.reactivate(picc)) return "could not activate the card: keep it still";
    if (picc.isMifareClassic()) return "it is a MIFARE Classic: it needs keys, use an NTAG";
    if (picc.isMifareDESFire()) return "it is a DESFire: writing it would format it, use an NTAG";
    if (!picc.supportsNDEF()) return "this card does not support NDEF";
    bool valid = false;
    if (!s_nfc.ndefIsValidFormat(valid)) return "error reading the card";
    if (!valid) {
        // Blank Ultralight: write the capability container (one-time, cannot be undone).
        if (!picc.isMifareUltralight() || !s_nfc.mifareUltralightChangeFormatToNDEF()) return "the card is not NDEF formatted";
    }
    TLV msg{Tag::Message};
    Record r{};
    r.setTextPayload(s_writeText, "es");
    msg.push_back(r);
    if (msg.required() + 1 > picc.userAreaSize()) return "the text does not fit on this card";
    if (!s_nfc.ndefWrite(msg)) return "error writing: don't move it";

    String back;
    bool ndef = false;
    if (!readTexts(picc, back, ndef) || back != s_writeText) return "what was written does not match when read back";
    return nullptr;
}

static void startWrite(const char* text) {
    strlcpy(s_writeText, text, sizeof(s_writeText));
    s_writing = true;
    s_writeUntil = millis() + WRITE_WAIT_MS;
    resetField();
    setBaseLeds(LED_ON, LED_ON / 2, 0);
    panel(TFT_YELLOW, "Bring the card close to write");
    lcd.println(s_writeText);
}

static void finishWrite(PICC* picc, const char* error) {
    s_writing = false;
    String j = "{\"event\":\"write\",\"ok\":";
    j += error ? "false" : "true";
    if (picc) j += ",\"uid\":\"" + String(picc->uidAsString().c_str()) + "\"";
    j += ",\"text\":\"";
    jsonEscape(j, s_writeText);
    j += "\"";
    if (error) { j += ",\"error\":\""; jsonEscape(j, error); j += "\""; }
    j += "}";
    sendEvent(j);

    if (error) {
        audioBeep(500, 200);
        setBaseLeds(LED_ON, 0, 0);
        panel(TFT_RED, "Could not write");
        lcd.println(error);
    } else {
        audioBeep(4000, 60);
        setBaseLeds(0, LED_ON, 0);
        panel(TFT_GREEN, "Written!");
        lcd.println(s_writeText);
    }
}

static void nfcPoll() {
    static uint32_t lastPoll = 0;
    if (s_reqCancel) {
        s_reqCancel = false;
        if (s_writing) finishWrite(nullptr, "cancelled");
    }
    if (s_reqWrite) {
        char text[MAX_TEXT + 1];
        portENTER_CRITICAL(&s_mux);
        strlcpy(text, s_reqText, sizeof(text));
        s_reqWrite = false;
        portEXIT_CRITICAL(&s_mux);
        startWrite(text);
    }
    if (s_reqRead) {
        s_reqRead = false;
        resetField();   // a card already on the reader is read again
    }
    if (s_writing && (int32_t)(millis() - s_writeUntil) > 0) {
        finishWrite(nullptr, "no card arrived");
        return;
    }
    if (millis() - lastPoll < POLL_MS) return;
    lastPoll = millis();

    PICC picc{};
    if (!s_nfc.detect(picc)) return;   // REQA: a tag already read stays halted until it leaves the field
    if (s_writing) {
        const char* err = writeTag(picc);
        s_nfc.deactivate();
        finishWrite(&picc, err);
    } else {
        setBaseLeds(0, 0, LED_ON);
        onTagRead(picc);
        setBaseLeds(0, 0, 0);
    }
}

// --------------------------------------------------------------------------- main
void setup() {
    // Speaker peaks cause brief voltage dips; don't reset on them.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.begin(115200);
    lcd.setBrightness(BRIGHTNESS);
    lcd.setFont(&fonts::efontJA_16);   // accents and ñ
    lcd.setTextWrap(true);
    status.setColorDepth(16);
    status.createSprite(320, 44);

    M5.Speaker.begin();
    M5.Speaker.setVolume(120);

    bool base = boardInit();
    setBaseLeds(0, 0, 0);
    s_nfcOk = s_units.add(s_unit, M5.In_I2C) && s_units.begin();
    Serial.printf("\n[Demo 05 NFC + voice] base:%s nfc:%s\n", base ? "ok" : "NO", s_nfcOk ? "ok" : "ERROR");
    if (s_nfcOk) drawIdle();
    else {
        panel(TFT_RED, "Can't find the NFC reader");
        lcd.println("The ST25R3916 (I2C 0x50) does not answer.");
        lcd.println("Is the base seated properly?");
    }

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("stackchan");
    WiFi.setSleep(false);              // modem sleep adds 100+ ms of jitter to every packet
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    M5.Speaker.tone(2000, 80);   // before audioStart(): nothing else uses the speaker yet
}

void loop() {
    i2cLock();   // touch, PMIC, LEDs, NFC: the audio task also uses this bus
    M5.update();
    static uint32_t lastUi = 0, lastBatt = 0;
    uint32_t now = millis();

    if (!s_linkStarted && WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        linkStart();      // no camera here: the relay never asks for video unless a phone watches
        audioStart();
        linkSetCommandHandler(onCommand);   // after audioStart(): ours first, then the audio ones
        s_linkStarted = true;
    }

    if (now - lastBatt >= 2000) {
        lastBatt = now;
        float busV, mA;
        if (!readBatteryCurrent(&busV, &mA)) mA = NAN;
        linkSetBattery(M5.Power.getBatteryVoltage() / 1000.0f, M5.Power.getBatteryLevel(), mA);
    }

    PendingCommands pc;
    linkTakePending(&pc);
    if (pc.brightness >= 0) lcd.setBrightness(constrain(pc.brightness, 0, 255));
    if (pc.leds[0] >= 0) setBaseLeds(pc.leds[0], pc.leds[1], pc.leds[2]);

    if (s_nfcOk) {
        s_units.update();
        nfcPoll();
    }
    if (now - lastUi >= 500) {
        lastUi = now;
        drawStatus();
    }
    // Tap the screen: mute/unmute the mic; with a result on screen, back to the help first.
    if (s_linkStarted && M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) {
        if (!s_writing) drawIdle();
        audioTap();
    }
    i2cUnlock();
    delay(10);
}
