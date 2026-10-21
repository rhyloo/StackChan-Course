// Stack-chan VR firmware: camera -> UDP -> laptop relay -> phone (Cardboard),
// and head pose from the phone -> relay -> servos. See ../PROTOCOL.md.
// The hardware and the UDP link live in lib/ (StackChanHW, StackChanLink).
#include <M5Unified.h>
#include <WiFi.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "link_config.h"
#include "board.h"
#include "camera.h"
#include "link.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static M5Canvas status(&M5.Display);
static bool s_cameraOk = false;
static bool s_linkStarted = false;

static void drawFace() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillCircle(100, 90, 14, TFT_WHITE);
    M5.Display.fillCircle(220, 90, 14, TFT_WHITE);
    M5.Display.fillRect(135, 140, 50, 6, TFT_WHITE);
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
    if (!s_cameraOk) {
        status.setTextColor(TFT_RED);
        status.print("ERROR: camera not found");
    } else if (st.relayOnline) {
        status.setTextColor(TFT_GREEN);
        status.printf("Laptop %s  %.1f fps  %u B", st.relayIp, st.fps, (unsigned)st.jpegBytes);
    } else {
        status.setTextColor(TFT_YELLOW);
        status.print("Looking for the laptop (relay)...");
    }
    status.setTextColor(TFT_LIGHTGREY);
    status.setCursor(4, 30);
    status.printf("Bat %d%%  %s  %s", (int)M5.Power.getBatteryLevel(), linkVideoWanted() ? "video ON" : "video paused", st.lastText);
    status.pushSprite(0, 240 - 44);
}

void setup() {
    // Servo current peaks cause brief voltage dips; don't reset on them.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(30);   // more brightness heats the screen and drains the battery
    drawFace();
    status.setColorDepth(16);
    status.createSprite(320, 44);
    status.setTextSize(1);

    M5.Speaker.begin();
    M5.Speaker.setVolume(120);

    bool base = boardInit();
    servoInit();
    setBaseLeds(0, 0, 40);
    s_cameraOk = cameraInit();
    Serial.printf("\n[Stack-chan VR " FW_VERSION "] base:%s camera:%s\n", base ? "ok" : "NO", s_cameraOk ? "ok" : "ERROR");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("stackchan");
    WiFi.setSleep(false);              // modem sleep adds 100+ ms of jitter to every packet
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    servoStartTask();
    M5.Speaker.tone(2000, 80);
}

// "probe" on the serial port: a guided calibration. Torque off, the screen says how to
// move the head by hand and every reading goes to the serial tagged with its step.
struct ProbeStep { const char* text; uint32_t ms; };
static const ProbeStep PROBE_STEPS[] = {
    { "Hold the head LEVEL,\nlooking straight ahead", 6000 },
    { "Tilt the head UP\nas far as it goes", 6000 },
    { "Tilt the head DOWN\nas far as it goes", 6000 },
    { "Head LEVEL again,\nfacing the FRONT", 6000 },
    { "Turn the head SLOWLY\nto the RIGHT:\none full turn", 25000 },
    { "Turn it SLOWLY back\nto the LEFT:\none full turn", 25000 },
};
static constexpr int PROBE_COUNT = sizeof(PROBE_STEPS) / sizeof(PROBE_STEPS[0]);
static int s_probeStep = -1;          // -1 = not probing
static uint32_t s_probeStepMs = 0;

static void drawProbeStep(int step, int secondsLeft) {
    M5.Display.fillScreen(TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.printf("Step %d/%d   %d s\n\n", step + 1, PROBE_COUNT, secondsLeft);
    M5.Display.setTextColor(TFT_YELLOW, TFT_NAVY);
    M5.Display.print(PROBE_STEPS[step].text);
    M5.Display.setTextSize(1);
}

static void probeLoop(uint32_t now) {
    static uint32_t lastRead = 0, lastDraw = 0;
    const ProbeStep& st = PROBE_STEPS[s_probeStep];
    if (now - s_probeStepMs >= st.ms) {
        if (++s_probeStep >= PROBE_COUNT) {
            s_probeStep = -1;
            servoSetProbe(false);
            Serial.println("[PROBE] done");
            drawFace();
            return;
        }
        s_probeStepMs = now;
        M5.Speaker.tone(1500, 80);
        lastDraw = 0;
    }
    if (now - lastDraw >= 1000) {
        lastDraw = now;
        drawProbeStep(s_probeStep, (PROBE_STEPS[s_probeStep].ms - (now - s_probeStepMs) + 999) / 1000);
    }
    if (now - lastRead >= 100) {
        lastRead = now;
        int p, t;
        servoProbeRead(&p, &t);
        Serial.printf("[PROBE] step=%d pan=%d tilt=%d\n", s_probeStep + 1, p, t);
    }
}

void loop() {
    M5.update();
    static uint32_t lastUi = 0, lastBatt = 0;
    static wl_status_t lastWifi = WL_IDLE_STATUS;
    uint32_t now = millis();

    wl_status_t w = WiFi.status();
    if (w != lastWifi) {
        lastWifi = w;
        if (w == WL_CONNECTED) {
            Serial.printf("[WIFI] connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            setBaseLeds(0, 40, 0);
            if (!s_linkStarted && s_cameraOk) {
                linkStart();
                s_linkStarted = true;
            }
        } else {
            setBaseLeds(40, 20, 0);
        }
    }

    if (now - lastBatt >= 2000) {
        lastBatt = now;
        float busV, mA;
        if (!readBatteryCurrent(&busV, &mA)) mA = NAN;
        linkSetBattery(M5.Power.getBatteryVoltage() / 1000.0f, M5.Power.getBatteryLevel(), mA);
    }

    PendingCommands pc;
    linkTakePending(&pc);
    if (pc.brightness >= 0) M5.Display.setBrightness(constrain(pc.brightness, 0, 255));
    if (pc.wifiSleep >= 0) WiFi.setSleep(pc.wifiSleep == 1);
    if (pc.leds[0] >= 0) setBaseLeds(pc.leds[0], pc.leds[1], pc.leds[2]);
    if (now - lastUi >= 500 && s_probeStep < 0) {
        lastUi = now;
        drawStatus();
    }
    // Tap the screen to center the head.
    if (M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) servoHome();

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line == "info") {
            LinkStats st;
            linkGetStats(&st);
            Serial.printf("ip=%s relay=%s online=%d fps=%.1f jpeg=%u poses=%u send_err=%u heap=%u psram=%u\n",
                WiFi.localIP().toString().c_str(), st.relayIp, st.relayOnline, st.fps, (unsigned)st.jpegBytes,
                (unsigned)st.poses, (unsigned)st.sendErrors, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
        } else if (line == "home") {
            servoHome();
        } else if (line.startsWith("wheel ")) {
            int pwm = 0, ms = 0;
            sscanf(line.c_str() + 6, "%d %d", &pwm, &ms);
            servoWheelTest(constrain(pwm, -1000, 1000), constrain(ms, 0, 5000));
        } else if (line == "probe" && s_probeStep < 0) {
            servoSetProbe(true);
            s_probeStep = 0;
            s_probeStepMs = now;
            Serial.println("[PROBE] start");
        }
    }
    if (s_probeStep >= 0) probeLoop(now);
    delay(10);
}
