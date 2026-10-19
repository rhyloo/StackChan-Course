// Demo 02 - Voz con Gemini. The demo 01 firmware (camera to the phone, head
// pose from the phone) plus microphone and speaker over UDP: see audio.cpp.
// Hands-free by default: just talk. Tap the screen to mute/unmute the mic.
// The hardware and the UDP link live in lib/ (StackChanHW, StackChanLink).
#include <M5Unified.h>
#include <WiFi.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "link_config.h"
#include "board.h"
#include "camera.h"
#include "link.h"
#include "audio.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Falta config/wifi_secrets.h: ejecuta ./setup.sh --wifi (o setup.bat -Wifi en Windows)"
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
        status.printf("WiFi: conectando a %s...", WIFI_SSID);
    } else {
        status.printf("%s  stackchan-%s", WiFi.localIP().toString().c_str(), robotId());
    }
    status.setCursor(4, 16);
    if (!s_cameraOk) {
        status.setTextColor(TFT_RED);
        status.print("ERROR: camara no detectada");
    } else if (st.relayOnline) {
        status.setTextColor(TFT_GREEN);
        status.printf("Portatil %s  %.1f fps  %u B", st.relayIp, st.fps, (unsigned)st.jpegBytes);
    } else {
        status.setTextColor(TFT_YELLOW);
        status.print("Buscando el portatil (relay)...");
    }
    status.setTextColor(TFT_LIGHTGREY);
    status.setCursor(4, 30);
    if (audioMicOn()) status.setTextColor(TFT_RED);
    else if (audioSpeaking()) status.setTextColor(TFT_CYAN);
    const char* state = audioMuted() ? "micro silenciado (toca para activar)"
                      : audioSpeaking() ? "hablando"
                      : audioMicOn() ? "te escucho"
                      : audioHandsFree() ? "..." : "toca para hablar";
    status.printf("Bat %d%%  %s  %s", (int)M5.Power.getBatteryLevel(), state, st.lastText);
    status.pushSprite(0, 240 - 44);
}

void setup() {
    // Servo current peaks cause brief voltage dips; don't reset on them.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(30);   // más brillo calienta la pantalla y gasta batería
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

void loop() {
    i2cLock();   // touch, PMIC, LEDs, NFC: the audio task also uses this bus
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
                audioStart();
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
    if (now - lastUi >= 500) {
        lastUi = now;
        drawStatus();
    }
    // Tap the screen: mute/unmute (hands-free) or talk/stop (push-to-talk).
    if (s_linkStarted && M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) audioTap();

    // Mouth: open while listening or speaking.
    static int lastMouth = -1;
    int mouth = audioSpeaking() ? 2 : (audioMicOn() ? 1 : 0);
    if (mouth != lastMouth) {
        lastMouth = mouth;
        M5.Display.fillRect(110, 125, 100, 40, TFT_BLACK);
        if (mouth == 0) M5.Display.fillRect(135, 140, 50, 6, TFT_WHITE);
        else if (mouth == 1) M5.Display.drawCircle(160, 143, 10, TFT_GREEN);   // listening
        else M5.Display.fillEllipse(160, 143, 24, 12, TFT_WHITE);              // speaking
    }

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
        }
    }
    i2cUnlock();
    delay(10);
}
