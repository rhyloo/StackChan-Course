// Demo 02.1 - Head and moods by voice. Demo 02's microphone and speaker over UDP
// (audio.cpp, compiled in from demo 02), without the VR viewer: the relay moves the
// head with POSE packets and changes the face with "face <mood>".
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
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static M5Canvas status(&M5.Display);
static bool s_linkStarted = false;

// --- Faces ---
enum Mood { NEUTRAL, HAPPY, SAD, SURPRISED, ANGRY, SLEEPY, MOOD_COUNT };
static const char* const MOOD_NAMES[MOOD_COUNT] = { "neutral", "happy", "sad", "surprised", "angry", "sleepy" };
static volatile int s_wantMood = NEUTRAL;   // set by the network task; drawn by loop()
static int s_mood = -1;

static void drawEyes(int mood) {
    auto& d = M5.Display;
    d.fillRect(0, 40, 320, 80, TFT_BLACK);
    for (int side = 0; side < 2; side++) {
        const int x = side ? 220 : 100, y = 90;
        const int in = side ? -1 : 1;         // towards the middle of the face
        switch (mood) {
        case HAPPY:                            // closed, smiling eyes: ^ ^
            d.fillArc(x, y + 8, 10, 17, 180, 360, TFT_WHITE);
            break;
        case SAD:                              // brows up in the middle
            d.fillCircle(x, y + 6, 12, TFT_WHITE);
            d.drawWideLine(x - 20 * in, y - 14, x + 18 * in, y - 26, 3, TFT_WHITE);
            break;
        case SURPRISED:
            d.fillCircle(x, y, 19, TFT_WHITE);
            break;
        case ANGRY:                            // brows down in the middle
            d.fillCircle(x, y + 6, 12, TFT_WHITE);
            d.drawWideLine(x - 20 * in, y - 26, x + 18 * in, y - 12, 3, TFT_WHITE);
            break;
        case SLEEPY:
            d.fillRect(x - 16, y + 4, 32, 6, TFT_WHITE);
            break;
        default:
            d.fillCircle(x, y, 14, TFT_WHITE);
        }
    }
}

// mouth: 0 = the mood's own, 1 = listening, 2 = speaking
static void drawMouth(int mouth) {
    auto& d = M5.Display;
    d.fillRect(100, 115, 120, 60, TFT_BLACK);
    if (mouth == 1) { d.drawCircle(160, 143, 10, TFT_GREEN); return; }
    if (mouth == 2) { d.fillEllipse(160, 143, 24, 12, TFT_WHITE); return; }
    switch (s_mood) {
    case HAPPY:     d.fillArc(160, 128, 20, 27, 15, 165, TFT_WHITE); break;    // smile
    case SAD:       d.fillArc(160, 170, 20, 27, 195, 345, TFT_WHITE); break;   // frown
    case SURPRISED: d.fillCircle(160, 145, 14, TFT_WHITE); d.fillCircle(160, 145, 8, TFT_BLACK); break;
    case ANGRY:     d.fillRect(140, 146, 40, 6, TFT_WHITE); break;
    case SLEEPY:    d.fillCircle(160, 146, 5, TFT_WHITE); break;
    default:        d.fillRect(135, 140, 50, 6, TFT_WHITE);
    }
}

// "face <mood>" from the relay. Runs on the network task: only store it (the screen
// belongs to loop()). Everything else goes to the audio commands.
static bool onCommand(const char* cmd) {
    if (!strncmp(cmd, "face ", 5)) {
        for (int i = 0; i < MOOD_COUNT; i++)
            if (!strcmp(cmd + 5, MOOD_NAMES[i])) s_wantMood = i;
        return true;
    }
    return audioCommand(cmd);
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
    if (st.relayOnline) {
        status.setTextColor(TFT_GREEN);
        status.printf("Laptop %s", st.relayIp);
    } else {
        status.setTextColor(TFT_YELLOW);
        status.print("Looking for the laptop (relay)...");
    }
    status.setTextColor(TFT_LIGHTGREY);
    status.setCursor(4, 30);
    if (audioMicOn()) status.setTextColor(TFT_RED);
    else if (audioSpeaking()) status.setTextColor(TFT_CYAN);
    const char* state = audioMuted() ? "mic muted (tap to unmute)"
                      : audioSpeaking() ? "speaking"
                      : audioMicOn() ? "listening"
                      : audioHandsFree() ? "..." : "tap to talk";
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
    M5.Display.setBrightness(30);   // brighter heats the screen and drains the battery
    M5.Display.fillScreen(TFT_BLACK);
    status.setColorDepth(16);
    status.createSprite(320, 44);
    status.setTextSize(1);

    M5.Speaker.begin();
    M5.Speaker.setVolume(120);

    bool base = boardInit();
    servoInit();
    setBaseLeds(0, 0, 40);
    bool camera = cameraInit();     // the link streams video if someone asks; this demo doesn't
    Serial.printf("\n[Demo 02.1 " FW_VERSION "] base:%s camera:%s\n", base ? "ok" : "NO", camera ? "ok" : "ERROR");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("stackchan");
    WiFi.setSleep(false);              // modem sleep adds 100+ ms of jitter to every packet
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    servoStartTask();
    M5.Speaker.tone(2000, 80);   // before audioStart(): nothing else uses the speaker yet
}

void loop() {
    i2cLock();   // touch, PMIC, LEDs: the audio task also uses this bus
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
            if (!s_linkStarted) {
                linkStart();
                audioStart();
                linkSetCommandHandler(onCommand);   // after audioStart(): ours first, then the audio ones
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

    // Face: eyes when the mood changes; mouth open while listening or speaking.
    static int lastMouth = -1;
    if (s_wantMood != s_mood) {
        s_mood = s_wantMood;
        drawEyes(s_mood);
        lastMouth = -1;
    }
    int mouth = audioSpeaking() ? 2 : (audioMicOn() ? 1 : 0);
    if (mouth != lastMouth) {
        lastMouth = mouth;
        drawMouth(mouth);
    }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line == "home") servoHome();
        else if (line.startsWith("face ")) onCommand(line.c_str());   // try the faces without the relay
    }
    i2cUnlock();
    delay(10);
}
