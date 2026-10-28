// Demo 15 - The OTA base: Wi-Fi, updates over the air and the safety nets.
//
// The flash has two app slots (app0 and app1). An update is written to the slot that
// is not running; when it is complete the robot restarts from it. The new firmware
// starts "pending": if it doesn't prove that it works, the bootloader goes back to
// the previous one (rollback). Here "it works" means: the test has run CONFIRM_MS,
// loop() is still turning and the Wi-Fi is up, so a next update can get in.
//
//   - It crashes before that (panic, watchdog): on the restart the bootloader boots
//     the previous firmware.
//   - It never gets there (stuck, no Wi-Fi): after ROLLBACK_MS this file goes back.
//   - It crashes later, once confirmed: after SAFE_AFTER_CRASHES crashes in a row it
//     starts in safe mode, without the test, only waiting for an update. Holding
//     the screen while it starts does the same.
//
// Updates come in two ways, both in the network task (core 0):
//   - espota (ArduinoOTA, UDP/TCP 3232): what `run.sh send` and PlatformIO use;
//   - a web page on port 80, to upload a .bin from the browser.
// On UDP port 3233 it answers "who" (the laptop looking for robots) with its state,
// and "log" too, plus the Log lines for a while, to whoever asked.
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

static constexpr uint32_t CONFIRM_MS   = 15000;   // test running this long + Wi-Fi up = the update stays
static constexpr uint32_t ROLLBACK_MS  = 90000;   // still pending at this uptime: back to the previous firmware
static constexpr uint32_t CRASH_RESET_MS = 30000; // test alive this long: the crash count starts again
static constexpr int      SAFE_AFTER_CRASHES = 3;
static constexpr uint16_t INFO_PORT    = 3233;    // "who" / "log" and the Log lines (ArduinoOTA uses 3232)
static constexpr uint32_t LOG_SUB_MS   = 10000;   // the laptop asks again every 2 s
static constexpr uint8_t  BRIGHTNESS   = 120;

static auto& lcd = M5.Display;
static M5Canvas s_bar(&M5.Display);
static WebServer s_web(80);
static char s_name[20];
static char s_build[9];                       // start of the ELF SHA-256: changes with every build
static uint32_t s_bootId;                     // random per start: the laptop sees the robot restart

// RTC memory survives a crash and a software restart (not a power cut).
RTC_NOINIT_ATTR static uint32_t s_rtcMagic, s_crashes;

enum class Image { valid, pending };          // pending = updated, not confirmed yet
static volatile Image s_image = Image::valid;
static bool s_rolledBack;                     // the bootloader threw away the last update
static bool s_safe;
static const char* s_safeWhy = "";

static volatile bool s_updating;              // set by the network task, seen by loop()
static volatile uint32_t s_updBytes, s_updTotal;
static volatile uint32_t s_loopBeat, s_testSince;
static bool s_testStarted, s_testStopped;

// ---------------------------------------------------------------- log
struct LogMsg { char text[160]; };
static QueueHandle_t s_logQueue;
static int s_sock = -1;
static sockaddr_in s_sub;                     // the laptop that wants the Log lines
static uint32_t s_subUntil;

OtaLog Log;

static void logLine(const char* text) {
    Serial.print(text);
    LogMsg m;
    strlcpy(m.text, text, sizeof(m.text));
    xQueueSend(s_logQueue, &m, 0);            // full queue: the line only goes to USB
}

// Messages of the base itself, from any task.
static void netLog(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logLine(buf);
}

size_t OtaLog::write(uint8_t c) {
    _line[_len++] = c;
    if (c == '\n' || _len == sizeof(_line) - 1) {
        _line[_len] = 0;
        logLine(_line);
        _len = 0;
    }
    return 1;
}

const char* otaRobotName() { return s_name; }

__attribute__((weak)) void testStop() {}

// Arduino marks a new firmware valid as soon as it starts. Returning true here
// says "not yet": the base decides it in checkImage().
extern "C" bool verifyRollbackLater() { return true; }

// ---------------------------------------------------------------- state
static const char* stateName() {
    return s_safe ? "safe" : s_image == Image::pending ? "pending" : "valid";
}

static size_t infoJson(char* out, size_t n) {
    return snprintf(out, n,
        "{\"robot\":\"%s\",\"test\":\"%s\",\"build\":\"%s\",\"state\":\"%s\",\"rolled_back\":%s,"
        "\"slot\":\"%s\",\"ip\":\"%s\",\"uptime\":%lu,\"boot\":%lu,\"free_heap\":%lu}",
        s_name, TEST_NAME, s_build, stateName(), s_rolledBack ? "true" : "false",
        esp_ota_get_running_partition()->label, WiFi.localIP().toString().c_str(),
        (unsigned long)(millis() / 1000), (unsigned long)s_bootId, (unsigned long)ESP.getFreeHeap());
}

static void checkImage(uint32_t now) {
    if (!s_safe && s_image == Image::pending && !s_updating) {
        const bool alive = now - s_loopBeat < 3000;
        if (s_testSince && now - s_testSince >= CONFIRM_MS && alive && WiFi.status() == WL_CONNECTED) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                s_image = Image::valid;
                netLog("[OTA] test '%s' confirmed: this firmware stays\n", TEST_NAME);
            }
        } else if (now >= ROLLBACK_MS) {
            netLog("[OTA] not confirmed in %lu s (%s): back to the previous firmware\n",
                   (unsigned long)(ROLLBACK_MS / 1000), !alive ? "loop() stuck" : !s_testSince ? "testSetup() never ended" : "no Wi-Fi");
            delay(200);
            esp_ota_mark_app_invalid_rollback_and_reboot();
            s_image = Image::valid;               // only gets here if there is no previous firmware
        }
    }
    if (s_crashes && s_testSince && now - s_testSince >= CRASH_RESET_MS) s_crashes = 0;
}

// ---------------------------------------------------------------- network task
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

// "who": answer with the state. "log": the same, and send the Log lines there for a while.
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
        char json[320];
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
<h1>Stack-chan OTA</h1><table id=info></table>
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
        char json[320];
        infoJson(json, sizeof(json));
        s_web.send(200, "application/json", json);
    });
    s_web.on("/update", HTTP_POST, [] {
        const bool ok = !Update.hasError();
        s_web.sendHeader("Connection", "close");
        s_web.send(ok ? 200 : 500, "text/plain", ok ? "Done: the robot restarts with the new firmware." : Update.errorString());
        netLog(ok ? "[OTA] web update done, restarting\n" : "[OTA] web update failed: %s, restarting\n", Update.errorString());
        delay(300);
        ESP.restart();
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
    ArduinoOTA.onEnd([] { netLog("[OTA] update done, restarting\n"); });
    ArduinoOTA.onError([](ota_error_t e) {
        netLog("[OTA] update failed (error %u), restarting\n", e);
        delay(300);
        ESP.restart();                        // the test was stopped: start clean
    });
    ArduinoOTA.begin();
    MDNS.addService("http", "tcp", 80);
}

static void netTask(void*) {
    bool started = false;
    wl_status_t last = WL_IDLE_STATUS;
    for (;;) {
        const uint32_t now = millis();
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
            ArduinoOTA.handle();              // during an espota update it returns at the end
            s_web.handleClient();
            udpPoll(now);
        }
        drainLog(now);
        checkImage(now);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ---------------------------------------------------------------- screen (loop task)
static void drawBar() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (now - last < 1000 && last) return;
    last = now;
    uint16_t bg = TFT_DARKGREEN;
    char right[48];
    if (s_safe) { bg = TFT_RED; snprintf(right, sizeof(right), "SAFE MODE"); }
    else if (s_image == Image::pending) {
        bg = TFT_ORANGE;
        const uint32_t ran = s_testSince ? (now - s_testSince) / 1000 : 0;
        snprintf(right, sizeof(right), "%s  checking %lus", TEST_NAME, (unsigned long)(CONFIRM_MS / 1000 > ran ? CONFIRM_MS / 1000 - ran : 0));
    } else snprintf(right, sizeof(right), "%s%s", TEST_NAME, s_rolledBack ? "  (rolled back)" : "  OK");
    char left[48];
    if (WiFi.status() == WL_CONNECTED) snprintf(left, sizeof(left), "%s %s", s_name, WiFi.localIP().toString().c_str());
    else { bg = TFT_DARKGREY; snprintf(left, sizeof(left), "Wi-Fi: %s...", WIFI_SSID); }
    s_bar.fillSprite(bg);
    s_bar.setTextColor(TFT_WHITE, bg);
    s_bar.setTextDatum(middle_left);
    s_bar.drawString(left, 4, OTA_BAR_H / 2);
    s_bar.setTextDatum(middle_right);
    s_bar.drawString(right, lcd.width() - 4, OTA_BAR_H / 2);
    s_bar.pushSprite(0, 0);
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

static void drawSafe() {
    static bool drawn = false;
    if (drawn) return;
    drawn = true;
    lcd.fillRect(0, OTA_BAR_H, lcd.width(), lcd.height() - OTA_BAR_H, TFT_BLACK);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.drawString("Safe mode", 12, 36);
    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(0, 76);
    lcd.printf("  Test '%s' doesn't run:\n  %s.\n\n  Send another test from the laptop:\n"
               "    demos/15-ota/run.sh send hello\n  or open the address above in a browser.",
               TEST_NAME, s_safeWhy);
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
    WiFi.macAddress(mac);
    snprintf(s_name, sizeof(s_name), "stackchan-%02X%02X", mac[4], mac[5]);

    // Crash count: only crashes (panic, watchdogs) add up; any other start resets it.
    const esp_reset_reason_t why = esp_reset_reason();
    const bool crashed = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
    if (s_rtcMagic != 0x07A15AFE) { s_rtcMagic = 0x07A15AFE; s_crashes = 0; }
    s_crashes = crashed ? s_crashes + 1 : 0;

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) s_image = Image::pending;
    s_rolledBack = esp_ota_get_last_invalid_partition() != nullptr;

    lcd.setBrightness(BRIGHTNESS);
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::efontJA_16);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(middle_center);
    lcd.drawString(String("OTA base - test '") + TEST_NAME + "'", 160, 100);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd.drawString("Hold the screen now for safe mode", 160, 130);
    lcd.setTextDatum(top_left);

    if (!boardInit()) Serial.println("Base K151 not answering (PY32)");
    setBaseLeds(0, 0, 0);

    // Holding the screen while it starts: safe mode (only waits for an update).
    bool held = false;
    for (uint32_t t0 = millis(); millis() - t0 < 1200 && !held;) {
        M5.update();
        held = M5.Touch.getCount() > 0;
        delay(20);
    }
    if (held) { s_safe = true; s_safeWhy = "the screen was held at start"; }
    else if (s_crashes >= SAFE_AFTER_CRASHES && s_image == Image::valid) {
        s_safe = true;
        s_safeWhy = "it crashed 3 times in a row";
    }

    s_bar.setColorDepth(16);
    s_bar.createSprite(lcd.width(), OTA_BAR_H);
    s_bar.setFont(&fonts::efontJA_12);
    lcd.fillScreen(TFT_BLACK);

    netLog("\n[OTA] %s  test '%s'  build %s  slot %s  %s%s  start: %s\n", s_name, TEST_NAME, s_build, running->label,
           stateName(), s_rolledBack ? " (the last update was rolled back)" : "",
           crashed ? "after a crash" : "normal");
    if (s_image == Image::pending)
        netLog("[OTA] new firmware: it stays if the test runs %lu s with Wi-Fi\n", (unsigned long)(CONFIRM_MS / 1000));

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(s_name);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    xTaskCreatePinnedToCore(netTask, "ota-net", 8192, nullptr, 2, nullptr, 0);
}

void loop() {
    M5.update();
    s_loopBeat = millis();
    if (s_updating) {
        if (!s_testStopped) {
            s_testStopped = true;
            if (s_testStarted) testStop();
        }
        drawUpdating();
        delay(50);
        return;
    }
    if (s_safe) {
        drawSafe();
        drawBar();
        delay(50);
        return;
    }
    if (!s_testStarted) {
        testSetup();
        s_testStarted = true;
        s_testSince = millis();
    }
    testLoop();
    drawBar();
}
