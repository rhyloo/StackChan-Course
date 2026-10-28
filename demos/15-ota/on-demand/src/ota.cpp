// Demo 15 - OTA on demand: the base, with the OTA off until you ask for it.
//
// The same two app slots and rollback as ../firmware/src/ota.cpp, but the robot is in
// one of two modes, chosen when it starts:
//
//   test mode (default)  the test runs and the base doesn't touch the Wi-Fi. Holding the
//                        screen HOLD_MS opens the menu: Activate OTA / Restart / Cancel.
//   OTA mode             the test doesn't run. Wi-Fi + ArduinoOTA (3232) + web page (80)
//                        + UDP 3233 ("who"/"log"), like the other base.
//
// OTA mode is always entered with a restart, so the radio and the memory are clean
// whatever the test did with them (BLE, ESP-NOW, an access point, the camera):
//   - the menu or otaEnable() leaves a mark in RTC memory and restarts;
//   - the screen is held while it starts;
//   - the test crashed SAFE_AFTER_CRASHES times in a row.
//
// A new firmware starts "pending", in test mode. It stays (valid) when the test has run
// CONFIRM_MS and loop() still turns, that is, when the menu can be reached: no Wi-Fi
// needed. If it crashes before, the bootloader boots the previous one; if it gets stuck,
// at ROLLBACK_MS this file goes back. Using the menu also confirms it: loop() is alive.
#include "ota.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <lwip/sockets.h>
#include "board.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static constexpr uint32_t CONFIRM_MS   = 15000;   // test running this long, loop() alive = the update stays
static constexpr uint32_t ROLLBACK_MS  = 90000;   // still pending at this uptime: back to the previous firmware
static constexpr uint32_t CRASH_RESET_MS = 30000; // test alive this long: the crash count starts again
static constexpr int      SAFE_AFTER_CRASHES = 3;
static constexpr uint32_t HOLD_MS      = 3000;    // hold the screen this long: the OTA menu
static constexpr uint32_t HOLD_SHOW_MS = 800;     // from here a bar at the top shows the hold
static constexpr uint32_t MENU_TIMEOUT_MS = 10000;
static constexpr uint16_t INFO_PORT    = 3233;    // "who" / "log" (ArduinoOTA uses 3232)
static constexpr uint32_t LOG_SUB_MS   = 10000;   // the laptop asks again every 2 s
static constexpr uint8_t  BRIGHTNESS   = 120;
static constexpr uint32_t RTC_MAGIC    = 0x0DE3A4D0;
static constexpr uint32_t OTA_REQUEST  = 0x07A0F00D;  // in s_rtcRequest: start in OTA mode

static auto& lcd = M5.Display;
static WebServer s_web(80);
static char s_name[20];
static char s_build[9];                       // start of the ELF SHA-256: changes with every build
static uint32_t s_bootId;                     // random per start: the laptop sees the robot restart

// RTC memory survives a crash and a software restart (not a power cut).
RTC_NOINIT_ATTR static uint32_t s_rtcMagic, s_crashes, s_rtcRequest;

enum class Image { valid, pending };          // pending = updated, not confirmed yet
static volatile Image s_image = Image::valid;
static bool s_rolledBack;                     // the bootloader threw away the last update
static bool s_otaMode;
static const char* s_otaWhy = "";

static volatile bool s_updating;              // set by the network task, seen by loop()
static volatile uint32_t s_updBytes, s_updTotal;
static volatile uint32_t s_loopBeat, s_testSince;
static bool s_testStarted;

// ---------------------------------------------------------------- log
// The base's own messages: to the USB serial always, and over the Wi-Fi in OTA mode to
// whoever asked "log" (run.sh send / run.sh log). Tests print with Serial.
struct LogMsg { char text[160]; };
static QueueHandle_t s_logQueue;
static int s_sock = -1;
static sockaddr_in s_sub;
static uint32_t s_subUntil;

static void netLog(const char* fmt, ...) {
    LogMsg m;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m.text, sizeof(m.text), fmt, ap);
    va_end(ap);
    Serial.print(m.text);
    xQueueSend(s_logQueue, &m, 0);            // full queue: the line only goes to USB
}

const char* otaRobotName() { return s_name; }

__attribute__((weak)) void testStop() {}
__attribute__((weak)) void testRedraw() { lcd.fillScreen(TFT_BLACK); }

// Arduino marks a new firmware valid as soon as it starts. Returning true here
// says "not yet": the base decides it in checkImage().
extern "C" bool verifyRollbackLater() { return true; }

// ---------------------------------------------------------------- state
static const char* stateName() { return s_image == Image::pending ? "pending" : "valid"; }

static size_t infoJson(char* out, size_t n) {
    return snprintf(out, n,
        "{\"robot\":\"%s\",\"test\":\"%s\",\"build\":\"%s\",\"state\":\"%s\",\"rolled_back\":%s,"
        "\"slot\":\"%s\",\"ip\":\"%s\",\"uptime\":%lu,\"boot\":%lu,\"free_heap\":%lu,"
        "\"ota\":\"on-demand\",\"why\":\"%s\"}",
        s_name, TEST_NAME, s_build, stateName(), s_rolledBack ? "true" : "false",
        esp_ota_get_running_partition()->label, WiFi.localIP().toString().c_str(),
        (unsigned long)(millis() / 1000), (unsigned long)s_bootId, (unsigned long)ESP.getFreeHeap(), s_otaWhy);
}

static void markValid(const char* why) {
    if (s_image == Image::pending && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        s_image = Image::valid;
        netLog("[OTA] test '%s' confirmed (%s): this firmware stays\n", TEST_NAME, why);
    }
}

// Test mode only: in OTA mode the test doesn't run, so there is nothing to judge.
static void checkImage(uint32_t now) {
    if (s_otaMode) return;
    if (s_image == Image::pending) {
        const bool alive = (int32_t)(now - s_loopBeat) < 3000;
        if (s_testSince && now - s_testSince >= CONFIRM_MS && alive) {
            markValid("it ran 15 s");
        } else if (now >= ROLLBACK_MS) {
            netLog("[OTA] not confirmed in %lu s (%s): back to the previous firmware\n",
                   (unsigned long)(ROLLBACK_MS / 1000), !s_testSince ? "testSetup() never ended" : "loop() stuck");
            delay(200);
            esp_ota_mark_app_invalid_rollback_and_reboot();
            s_image = Image::valid;               // only gets here if there is no previous firmware
        }
    }
    if (s_crashes && s_testSince && now - s_testSince >= CRASH_RESET_MS) s_crashes = 0;
}

// Restart straight into OTA mode (also after a failed update: the next try can get in).
static void restartInOtaMode() {
    s_rtcRequest = OTA_REQUEST;
    delay(200);
    ESP.restart();
}

// ---------------------------------------------------------------- network (OTA mode)
static void sendTo(const sockaddr_in& to, const char* data, size_t len) {
    sendto(s_sock, data, len, 0, (const sockaddr*)&to, sizeof(to));
}

static void udpStart() {
    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in me = {};
    me.sin_family = AF_INET;
    me.sin_port = htons(INFO_PORT);
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(s_sock, (sockaddr*)&me, sizeof(me));
    fcntl(s_sock, F_SETFL, O_NONBLOCK);
}

// "who": answer with the state. "log": the same, and send the base's messages there for a while.
static void udpPoll(uint32_t now) {
    char buf[16];
    sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n;
    while ((n = recvfrom(s_sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl)) > 0) {
        buf[n] = 0;
        const bool log = !strncmp(buf, "log", 3);
        if (!log && strncmp(buf, "who", 3)) continue;
        if (log) { s_sub = from; s_subUntil = now + LOG_SUB_MS; }
        char json[384];
        sendTo(from, json, infoJson(json, sizeof(json)));
        fl = sizeof(from);
    }
}

static void drainLog(uint32_t now) {
    LogMsg m;
    while (xQueueReceive(s_logQueue, &m, 0) == pdTRUE)
        if (s_sock >= 0 && s_subUntil && (int32_t)(s_subUntil - now) > 0) sendTo(s_sub, m.text, strlen(m.text));
}

static const char PAGE[] = R"HTML(<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Stack-chan OTA</title>
<style>body{font-family:system-ui,sans-serif;max-width:34em;margin:2em auto;padding:0 1em}
td{padding:.15em 1em .15em 0}progress{width:100%}code{background:#8882;padding:0 .2em}</style>
<h1>Stack-chan OTA (on demand)</h1><table id=info></table>
<h2>Upload a firmware</h2>
<p>The <code>firmware.bin</code> PlatformIO builds, in <code>.pio/build/&lt;env&gt;/</code>.
If it isn't a test of demo 15 it has no OTA: to come back, flash demo 15 over USB.</p>
<input type=file id=f accept=.bin> <button onclick=up()>Upload</button>
<p><progress id=p max=1 value=0></progress></p><p id=msg></p>
<script>
const $ = id => document.getElementById(id);
async function info() {
  try {
    const j = await (await fetch('/info')).json();
    $('info').innerHTML = Object.entries(j).map(([k, v]) => `<tr><td>${k}<td><b>${v}</b>`).join('');
  } catch (e) {}
}
info(); setInterval(info, 2000);
function up() {
  const f = $('f').files[0]; if (!f) return;
  const x = new XMLHttpRequest(), d = new FormData();
  d.append('firmware', f);
  x.upload.onprogress = e => $('p').value = e.loaded / e.total;
  x.onload = () => $('msg').textContent = x.responseText;
  x.onerror = () => $('msg').textContent = 'The upload failed.';
  x.open('POST', '/update?size=' + f.size); x.send(d);
  $('msg').textContent = 'Uploading...';
}
</script>
)HTML";

static void webStart() {
    s_web.on("/", HTTP_GET, [] { s_web.send_P(200, "text/html", PAGE); });
    s_web.on("/info", HTTP_GET, [] {
        char json[384];
        infoJson(json, sizeof(json));
        s_web.send(200, "application/json", json);
    });
    s_web.on("/update", HTTP_POST, [] {
        const bool ok = !Update.hasError();
        s_web.sendHeader("Connection", "close");
        s_web.send(ok ? 200 : 500, "text/plain", ok ? "Done: the robot restarts with the new firmware." : Update.errorString());
        netLog(ok ? "[OTA] web update done, restarting\n" : "[OTA] web update failed: %s, restarting\n", Update.errorString());
        delay(300);
        if (ok) ESP.restart();
        restartInOtaMode();
    }, [] {
        HTTPUpload& up = s_web.upload();
        if (up.status == UPLOAD_FILE_START) {
            s_updTotal = s_web.arg("size").toInt();
            s_updBytes = 0;
            s_updating = true;
            netLog("[OTA] receiving %s from the web page\n", up.filename.c_str());
            Update.begin(s_updTotal ? s_updTotal : UPDATE_SIZE_UNKNOWN);
        } else if (up.status == UPLOAD_FILE_WRITE) {
            Update.write(up.buf, up.currentSize);
            s_updBytes += up.currentSize;
        } else if (up.status == UPLOAD_FILE_END) {
            Update.end(true);
        } else if (up.status == UPLOAD_FILE_ABORTED) {
            Update.abort();
        }
    });
    s_web.begin();
}

static void otaStart() {
    ArduinoOTA.setHostname(s_name);           // also its mDNS name: <name>.local
#ifdef OTA_PASSWORD
    ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
    ArduinoOTA.onStart([] {
        s_updBytes = s_updTotal = 0;
        s_updating = true;
        netLog("[OTA] receiving an update (espota)\n");
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) { s_updBytes = done; s_updTotal = total; });
    ArduinoOTA.onEnd([] { netLog("[OTA] update done, restarting with the OTA off\n"); });
    ArduinoOTA.onError([](ota_error_t e) {
        netLog("[OTA] update failed (error %u), restarting in OTA mode\n", e);
        restartInOtaMode();
    });
    ArduinoOTA.begin();
    MDNS.addService("http", "tcp", 80);
}

// Core 0. In test mode it only watches the new firmware (checkImage); in OTA mode it
// also runs the network.
static void netTask(void*) {
    bool started = false;
    wl_status_t last = WL_IDLE_STATUS;
    for (;;) {
        const uint32_t now = millis();
        if (s_otaMode) {
            const wl_status_t w = WiFi.status();
            if (w != last) {
                last = w;
                if (w == WL_CONNECTED) {
                    if (!started) { otaStart(); webStart(); udpStart(); started = true; }
                    netLog("[WIFI] connected: %s  http://%s.local  (http://%s)\n", s_name, s_name,
                           WiFi.localIP().toString().c_str());
                }
            }
            if (started && w == WL_CONNECTED) {
                ArduinoOTA.handle();          // during an espota update it returns at the end
                s_web.handleClient();
                udpPoll(now);
            }
        }
        drainLog(now);
        checkImage(now);
        vTaskDelay(pdMS_TO_TICKS(s_otaMode ? 5 : 100));
    }
}

// ---------------------------------------------------------------- screen (loop task)
static void button(int y, uint16_t bg, const char* text) {
    lcd.fillRoundRect(40, y, 240, 40, 8, bg);
    lcd.setTextColor(TFT_WHITE, bg);
    lcd.setTextDatum(middle_center);
    lcd.drawString(text, 160, y + 20);
}

static bool inButton(const m5::touch_detail_t& d, int y) {
    return d.x >= 40 && d.x < 280 && d.y >= y && d.y < y + 40;
}

static void drawUpdating() {
    static int lastPct = -2;
    const uint32_t total = s_updTotal, bytes = s_updBytes;
    const int pct = total ? bytes * 100 / total : -1;
    if (pct == lastPct && lastPct != -2) return;
    if (lastPct == -2) {
        lcd.fillScreen(TFT_NAVY);
        lcd.setTextColor(TFT_WHITE, TFT_NAVY);
        lcd.setTextDatum(middle_center);
        lcd.setFont(&fonts::FreeSansBold12pt7b);
        lcd.drawString("Receiving an update", 160, 80);
        lcd.setFont(&fonts::efontJA_16);
        lcd.drawString("Don't switch it off", 160, 200);
    }
    lastPct = pct;
    lcd.drawRoundRect(30, 120, 260, 24, 6, TFT_WHITE);
    if (pct >= 0) lcd.fillRoundRect(32, 122, 256 * pct / 100, 20, 5, TFT_CYAN);
    lcd.setTextColor(TFT_WHITE, TFT_NAVY);
    lcd.setTextDatum(middle_center);
    char txt[32];
    if (pct >= 0) snprintf(txt, sizeof(txt), "  %d %%  ", pct);
    else snprintf(txt, sizeof(txt), "  %lu KB  ", (unsigned long)(bytes / 1024));
    lcd.drawString(txt, 160, 165);
}

static constexpr int BACK_Y = 192;            // "Back to the test" button in OTA mode

static void drawOtaMode() {
    static uint32_t last = 0;
    static bool drawn = false;
    if (!drawn) {
        drawn = true;
        lcd.fillScreen(TFT_BLACK);
        lcd.fillRect(0, 0, lcd.width(), 32, TFT_ORANGE);
        lcd.setTextColor(TFT_BLACK, TFT_ORANGE);
        lcd.setTextDatum(middle_center);
        lcd.setFont(&fonts::FreeSansBold12pt7b);
        lcd.drawString("OTA mode", 160, 17);
        lcd.setFont(&fonts::efontJA_16);
        lcd.setTextDatum(top_left);
        lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        lcd.drawString(String("Why: ") + s_otaWhy, 10, 40);
        lcd.drawString(String("Firmware: '") + TEST_NAME + "', " + stateName() + (s_rolledBack ? " (rolled back)" : ""), 10, 62);
        lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        lcd.drawString("From the laptop:", 10, 120);
        lcd.setTextColor(TFT_CYAN, TFT_BLACK);
        lcd.drawString("  demos/15-ota/run.sh send TEST", 10, 142);
        button(BACK_Y, TFT_DARKGREEN, "Back to the test");
    }
    const uint32_t now = millis();
    if (now - last < 1000 && last) return;
    last = now;
    char line[64];
    if (WiFi.status() == WL_CONNECTED) snprintf(line, sizeof(line), "%s  %s", s_name, WiFi.localIP().toString().c_str());
    else snprintf(line, sizeof(line), "Wi-Fi: %s...", WIFI_SSID);
    lcd.fillRect(0, 86, lcd.width(), 24, TFT_BLACK);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_GREENYELLOW : TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(line, 10, 88);
}

// M5.update() reads the touch over I2C: under the lock, in case a test has another task on
// that bus (an audio task switching the codec between mic and speaker).
static void update() {
    I2CGuard lock;
    M5.update();
}

// ---------------------------------------------------------------- the OTA menu (test mode)
static constexpr int MENU_OTA_Y = 64, MENU_RESTART_Y = 114, MENU_CANCEL_Y = 164;

// True when the screen has been held HOLD_MS. From HOLD_SHOW_MS a bar at the top grows;
// if the finger lifts before, the test redraws its screen.
static bool heldForMenu() {
    static uint32_t since = 0;
    static bool shown = false;
    const uint32_t now = millis();
    if (M5.Touch.getCount() == 0) {
        since = 0;
        if (shown) { shown = false; testRedraw(); }
        return false;
    }
    if (!since) since = now ? now : 1;
    const uint32_t held = now - since;
    if (held >= HOLD_MS) { since = 0; shown = false; return true; }
    if (held >= HOLD_SHOW_MS) {
        shown = true;
        lcd.fillRect(0, 0, lcd.width() * (held - HOLD_SHOW_MS) / (HOLD_MS - HOLD_SHOW_MS), 6, TFT_ORANGE);
    }
    return false;
}

// Modal: the test doesn't run while the menu is open.
static void runMenu() {
    netLog("[OTA] menu opened\n");
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(middle_center);
    lcd.drawString(String("Test '") + TEST_NAME + "'", 160, 20);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd.drawString("OTA is off", 160, 42);
    button(MENU_OTA_Y, TFT_ORANGE, "Activate OTA");
    button(MENU_RESTART_Y, TFT_DARKGREY, "Restart");
    button(MENU_CANCEL_Y, TFT_MAROON, "Cancel");

    do { update(); s_loopBeat = millis(); delay(10); } while (M5.Touch.getCount());   // let go first
    for (const uint32_t t0 = millis(); millis() - t0 < MENU_TIMEOUT_MS;) {
        update();
        s_loopBeat = millis();
        const auto d = M5.Touch.getDetail();
        if (d.wasClicked()) {
            if (inButton(d, MENU_OTA_Y)) {
                lcd.fillScreen(TFT_BLACK);
                lcd.drawString("Restarting in OTA mode...", 160, 120);
                otaEnable();
            }
            if (inButton(d, MENU_RESTART_Y)) {
                markValid("restarted from the menu");
                if (s_testStarted) testStop();
                delay(200);
                ESP.restart();
            }
            if (inButton(d, MENU_CANCEL_Y)) break;
        }
        delay(10);
    }
    netLog("[OTA] menu closed\n");
    testRedraw();
}

void otaEnable() {
    netLog("[OTA] going into OTA mode\n");
    markValid("the OTA menu was used");          // loop() is alive; restarting a pending one would roll it back
    if (s_testStarted) testStop();
    restartInOtaMode();
}

// ---------------------------------------------------------------- setup / loop
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    Serial.setTxTimeoutMs(0);                 // without a USB cable, Serial doesn't block
    s_logQueue = xQueueCreate(24, sizeof(LogMsg));
    s_bootId = esp_random();
    char sha[65];
    esp_ota_get_app_elf_sha256(sha, sizeof(sha));
    strlcpy(s_build, sha, sizeof(s_build));
    uint8_t mac[6];
    WiFi.macAddress(mac);                     // read from eFuse: doesn't switch the radio on
    snprintf(s_name, sizeof(s_name), "stackchan-%02X%02X", mac[4], mac[5]);

    // Crash count: only crashes (panic, watchdogs) add up; any other start resets it.
    const esp_reset_reason_t why = esp_reset_reason();
    const bool crashed = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
    if (s_rtcMagic != RTC_MAGIC) { s_rtcMagic = RTC_MAGIC; s_crashes = 0; s_rtcRequest = 0; }
    s_crashes = crashed ? s_crashes + 1 : 0;
    const bool requested = s_rtcRequest == OTA_REQUEST && why == ESP_RST_SW;
    s_rtcRequest = 0;

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) s_image = Image::pending;
    s_rolledBack = esp_ota_get_last_invalid_partition() != nullptr;

    lcd.setBrightness(BRIGHTNESS);
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(middle_center);
    lcd.drawString(String("OTA on demand - test '") + TEST_NAME + "'", 160, 100);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    if (!requested) lcd.drawString("Hold the screen now for OTA mode", 160, 130);

    if (!boardInit()) Serial.println("Base K151 not answering (PY32)");
    setBaseLeds(0, 0, 0);                     // the base keeps the LEDs lit across a restart

    // Holding the screen while it starts: OTA mode.
    bool held = false;
    for (uint32_t t0 = millis(); !requested && millis() - t0 < 1200 && !held;) {
        M5.update();
        held = M5.Touch.getCount() > 0;
        delay(20);
    }
    if (requested) { s_otaMode = true; s_otaWhy = "activated from the menu"; }
    else if (held) { s_otaMode = true; s_otaWhy = "the screen was held at start"; }
    else if (s_crashes >= SAFE_AFTER_CRASHES && s_image == Image::valid) {
        s_otaMode = true;
        s_otaWhy = "the test crashed 3 times in a row";
    }
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextDatum(top_left);

    netLog("\n[OTA] %s  test '%s'  build %s  slot %s  %s%s  start: %s  -> %s\n", s_name, TEST_NAME, s_build,
           running->label, stateName(), s_rolledBack ? " (the last update was rolled back)" : "",
           crashed ? "after a crash" : "normal", s_otaMode ? "OTA mode" : "test mode, OTA off");
    if (s_otaMode) {
        netLog("[OTA] OTA mode (%s): the test doesn't run, waiting for an update\n", s_otaWhy);
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(s_name);
        WiFi.setSleep(false);
        WiFi.setAutoReconnect(true);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    } else {
        netLog("[OTA] hold the screen %lu s for the OTA menu\n", (unsigned long)(HOLD_MS / 1000));
        if (s_image == Image::pending)
            netLog("[OTA] new firmware: it stays if the test runs %lu s\n", (unsigned long)(CONFIRM_MS / 1000));
    }
    xTaskCreatePinnedToCore(netTask, "ota-net", 8192, nullptr, 2, nullptr, 0);
}

void loop() {
    update();
    s_loopBeat = millis();
    if (s_otaMode) {
        if (s_updating) drawUpdating();
        else {
            drawOtaMode();
            const auto d = M5.Touch.getDetail();
            if (d.wasClicked() && inButton(d, BACK_Y)) {
                netLog("[OTA] back to the test\n");
                delay(200);
                ESP.restart();                // no OTA_REQUEST mark: test mode
            }
        }
        delay(20);
        return;
    }
    if (!s_testStarted) {
        testSetup();
        s_testStarted = true;
        s_testSince = millis();
    }
    if (heldForMenu()) {
        runMenu();
        return;
    }
    testLoop();
}
