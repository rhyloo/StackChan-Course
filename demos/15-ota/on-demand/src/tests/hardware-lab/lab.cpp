// Test "hardware-lab" (OTA on demand): every part of the robot on one web page.
//
// The OTA is off, so the radio is the test's: it connects to the Wi-Fi of
// config/wifi_secrets.h itself and serves http://stackchan-XXXX.local (or its IP):
//   lab.cpp     this file: sensors, commands and the screen, all in the loop() task
//   web.cpp     the page and the camera (tasks on core 0)
//   audio.cpp   microphone and speaker (its own task on core 1)
//   index.html  the page
// The screen shows the address and a summary. Tap it: a QR code with the address.
// Hold it 3 s: the OTA menu. The page's "Activate OTA" button does the same.
//
// loop() owns I2C and the screen: the web tasks leave text commands in a queue
// ("leds 255 0 0", "head 30 10"...) and read the last state, as JSON. The same
// commands work typed on the serial monitor.
#include "ota.h"
#include "lab.h"
#include "board.h"
#include "camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <soc/rtc_cntl_reg.h>

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static constexpr uint8_t  LTR553_ADDR = 0x23;   // light and proximity, above the camera
static constexpr uint8_t  SI12T_ADDR  = 0x68;   // the base's 3-zone touch panel
static constexpr uint8_t  NFC_ADDR    = 0x50;   // ST25R3916
static constexpr uint16_t IR_RX_PIN = 10, IR_TX_PIN = 5;
static constexpr int      SD_SCK = 36, SD_MISO = 35, SD_MOSI = 37, SD_CS = 4;   // shared with the screen
static constexpr uint32_t SENSOR_MS = 150, SLOW_MS = 1000, NFC_MS = 300, DRAW_MS = 300;
static constexpr uint32_t SCREEN_TEST_MS = 3000;  // "screen R G B" shows the color this long
static constexpr uint32_t SAME_TAG_MS = 1500;     // the same NFC tag again within this = still there

static auto& lcd = M5.Display;
static M5Canvas s_canvas(&M5.Display);           // the whole screen, drawn and pushed at once
struct CommandLine { char text[96]; };
static QueueHandle_t s_commands;
static char s_json[2048];
static portMUX_TYPE s_jsonMux = portMUX_INITIALIZER_UNLOCKED;

// What answered at start.
static bool s_okBase, s_okCamera, s_okLight, s_okTouch, s_okNfc, s_okSd;
static uint32_t s_sdMb;

// Live values (loop task only).
static float s_ax, s_ay, s_az, s_gx, s_gy, s_gz, s_lux;
static int s_prox;
static float s_battV, s_battMa = NAN;
static int s_battPct;
static bool s_charging;
static uint8_t s_zones[3], s_zonesRaw;           // Si12T: 0 none, 1 low, 2 medium, 3 high
static int s_tapX = -1, s_tapY = -1, s_taps;
static char s_rtc[24] = "?";
static bool s_netStarted, s_qr;
static uint32_t s_screenUntil;

// NFC (demo 04).
static m5::unit::UnitUnified s_units;
static m5::unit::UnitNFC s_nfcUnit{NFC_ADDR};
static m5::nfc::NFCLayerA s_nfc{s_nfcUnit};
static bool s_nfcOn = true;
static std::string s_nfcUid, s_nfcType, s_nfcText;
static uint32_t s_nfcSeen;
static int s_nfcCount;

// Infrared (demo 06).
static IRrecv s_irRx(IR_RX_PIN, 1024, 50, true);
static IRsend s_irTx(IR_TX_PIN);
static decode_results s_irRes;
static decode_type_t s_irType = UNKNOWN;
static uint64_t s_irValue;
static uint16_t s_irBits;
static int s_irCount;
static char s_irText[48] = "";

void labCommand(const char* line) {
    CommandLine c;
    strlcpy(c.text, line, sizeof(c.text));
    xQueueSend(s_commands, &c, 0);               // full: dropped (the page sends again)
}

size_t labStateJson(char* out, size_t size) {
    portENTER_CRITICAL(&s_jsonMux);
    const size_t n = strlcpy(out, s_json, size);
    portEXIT_CRITICAL(&s_jsonMux);
    return min(n, size - 1);
}

// ---------------------------------------------------------------- sensors
// LTR-553 light and proximity (as demo 13).
static bool ltrInit() {
    bool ok = M5.In_I2C.writeRegister8(LTR553_ADDR, 0x80, 0x01, 400000);   // ALS_CONTR: active, gain 1x
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x81, 0x03, 400000);             // PS_CONTR: active
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x82, 0x7F, 400000);             // PS_LED: 60 kHz, 100 % duty, 100 mA
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x84, 0x02, 400000);             // PS_MEAS_RATE: 100 ms
    return ok;
}

static void ltrRead() {
    uint8_t b[4];
    if (M5.In_I2C.readRegister(LTR553_ADDR, 0x88, b, 4, 400000)) {         // CH1 low/high, CH0 low/high
        const float ch1 = b[0] | b[1] << 8, ch0 = b[2] | b[3] << 8;
        const float ratio = ch0 + ch1 > 0 ? ch1 / (ch0 + ch1) : 0;
        if (ratio < 0.45f) s_lux = 1.7743f * ch0 + 1.1059f * ch1;          // the datasheet's formula, gain 1x, 100 ms
        else if (ratio < 0.64f) s_lux = 4.2785f * ch0 - 1.9548f * ch1;
        else if (ratio < 0.85f) s_lux = 0.5926f * ch0 + 0.1185f * ch1;
        else s_lux = 0;
    }
    if (M5.In_I2C.readRegister(LTR553_ADDR, 0x8D, b, 2, 400000)) s_prox = (b[0] | b[1] << 8) & 0x7FF;
}

// Si12T capacitive touch (datasheet: docs/hardware/datasheets). The panel uses channels
// 1-3. Out of reset only channel 1 senses, and the chip sleeps between readings.
static bool touchInit() {
    bool ok = M5.In_I2C.writeRegister8(SI12T_ADDR, 0x09, 0x03, 400000);    // CTRL: no sleep (bits 1-0 must be 1)
    M5.In_I2C.writeRegister8(SI12T_ADDR, 0x0A, 0xF8, 400000);              // Ref_rst1: channels 1-3 not held in reset
    M5.In_I2C.writeRegister8(SI12T_ADDR, 0x0C, 0xF8, 400000);              // Ch_on1: channels 1-3 sensing (0 = on)
    return ok;
}

static void touchRead() {
    uint8_t out;
    if (!M5.In_I2C.readRegister(SI12T_ADDR, 0x10, &out, 1, 400000)) return;   // Output1: 2 bits per channel
    s_zonesRaw = out;
    for (int i = 0; i < 3; i++) s_zones[i] = (out >> (i * 2)) & 0x03;
}

static void readSensors(bool slow) {
    I2CGuard lock;
    if (M5.Imu.update()) {
        M5.Imu.getAccel(&s_ax, &s_ay, &s_az);
        M5.Imu.getGyro(&s_gx, &s_gy, &s_gz);
    }
    if (s_okLight) ltrRead();
    if (s_okTouch) touchRead();
    if (!slow) return;
    s_battPct = M5.Power.getBatteryLevel();
    s_battV = M5.Power.getBatteryVoltage() / 1000.0f;
    s_charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
    float busV, mA;
    s_battMa = readBatteryCurrent(&busV, &mA) ? mA : NAN;
    const auto t = M5.Rtc.getDateTime();
    snprintf(s_rtc, sizeof(s_rtc), "%04d-%02d-%02d %02d:%02d:%02d", t.date.year, t.date.month, t.date.date,
             t.time.hours, t.time.minutes, t.time.seconds);
}

// A new tag: its UID, type and the first text or URL record (demo 04 has the rest).
static void nfcPoll() {
    using namespace m5::nfc;
    using namespace m5::nfc::a;
    using namespace m5::nfc::ndef;
    I2CGuard lock;
    PICC picc{};
    if (!s_nfc.detect(picc)) return;
    const auto uid = picc.uidAsString();
    const bool same = uid == s_nfcUid && millis() - s_nfcSeen < SAME_TAG_MS;
    s_nfcSeen = millis();
    if (same) { s_nfc.deactivate(); return; }
    s_nfcUid = uid;
    s_nfcType = picc.typeAsString();
    s_nfcText.clear();
    s_nfcCount++;
    if (s_nfc.identify(picc) && s_nfc.reactivate(picc)) {
        s_nfcType = picc.typeAsString();
        bool valid = false;
        TLV msg{};
        if (picc.supportsNDEF() && !picc.isMifareClassic() && s_nfc.ndefIsValidFormat(valid) && valid && s_nfc.ndefRead(msg)) {
            for (auto&& r : msg.records()) {
                if (r.tnf() == TNF::Wellknown || r.tnf() == TNF::URI) { s_nfcText = r.payloadAsString(); break; }
            }
        }
    }
    s_nfc.deactivate();
    audioTone(3000, 40);
    Serial.printf("[lab] NFC %s %s %s\n", s_nfcUid.c_str(), s_nfcType.c_str(), s_nfcText.c_str());
}

static void irPoll() {
    if (!s_irRx.decode(&s_irRes)) return;
    if (!s_irRes.repeat && s_irRes.decode_type != UNKNOWN) {
        s_irType = s_irRes.decode_type;
        s_irValue = s_irRes.value;
        s_irBits = s_irRes.bits;
        s_irCount++;
        snprintf(s_irText, sizeof(s_irText), "%s 0x%s (%u bits)", typeToString(s_irType).c_str(),
                 uint64ToString(s_irValue, 16).c_str(), s_irBits);
        Serial.printf("[lab] IR %s\n", s_irText);
    }
    s_irRx.resume();
}

// The last code received, back out of the emitter. Air conditioner codes aren't one number: not those.
static void irSend() {
    if (s_irType == UNKNOWN || hasACState(s_irType)) { Serial.println("[lab] IR: nothing to send"); return; }
    s_irRx.disableIRIn();                       // don't receive our own LED
    s_irTx.send(s_irType, s_irValue, s_irBits);
    s_irRx.enableIRIn();
    Serial.printf("[lab] IR sent %s\n", s_irText);
}

// ---------------------------------------------------------------- commands
// "word [numbers or on/off]". The list is in the page (index.html) and in README.md.
static void runCommand(char* line) {
    char* arg = strchr(line, ' ');
    if (arg) *arg++ = 0;
    else arg = line + strlen(line);
    int a = 0, b = 0, c = 0;
    const int n = sscanf(arg, "%d %d %d", &a, &b, &c);
    const bool on = !strcmp(arg, "on");
    if (!strcmp(line, "head") && n == 2) servoSetTarget(a * DEG_TO_RAD, b * DEG_TO_RAD);
    else if (!strcmp(line, "center")) servoSetTarget(0, 0);
    else if (!strcmp(line, "torque")) servoSetTorque(on);
    else if (!strcmp(line, "leds") && n == 3) setBaseLeds(constrain(a, 0, 255), constrain(b, 0, 255), constrain(c, 0, 255));
    else if (!strcmp(line, "screen") && n == 3) {
        s_screenUntil = millis() + SCREEN_TEST_MS;
        lcd.fillScreen(lcd.color565(constrain(a, 0, 255), constrain(b, 0, 255), constrain(c, 0, 255)));
    }
    else if (!strcmp(line, "brightness") && n == 1) lcd.setBrightness(constrain(a, 0, 255));
    else if (!strcmp(line, "tone") && n == 2) audioTone(constrain(a, 100, 8000), constrain(b, 10, 2000));
    else if (!strcmp(line, "volume") && n == 1) audioVolume(constrain(a, 0, 255));
    else if (!strcmp(line, "mic")) audioMicLive(on);
    else if (!strcmp(line, "record")) audioRecord();
    else if (!strcmp(line, "play")) audioPlay();
    else if (!strcmp(line, "ir")) irSend();
    else if (!strcmp(line, "nfc")) s_nfcOn = on;
    else if (!strcmp(line, "ota")) otaEnable();
    else { Serial.printf("[lab] unknown command: %s %s\n", line, arg); return; }
    Serial.printf("[lab] %s %s\n", line, arg);
}

// ---------------------------------------------------------------- state as JSON
static void jsonText(char* out, size_t size, const char* s) {
    size_t j = 0;
    for (; *s && j + 7 < size; s++) {
        const unsigned char ch = *s;
        if (ch == '"' || ch == '\\') { out[j++] = '\\'; out[j++] = ch; }
        else if (ch < 0x20) j += snprintf(out + j, size - j, "\\u%04x", ch);
        else out[j++] = ch;
    }
    out[j] = 0;
}

static void buildJson() {
    static char buf[sizeof(s_json)];
    char nfcText[160], nfcType[64];
    jsonText(nfcText, sizeof(nfcText), s_nfcText.c_str());
    jsonText(nfcType, sizeof(nfcType), s_nfcType.c_str());
    float pan, tilt;
    servoGetCurrent(&pan, &tilt);
    const AudioState au = audioState();
    char ma[16] = "null";
    if (!isnan(s_battMa)) snprintf(ma, sizeof(ma), "%.0f", s_battMa);
    const int n = snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"test\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"up\":%lu,\"heap\":%lu,\"psram\":%lu,\"temp\":%.1f,\"rtc\":\"%s\","
        "\"ok\":{\"base\":%d,\"camera\":%d,\"light\":%d,\"touch\":%d,\"nfc\":%d,\"sd\":%d},\"sd_mb\":%lu,"
        "\"bat\":{\"pct\":%d,\"v\":%.2f,\"ma\":%s,\"charging\":%d},"
        "\"head\":{\"pan\":%.1f,\"tilt\":%.1f},"
        "\"imu\":{\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"gx\":%.0f,\"gy\":%.0f,\"gz\":%.0f},"
        "\"lux\":%.1f,\"prox\":%d,"
        "\"touch\":{\"x\":%d,\"y\":%d,\"taps\":%d,\"zones\":[%d,%d,%d],\"raw\":%d},"
        "\"nfc\":{\"on\":%d,\"count\":%d,\"uid\":\"%s\",\"type\":\"%s\",\"text\":\"%s\"},"
        "\"ir\":{\"count\":%d,\"code\":\"%s\"},"
        "\"audio\":{\"mic\":%d,\"level\":%d,\"peak\":%d,\"status\":\"%s\",\"volume\":%d,\"seconds\":%d}}",
        otaRobotName(), TEST_NAME, WiFi.localIP().toString().c_str(), WiFi.RSSI(), (unsigned long)(millis() / 1000),
        (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram(), temperatureRead(), s_rtc,
        s_okBase, s_okCamera, s_okLight, s_okTouch, s_okNfc, s_okSd, (unsigned long)s_sdMb,
        s_battPct, s_battV, ma, s_charging,
        pan * RAD_TO_DEG, tilt * RAD_TO_DEG,
        s_ax, s_ay, s_az, s_gx, s_gy, s_gz,
        s_lux, s_prox,
        s_tapX, s_tapY, s_taps, s_zones[0], s_zones[1], s_zones[2], s_zonesRaw,
        s_nfcOn, s_nfcCount, s_nfcUid.c_str(), nfcType, nfcText,
        s_irCount, s_irText,
        au.micOn, au.level, au.peak, au.status, au.volume, REC_SECONDS);
    if (n <= 0 || n >= (int)sizeof(buf)) return;
    portENTER_CRITICAL(&s_jsonMux);
    memcpy(s_json, buf, n + 1);
    portEXIT_CRITICAL(&s_jsonMux);
}

// ---------------------------------------------------------------- screen
static void check(int x, int y, const char* name, bool ok) {
    auto& c = s_canvas;
    const uint16_t bg = ok ? TFT_DARKGREEN : TFT_MAROON;
    c.fillRoundRect(x, y, 50, 20, 4, bg);
    c.setTextColor(TFT_WHITE, bg);
    c.setTextDatum(middle_center);
    c.drawString(name, x + 25, y + 10);
}

static void draw() {
    auto& c = s_canvas;
    c.fillSprite(TFT_BLACK);
    c.setFont(&fonts::efontJA_16);
    c.setTextDatum(top_left);
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    c.drawString("Hardware lab", 6, 4);
    c.setTextColor(TFT_DARKGREY, TFT_BLACK);
    c.drawString("OTA off", 250, 4);
    if (WiFi.status() == WL_CONNECTED) {
        c.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
        c.drawString(String("http://") + WiFi.localIP().toString(), 6, 24);
        c.drawString(String("http://") + otaRobotName() + ".local", 6, 42);
    } else {
        c.setTextColor(TFT_ORANGE, TFT_BLACK);
        c.drawString(String("Wi-Fi: ") + WIFI_SSID + "...", 6, 24);
    }
    c.setFont(&fonts::efontJA_12);
    const char* names[] = { "base", "camera", "light", "touch", "NFC", "SD" };
    const bool oks[] = { s_okBase, s_okCamera, s_okLight, s_okTouch, s_okNfc, s_okSd };
    for (int i = 0; i < 6; i++) check(6 + i * 52, 64, names[i], oks[i]);

    c.setFont(&fonts::efontJA_16);
    c.setTextDatum(top_left);
    c.setTextColor(TFT_WHITE, TFT_BLACK);
    float pan, tilt;
    servoGetCurrent(&pan, &tilt);
    c.setCursor(6, 92);
    c.printf("Battery %d%%  %.2f V%s\n", s_battPct, s_battV, s_charging ? "  charging" : "");
    c.setCursor(6, c.getCursorY());
    c.printf("Head pan %+.0f  tilt %+.0f\n", pan * RAD_TO_DEG, tilt * RAD_TO_DEG);
    c.setCursor(6, c.getCursorY());
    c.printf("Light %.0f lx  proximity %d\n", s_lux, s_prox);
    c.setCursor(6, c.getCursorY());
    c.printf("NFC %s\n", s_nfcUid.empty() ? (s_nfcOn ? "bring a tag close" : "off") : s_nfcUid.c_str());
    c.setCursor(6, c.getCursorY());
    c.printf("IR  %s\n", s_irText[0] ? s_irText : "point a remote at the base");

    // Head touch zones and the mic level.
    const int y = 186;
    c.drawString("Zones", 6, y);
    for (int i = 0; i < 3; i++) {
        static const uint16_t LEVEL[] = { TFT_DARKGREY, TFT_YELLOW, TFT_ORANGE, TFT_RED };
        c.fillRoundRect(64 + i * 30, y, 24, 16, 3, LEVEL[s_zones[i]]);
    }
    const AudioState au = audioState();
    c.drawString("Mic", 170, y);
    c.drawRect(204, y + 2, 110, 12, TFT_DARKGREY);
    if (au.micOn) c.fillRect(205, y + 3, 108 * au.level / 100, 10, au.level > 85 ? TFT_RED : TFT_GREEN);
    c.setFont(&fonts::efontJA_12);
    c.setTextColor(TFT_DARKGREY, TFT_BLACK);
    c.setTextDatum(bottom_center);
    c.drawString("tap: QR code  -  hold 3 s: OTA menu", 160, 238);
    c.pushSprite(0, 0);
}

static void showQr() {
    lcd.fillScreen(TFT_WHITE);
    lcd.qrcode((String("http://") + WiFi.localIP().toString()).c_str(), 70, 10, 180, 3);
    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextColor(TFT_BLACK, TFT_WHITE);
    lcd.setTextDatum(bottom_center);
    lcd.drawString("tap to go back", 160, 232);
}

// ---------------------------------------------------------------- test
void testSetup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // servo current peaks dip the supply: don't reset on them (demo 13)
    s_commands = xQueueCreate(8, sizeof(CommandLine));
    strlcpy(s_json, "{}", sizeof(s_json));
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(middle_center);
    lcd.drawString("Hardware lab: starting...", 160, 120);

    s_okBase = M5.In_I2C.readRegister8(0x6F, 0x02, 400000) != 0;   // the base's PY32 version (0 = not answering)
    servoInit();
    servoStartTask();
    {
        I2CGuard lock;
        s_okCamera = cameraInit();
        s_okLight = ltrInit();
        s_okTouch = touchInit();
        s_okNfc = s_units.add(s_nfcUnit, M5.In_I2C) && s_units.begin();
    }
    s_irRx.enableIRIn();
    s_irTx.begin();
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    s_okSd = SD.begin(SD_CS, SPI, 25000000);
    if (s_okSd) s_sdMb = SD.cardSize() >> 20;
    audioStart();
    Serial.printf("[lab] base %d camera %d light %d touch %d nfc %d sd %d (%lu MB)\n", s_okBase, s_okCamera, s_okLight,
                  s_okTouch, s_okNfc, s_okSd, (unsigned long)s_sdMb);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(otaRobotName());
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    s_canvas.setPsram(true);
    s_canvas.setColorDepth(16);
    s_canvas.createSprite(lcd.width(), lcd.height());
    readSensors(true);
    draw();
    audioTone(2000, 80);
}

void testLoop() {
    const uint32_t now = millis();
    static uint32_t lastSensor = 0, lastSlow = 0, lastNfc = 0, lastDraw = 0;

    if (!s_netStarted && WiFi.status() == WL_CONNECTED) {
        s_netStarted = true;
        MDNS.begin(otaRobotName());
        MDNS.addService("http", "tcp", 80);
        webStart();
        Serial.printf("[lab] open http://%s.local or http://%s\n", otaRobotName(), WiFi.localIP().toString().c_str());
    }

    CommandLine c;
    while (xQueueReceive(s_commands, &c, 0) == pdTRUE) runCommand(c.text);
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        // A terminal with echo on sends our own "[lab] ..." lines back: not commands.
        if (line.length() && line[0] != '[') labCommand(line.c_str());
    }

    const auto d = M5.Touch.getDetail();
    if (d.isPressed()) { s_tapX = d.x; s_tapY = d.y; }
    if (d.wasClicked()) {
        s_taps++;
        s_qr = !s_qr && s_netStarted;
        if (s_qr) showQr();
        else draw();
    }

    irPoll();
    if (now - lastSensor >= SENSOR_MS) {
        lastSensor = now;
        const bool slow = now - lastSlow >= SLOW_MS;
        if (slow) lastSlow = now;
        readSensors(slow);
        buildJson();
    }
    if (s_okNfc && s_nfcOn && now - lastNfc >= NFC_MS) {
        lastNfc = now;
        nfcPoll();
    }
    if (s_screenUntil && (int32_t)(now - s_screenUntil) < 0) return;
    s_screenUntil = 0;
    if (!s_qr && now - lastDraw >= DRAW_MS) {
        lastDraw = now;
        draw();
    }
    delay(2);
}

void testStop() {
    setBaseLeds(0, 0, 0);
    audioMicLive(false);
}

void testRedraw() {
    s_qr = false;
    draw();
}
