// Demo 13 - Stack-chan as a web server. The robot serves its own page: open
// http://stackchan-XXXX.local (or its IP) on a phone or laptop on the same Wi-Fi and you get
// the camera, the head (all the way round), the face, the LEDs, the speaker and every
// sensor live. No relay, no app. Optional MQTT, with Home Assistant discovery (mqtt.cpp).
//
//   web.cpp    HTTP on port 80 (page, WebSocket /ws, /photo.jpg) and MJPEG on port 81
//   mqtt.cpp   the broker set from the page, state every 2 s, commands on <base>/cmd
//   face.cpp   the faces of demo 02.1
//   web/index.html  the page itself
//
// loop() owns I2C and the screen: it reads the sensors and runs the commands that the
// web and MQTT tasks leave in a queue.
#include <M5Unified.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "board.h"
#include "camera.h"
#include "camera_extra.h"
#include "face.h"
#include "mqtt.h"
#include "shared.h"
#include "web.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static constexpr uint8_t LTR553_ADDR = 0x23;   // light and proximity sensor, above the camera
static constexpr uint32_t SENSOR_MS = 100, BROADCAST_MS = 200, MQTT_MS = 2000, QR_MS = 20000;

static State s_state;
static portMUX_TYPE s_stateMux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_commands;
struct CommandMsg { char json[256]; };
static M5Canvas status(&M5.Display);
static char s_name[20], s_url[48];
static bool s_netStarted = false;
static uint32_t s_qrUntil = 0;                 // showing the QR code until then
static int s_mood = 0;

const char* robotName() { return s_name; }

State stateGet() {
    portENTER_CRITICAL(&s_stateMux);
    State s = s_state;
    portEXIT_CRITICAL(&s_stateMux);
    return s;
}

size_t stateToJson(char* json, size_t size) {
    State s = stateGet();
    JsonDocument d;
    d["name"] = s_name;
    d["batt"] = serialized(String(s.battV, 2));
    d["pct"] = s.battPct;
    if (!isnan(s.currentMa)) d["ma"] = (int)s.currentMa;
    d["pan"] = serialized(String(s.pan, 1));
    d["tilt"] = serialized(String(s.tilt, 1));
    JsonArray acc = d["acc"].to<JsonArray>();
    acc.add(serialized(String(s.ax, 2)));
    acc.add(serialized(String(s.ay, 2)));
    acc.add(serialized(String(s.az, 2)));
    d["pitch"] = serialized(String(s.pitch, 1));
    d["roll"] = serialized(String(s.roll, 1));
    d["gyro"] = (int)s.gyro;
    d["lux"] = serialized(String(s.lux, 1));
    d["prox"] = s.prox;
    d["touch"] = s.touches;
    d["rssi"] = s.rssi;
    d["up"] = s.uptime;
    d["heap"] = s.heap;
    d["temp"] = serialized(String(s.chipTemp, 1));
    d["mood"] = s.mood;
    d["night"] = cameraIsNight();
    d["text"] = s.text;
    d["ws"] = webClients();
    d["mqtt"] = mqttStatus();
    d["broker"] = mqttBroker();
    return serializeJson(d, json, size);
}

void commandSubmit(const char* json, size_t len) {
    CommandMsg m;
    len = min(len, sizeof(m.json) - 1);
    memcpy(m.json, json, len);
    m.json[len] = 0;
    xQueueSend(s_commands, &m, 0);   // full queue: drop it (the page sends the head again anyway)
}

// --- LTR-553 light and proximity (register map in docs/hardware/datasheets) ---
static bool ltrInit() {
    I2CGuard lock;
    bool ok = M5.In_I2C.writeRegister8(LTR553_ADDR, 0x80, 0x01, 400000);   // ALS_CONTR: active, gain 1x
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x81, 0x03, 400000);             // PS_CONTR: active
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x82, 0x7F, 400000);             // PS_LED: 60 kHz, 100 % duty, 100 mA
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x84, 0x02, 400000);             // PS_MEAS_RATE: 100 ms
    return ok;
}

static void ltrRead(float* lux, int* prox) {
    uint8_t b[4];
    if (M5.In_I2C.readRegister(LTR553_ADDR, 0x88, b, 4, 400000)) {   // CH1 low/high, CH0 low/high
        float ch1 = b[0] | b[1] << 8, ch0 = b[2] | b[3] << 8;
        float ratio = ch0 + ch1 > 0 ? ch1 / (ch0 + ch1) : 0;
        // The datasheet's formula for gain 1x and 100 ms integration.
        if (ratio < 0.45f) *lux = 1.7743f * ch0 + 1.1059f * ch1;
        else if (ratio < 0.64f) *lux = 4.2785f * ch0 - 1.9548f * ch1;
        else if (ratio < 0.85f) *lux = 0.5926f * ch0 + 0.1185f * ch1;
        else *lux = 0;
    }
    if (M5.In_I2C.readRegister(LTR553_ADDR, 0x8D, b, 2, 400000)) *prox = (b[0] | b[1] << 8) & 0x7FF;
}

static void readSensors() {
    I2CGuard lock;
    State s = stateGet();
    s.battV = M5.Power.getBatteryVoltage() / 1000.0f;
    s.battPct = M5.Power.getBatteryLevel();
    float busV, mA;
    s.currentMa = readBatteryCurrent(&busV, &mA) ? mA : NAN;
    float pan, tilt;
    servoGetCurrent(&pan, &tilt);
    s.pan = pan * RAD_TO_DEG;
    s.tilt = tilt * RAD_TO_DEG;
    if (M5.Imu.update()) {
        float gx, gy, gz;
        M5.Imu.getAccel(&s.ax, &s.ay, &s.az);
        M5.Imu.getGyro(&gx, &gy, &gz);
        // The CoreS3 stands up in the Stack-chan: with the head level, gravity is along +y.
        // pitch: + = the head looks up (checked: tilt 0 -> 45 moved it 46 deg); roll: sideways.
        s.pitch = atan2f(s.az, s.ay) * RAD_TO_DEG;
        s.roll = atan2f(-s.ax, s.ay) * RAD_TO_DEG;
        s.gyro = sqrtf(gx * gx + gy * gy + gz * gz);
    }
    ltrRead(&s.lux, &s.prox);
    s.rssi = WiFi.RSSI();
    s.uptime = millis() / 1000;
    s.heap = ESP.getFreeHeap();
    s.chipTemp = temperatureRead();
    portENTER_CRITICAL(&s_stateMux);
    s_state = s;
    portEXIT_CRITICAL(&s_stateMux);
}

static void setText(const char* text) {
    portENTER_CRITICAL(&s_stateMux);
    strncpy(s_state.text, text, sizeof(s_state.text) - 1);
    s_state.text[sizeof(s_state.text) - 1] = 0;
    portEXIT_CRITICAL(&s_stateMux);
}

static void drawFaceAndText() {
    faceDraw(s_mood);
    State s = stateGet();
    if (s.text[0]) {
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setTextDatum(top_center);
        M5.Display.setTextSize(2);
        M5.Display.drawString(s.text, 160, 10);
        M5.Display.setTextSize(1);
        M5.Display.setTextDatum(top_left);
    }
}

// Commands from the page and MQTT (JSON). See README.md.
static void runCommand(const char* json) {
    JsonDocument d;
    if (deserializeJson(d, json)) return;
    const char* cmd = d["cmd"] | "";
    if (!strcmp(cmd, "head")) {
        State s = stateGet();
        float pan = d["pan"] | s.pan, tilt = d["tilt"] | s.tilt;   // a missing angle stays where it is
        servoSetTarget(pan * DEG_TO_RAD, tilt * DEG_TO_RAD);
    } else if (!strcmp(cmd, "center")) {
        servoSetTarget(0, 0);
    } else if (!strcmp(cmd, "mood")) {
        int m = moodFromName(d["mood"] | "");
        if (m < 0) return;
        s_mood = m;
        portENTER_CRITICAL(&s_stateMux);
        strncpy(s_state.mood, MOOD_NAMES[m], sizeof(s_state.mood) - 1);
        portEXIT_CRITICAL(&s_stateMux);
        if (millis() >= s_qrUntil) drawFaceAndText();
    } else if (!strcmp(cmd, "leds")) {
        setBaseLeds(constrain(d["r"] | 0, 0, 255), constrain(d["g"] | 0, 0, 255), constrain(d["b"] | 0, 0, 255));
    } else if (!strcmp(cmd, "text")) {
        setText(d["text"] | "");
        if (millis() >= s_qrUntil) drawFaceAndText();
    } else if (!strcmp(cmd, "beep")) {
        M5.Speaker.tone(constrain(d["hz"] | 1000, 100, 8000), constrain(d["ms"] | 100, 10, 2000));
    } else if (!strcmp(cmd, "camera")) {
        cameraNight(d["night"] | false);
    } else if (!strcmp(cmd, "mqtt")) {
        mqttConfigure(d["host"] | "", d["port"] | 1883);
    }
}

static void drawStatus() {
    State s = stateGet();
    status.fillSprite(TFT_BLACK);
    status.setCursor(4, 2);
    if (WiFi.status() != WL_CONNECTED) {
        status.setTextColor(TFT_ORANGE);
        status.printf("WiFi: connecting to %s...", WIFI_SSID);
    } else {
        status.setTextColor(TFT_GREEN);
        status.printf("%s  (tap: QR)", s_url);
    }
    status.setTextColor(TFT_LIGHTGREY);
    status.setCursor(4, 16);
    status.printf("%s  web clients %d  MQTT %s", WiFi.localIP().toString().c_str(), webClients(), mqttStatus());
    status.setCursor(4, 30);
    status.printf("Bat %d%%  pan %+.0f  tilt %+.0f  light %.0f lx", s.battPct, s.pan, s.tilt, s.lux);
    status.pushSprite(0, 240 - 44);
}

void setup() {
    // Servo current peaks cause brief voltage dips; don't reset on them.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(30);   // brighter heats the screen and drains the battery
    faceDraw(0);
    status.setColorDepth(16);
    status.createSprite(320, 44);
    status.setTextSize(1);
    M5.Speaker.begin();
    M5.Speaker.setVolume(120);

    s_commands = xQueueCreate(8, sizeof(CommandMsg));
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_name, sizeof(s_name), "stackchan-%02X%02X", mac[4], mac[5]);
    snprintf(s_url, sizeof(s_url), "http://%s.local", s_name);

    bool base = boardInit();
    servoInit();
    setBaseLeds(0, 0, 255);
    bool camera = cameraInit();
    bool ltr = ltrInit();
    Serial.printf("\n[Demo 13 web server] %s  base:%s camera:%s light:%s\n", s_name, base ? "ok" : "NO",
                  camera ? "ok" : "ERROR", ltr ? "ok" : "NO");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(s_name);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    servoStartTask();
    M5.Speaker.tone(2000, 80);
}

void loop() {
    i2cLock();   // touch, PMIC, IMU, LEDs: the web task also touches this bus (the camera's registers)
    M5.update();
    static uint32_t lastSensor = 0, lastBroadcast = 0, lastMqtt = 0, lastUi = 0;
    static wl_status_t lastWifi = WL_IDLE_STATUS;
    const uint32_t now = millis();

    wl_status_t w = WiFi.status();
    if (w != lastWifi) {
        lastWifi = w;
        if (w == WL_CONNECTED) {
            if (!s_netStarted) {
                MDNS.begin(s_name);
                MDNS.addService("http", "tcp", 80);
                webStart();
                mqttStart();
                s_netStarted = true;
            }
            setBaseLeds(0, 255, 0);
            Serial.printf("[WIFI] connected. Open %s or http://%s on the same Wi-Fi\n", s_url,
                          WiFi.localIP().toString().c_str());
        } else {
            setBaseLeds(255, 128, 0);
        }
    }

    CommandMsg m;
    while (xQueueReceive(s_commands, &m, 0) == pdTRUE) runCommand(m.json);

    if (now - lastSensor >= SENSOR_MS) {
        lastSensor = now;
        readSensors();
    }
    if (s_netStarted && now - lastBroadcast >= BROADCAST_MS) {
        lastBroadcast = now;
        static char json[640];
        size_t n = stateToJson(json, sizeof(json));
        webBroadcast(json, n);
        if (now - lastMqtt >= MQTT_MS) {
            lastMqtt = now;
            mqttPublishState(json, n);
        }
    }

    // Tap the screen: a QR code with the page's address, for the phone's camera.
    if (M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) {
        portENTER_CRITICAL(&s_stateMux);
        s_state.touches++;
        portEXIT_CRITICAL(&s_stateMux);
        if (s_netStarted && now >= s_qrUntil) {
            s_qrUntil = now + QR_MS;
            M5.Display.fillRect(0, 0, 320, 196, TFT_WHITE);
            M5.Display.qrcode(("http://" + WiFi.localIP().toString()).c_str(), 70, 8, 180, 3);
        } else {
            s_qrUntil = 0;
            drawFaceAndText();
        }
    }
    if (s_qrUntil && now >= s_qrUntil) {
        s_qrUntil = 0;
        drawFaceAndText();
    }
    if (now - lastUi >= 500) {
        lastUi = now;
        drawStatus();
    }

    if (Serial.available()) {   // the same JSON commands, typed on the serial monitor
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.startsWith("{")) commandSubmit(line.c_str(), line.length());
    }
    i2cUnlock();
    delay(5);
}
