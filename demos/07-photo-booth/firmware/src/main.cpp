// Demo 07 - Photo booth: photo with a countdown, a themed filter and a print on
// an MX10 thermal printer.
//
//   1. Camera preview. Tap the left half (3 s) or the right half (5 s).
//   2. Countdown, flash and photo. The photo (JPEG) travels to the laptop.
//   3. Choose a filter: Gemini draws Ghibli, Gaming, Comic and Picasso; OpenCV on the
//      laptop does Original, Sketch and Pixel.
//   4. The laptop sends it back as dithered black and white, 384 dots wide:
//      what you see on the screen is exactly what comes out on the paper.
//   5. "Print": the robot connects to the MX10 over Bluetooth and prints it.
//
//   robot --PHOTO 0x29 (JPEG)--> laptop: filter (Gemini / OpenCV) + dithering
//   robot --PHOTO_EVENT 0x2A {"event":"filter"...}-->
//   robot <--PRINT_BITMAP 0x2B (1 bit, 384 px)-- laptop
//   robot --BLE--> MX10
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "board.h"
#include "camera.h"
#include "link.h"
#include "printer_mx10.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static constexpr uint8_t  T_PHOTO       = 0x29;   // robot -> laptop: photoId u32, idx u16, count u16, ms u32, JPEG
static constexpr uint8_t  T_PHOTO_EVENT = 0x2A;   // robot -> laptop: JSON
static constexpr uint8_t  T_BITMAP      = 0x2B;   // laptop -> robot: job u32, idx u16, count u16, w u16, h u16, rows
static constexpr size_t   PHOTO_HEADER  = 12;
static constexpr size_t   PHOTO_CHUNK   = 1184;   // + 12 + 4 = the link's 1200-byte datagram
static constexpr size_t   BITMAP_HEADER = 12;
static constexpr int      MAX_HEIGHT    = 576;    // 7 cm of paper at 8 dots/mm
static constexpr int      MAX_CHUNKS    = 64;
static constexpr uint8_t  JPEG_QUALITY  = 90;
static constexpr uint32_t ACK_MS        = 1500;   // resend the photo if the laptop doesn't confirm it
static constexpr int      SEND_TRIES    = 3;
static constexpr uint32_t WORK_TIMEOUT_MS = 90000;  // Gemini can take a while
static constexpr uint8_t  BRIGHTNESS    = 179;    // 70 %
static constexpr uint8_t  LED_ON        = 40;     // well under LED_MAX_LEVEL
static constexpr int      W = 320, H = 240, BAR_H = 44;

enum class State { Preview, Countdown, Sending, Menu, Working, Result, Printing };
static State s_state = State::Preview;

struct Filter { const char* id; const char* label; bool ai; };
// Menu grid: 4 columns x 2 rows under the thumbnail; AI filters on the top row.
static constexpr int FILTER_COLS = 4, FILTER_W = W / FILTER_COLS, FILTER_H = 60, FILTER_Y = 120;
static const Filter FILTERS[] = {
    { "ghibli", "Ghibli", true },       { "gaming", "Gaming", true },     { "comic", "Comic", true },
    { "picasso", "Picasso", true },     { "original", "Original", false }, { "sketch", "Sketch", false },
    { "pixel", "Pixel", false },
};
static constexpr int FILTER_COUNT = sizeof(FILTERS) / sizeof(FILTERS[0]);

static auto& lcd = M5.Display;
static M5Canvas s_view(&M5.Display);       // preview + countdown, drawn off-screen: no flicker
static uint16_t* s_photo = nullptr;        // the shot, RGB565 as the camera gives it (PSRAM)
static uint32_t s_photoId = 0;
static int s_countFrom = 3;
static uint32_t s_stateMs = 0;
static String s_filterLabel;
static bool s_linkStarted = false;

// --- Filled by the network task, read by loop() ---
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_ackedPhoto = 0;
static char s_status[96] = "";
static char s_error[128] = "";
static volatile bool s_statusChanged = false, s_errorArrived = false;
static uint8_t* s_bmp = nullptr;           // 1-bit rows from the laptop (PSRAM)
static volatile uint32_t s_bmpJob = 0;
static volatile int s_bmpW = 0, s_bmpH = 0, s_bmpCount = 0, s_bmpGot = 0;
static bool s_bmpHave[MAX_CHUNKS];
static volatile bool s_bmpReady = false, s_bmpAckDue = false;

// --------------------------------------------------------------------------- link
static bool onCommand(const char* cmd) {
    if (!strncmp(cmd, "photo ack ", 10)) { s_ackedPhoto = strtoul(cmd + 10, nullptr, 10); return true; }
    if (!strncmp(cmd, "photo status ", 13)) {
        portENTER_CRITICAL(&s_mux);
        strlcpy(s_status, cmd + 13, sizeof(s_status));
        s_statusChanged = true;
        portEXIT_CRITICAL(&s_mux);
        return true;
    }
    if (!strncmp(cmd, "photo error ", 12)) {
        portENTER_CRITICAL(&s_mux);
        strlcpy(s_error, cmd + 12, sizeof(s_error));
        s_errorArrived = true;
        portEXIT_CRITICAL(&s_mux);
        return true;
    }
    return false;
}

static void onMessage(uint8_t type, const uint8_t* p, size_t len) {
    if (type != T_BITMAP || len <= BITMAP_HEADER) return;
    const uint32_t job = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    const int idx = (p[4] << 8) | p[5], count = (p[6] << 8) | p[7];
    const int w = (p[8] << 8) | p[9], h = (p[10] << 8) | p[11];
    if (w != PRINTER_WIDTH || h <= 0 || h > MAX_HEIGHT || count <= 0 || count > MAX_CHUNKS || idx >= count) return;
    if (job != s_bmpJob) {   // a new image: start over
        s_bmpReady = false;
        s_bmpJob = job;
        s_bmpW = w; s_bmpH = h; s_bmpCount = count; s_bmpGot = 0;
        memset(s_bmpHave, 0, sizeof(s_bmpHave));
    }
    const size_t total = (size_t)h * PRINTER_ROW_BYTES, per = (total + count - 1) / count;
    const size_t off = idx * per, n = len - BITMAP_HEADER;
    if (off + n > total) return;
    if (!s_bmpHave[idx]) {
        memcpy(s_bmp + off, p + BITMAP_HEADER, n);
        s_bmpHave[idx] = true;
        if (++s_bmpGot == s_bmpCount) s_bmpReady = true;
    }
    if (s_bmpReady) s_bmpAckDue = true;   // also for resends: the first ack may have been lost
}

static void sendEvent(const char* json) {
    linkSend(T_PHOTO_EVENT, (const uint8_t*)json, strlen(json));
    Serial.printf("[PHOTO] %s\n", json);
}

static void sendPhoto(const uint8_t* jpg, size_t len) {
    static uint8_t pkt[PHOTO_HEADER + PHOTO_CHUNK];
    const uint16_t count = (len + PHOTO_CHUNK - 1) / PHOTO_CHUNK;
    for (uint16_t i = 0; i < count; i++) {
        const size_t off = (size_t)i * PHOTO_CHUNK, n = min(PHOTO_CHUNK, len - off);
        const uint32_t ms = millis();
        const uint8_t head[PHOTO_HEADER] = { (uint8_t)(s_photoId >> 24), (uint8_t)(s_photoId >> 16), (uint8_t)(s_photoId >> 8),
                                             (uint8_t)s_photoId, (uint8_t)(i >> 8), (uint8_t)i, (uint8_t)(count >> 8),
                                             (uint8_t)count, (uint8_t)(ms >> 24), (uint8_t)(ms >> 16), (uint8_t)(ms >> 8), (uint8_t)ms };
        memcpy(pkt, head, PHOTO_HEADER);
        memcpy(pkt + PHOTO_HEADER, jpg + off, n);
        linkSend(T_PHOTO, pkt, PHOTO_HEADER + n);
        delay(2);   // don't flood lwIP's queue
    }
}

// --------------------------------------------------------------------------- drawing
static uint16_t textOn(uint16_t bg) { return bg == TFT_WHITE || bg == TFT_YELLOW || bg == TFT_CYAN ? TFT_BLACK : TFT_WHITE; }

static void button(int x, int y, int w, int h, const char* label, uint16_t bg, const char* small = nullptr) {
    lcd.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 8, bg);
    lcd.setTextColor(textOn(bg), bg);
    lcd.setTextDatum(middle_center);
    lcd.drawString(label, x + w / 2, y + h / 2 - (small ? 6 : 0));
    if (small) lcd.drawString(small, x + w / 2, y + h / 2 + 12);
    lcd.setTextDatum(top_left);
}

static void message(const char* text, uint16_t color) {
    lcd.fillRect(0, H - BAR_H, W, BAR_H, TFT_BLACK);
    lcd.setTextColor(color, TFT_BLACK);
    lcd.setTextDatum(middle_center);
    lcd.drawString(text, W / 2, H - BAR_H / 2);
    lcd.setTextDatum(top_left);
}

// The shot at half size (160x120) in the top-left corner.
static void drawThumb() {
    static uint16_t row[160];
    lcd.startWrite();
    for (int y = 0; y < 120; y++) {
        const uint16_t* src = s_photo + (y * 2) * W;
        for (int x = 0; x < 160; x++) row[x] = src[x * 2];
        lcd.pushImage(0, y, 160, 1, (const lgfx::swap565_t*)row);
    }
    lcd.endWrite();
}

static void drawPreviewBar() {
    lcd.fillRect(0, H - BAR_H, W, BAR_H, TFT_BLACK);
    button(0, H - BAR_H, W / 2, BAR_H, "Photo in 3 s", TFT_CYAN);
    button(W / 2, H - BAR_H, W / 2, BAR_H, "Photo in 5 s", TFT_YELLOW);
}

static void drawMenu() {
    lcd.fillScreen(TFT_BLACK);
    drawThumb();
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(170, 10);
    lcd.print("Choose a filter");
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.setCursor(170, 32);
    lcd.print("AI = Gemini");
    button(160, 64, 160, 54, "New photo", TFT_DARKGREY);
    for (int i = 0; i < FILTER_COUNT; i++) {
        const Filter& f = FILTERS[i];
        button((i % FILTER_COLS) * FILTER_W, FILTER_Y + (i / FILTER_COLS) * FILTER_H, FILTER_W, FILTER_H,
               f.label, f.ai ? TFT_MAGENTA : TFT_BLUE, f.ai ? "AI" : nullptr);
    }
}

// The 1-bit image scaled to fit above the bar, black on white: what the paper will show.
static void drawBitmap() {
    const int areaH = H - BAR_H;
    const float s = min((float)W / s_bmpW, (float)areaH / s_bmpH);
    const int dw = s_bmpW * s, dh = s_bmpH * s, x0 = (W - dw) / 2, y0 = (areaH - dh) / 2;
    static uint16_t row[W];
    lcd.fillRect(0, 0, W, areaH, TFT_DARKGREY);
    lcd.startWrite();
    for (int y = 0; y < dh; y++) {
        const uint8_t* src = s_bmp + (int)(y / s) * PRINTER_ROW_BYTES;
        for (int x = 0; x < dw; x++) {
            const int sx = x / s;
            row[x] = (src[sx >> 3] >> (sx & 7)) & 1 ? TFT_BLACK : TFT_WHITE;
        }
        lcd.pushImage(x0, y0 + y, dw, 1, row);
    }
    lcd.endWrite();
}

static void drawResult() {
    drawBitmap();
    lcd.fillRect(0, H - BAR_H, W, BAR_H, TFT_BLACK);
    button(0, H - BAR_H, 120, BAR_H, "Print", TFT_GREEN);
    button(120, H - BAR_H, 100, BAR_H, "New filter", TFT_BLUE);
    button(220, H - BAR_H, 100, BAR_H, "New photo", TFT_DARKGREY);
}

static void printProgress(float f) {
    const int w = (W - 20) * min(1.0f, f);
    lcd.fillRect(10, H - 14, w, 8, TFT_GREEN);
}

// --------------------------------------------------------------------------- states
static void enter(State s) {
    s_state = s;
    s_stateMs = millis();
    switch (s) {
        case State::Preview: lcd.fillScreen(TFT_BLACK); drawPreviewBar(); setBaseLeds(0, 0, 0); break;
        case State::Menu: drawMenu(); break;
        case State::Result: drawResult(); break;
        default: break;
    }
}

static void showError(const char* text, State back) {
    Serial.printf("[PHOTO] error: %s\n", text);
    M5.Speaker.tone(500, 200);
    setBaseLeds(LED_ON, 0, 0);
    enter(back);
    message(text, TFT_RED);
}

static void drawLivePreview(const char* overlay) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return;
    s_view.pushImage(0, 0, W, H - BAR_H, (const lgfx::swap565_t*)fb->buf);   // first 196 rows of the frame
    esp_camera_fb_return(fb);
    LinkStats st;
    linkGetStats(&st);
    s_view.setFont(&fonts::efontJA_16);
    s_view.setTextDatum(top_left);
    s_view.setTextColor(st.relayOnline ? TFT_GREEN : TFT_ORANGE);
    s_view.setCursor(4, 2);
    s_view.print(WiFi.status() != WL_CONNECTED ? "Connecting to Wi-Fi..."
                 : st.relayOnline ? "Laptop ready" : "Looking for the laptop (run.sh)...");
    if (overlay) {
        s_view.setFont(&fonts::FreeSansBold24pt7b);
        s_view.setTextSize(3);
        s_view.setTextDatum(middle_center);
        s_view.setTextColor(TFT_WHITE);
        s_view.drawString(overlay, W / 2, (H - BAR_H) / 2);
        s_view.setTextSize(1);
    }
    s_view.pushSprite(0, 0);
}

static void takePhoto() {
    // Flash: white screen and LEDs, like a real photo booth.
    lcd.fillScreen(TFT_WHITE);
    setBaseLeds(LED_ON, LED_ON, LED_ON);
    M5.Speaker.tone(3000, 120);
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { showError("The camera gave no image", State::Preview); return; }
    memcpy(s_photo, fb->buf, min(fb->len, (size_t)W * H * 2));
    esp_camera_fb_return(fb);
    setBaseLeds(0, 0, 0);

    if (!linkRelayOnline()) { showError("No laptop: start run.sh", State::Preview); return; }
    s_photoId++;
    s_bmpReady = false;
    enter(State::Sending);
    lcd.fillScreen(TFT_BLACK);
    drawThumb();
    message("Sending the photo to the laptop...", TFT_WHITE);
    uint8_t* jpg = nullptr;
    size_t len = 0;
    if (!fmt2jpg((uint8_t*)s_photo, (size_t)W * H * 2, W, H, PIXFORMAT_RGB565, JPEG_QUALITY, &jpg, &len)) {
        showError("Could not compress the photo", State::Preview);
        return;
    }
    for (int t = 0; t < SEND_TRIES && s_ackedPhoto != s_photoId; t++) {
        sendPhoto(jpg, len);
        for (uint32_t t0 = millis(); millis() - t0 < ACK_MS && s_ackedPhoto != s_photoId;) delay(10);
    }
    free(jpg);
    Serial.printf("[PHOTO] #%u, %u bytes, %s\n", (unsigned)s_photoId, (unsigned)len, s_ackedPhoto == s_photoId ? "ok" : "no ack");
    if (s_ackedPhoto != s_photoId) { showError("The laptop did not get the photo", State::Preview); return; }
    enter(State::Menu);
}

static void chooseFilter(const Filter& f) {
    char json[96];
    snprintf(json, sizeof(json), "{\"event\":\"filter\",\"photo\":%u,\"name\":\"%s\"}", (unsigned)s_photoId, f.id);
    s_bmpReady = false;
    s_filterLabel = f.label;
    portENTER_CRITICAL(&s_mux);
    snprintf(s_status, sizeof(s_status), "Applying %s...", f.label);
    s_statusChanged = true;
    s_errorArrived = false;
    portEXIT_CRITICAL(&s_mux);
    sendEvent(json);
    enter(State::Working);
    setBaseLeds(LED_ON / 2, 0, LED_ON / 2);
}

static void doPrint() {
    enter(State::Printing);
    message("Printing... (looking for the printer)", TFT_WHITE);
    setBaseLeds(0, 0, LED_ON);
    String err;
    const bool ok = printerPrint(s_bmp, s_bmpH, printProgress, err);
    char json[192];
    snprintf(json, sizeof(json), "{\"event\":\"printed\",\"ok\":%s,\"error\":\"%s\"}", ok ? "true" : "false", ok ? "" : err.c_str());
    sendEvent(json);
    enter(State::Result);
    if (ok) {
        M5.Speaker.tone(4000, 80);
        setBaseLeds(0, LED_ON, 0);
        message("Printed! Take your photo", TFT_GREEN);
        delay(1500);
        drawResult();
    } else {
        showError(err.c_str(), State::Result);
    }
}

// --------------------------------------------------------------------------- main
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // printer + Wi-Fi + flash peaks: don't reset on a dip
    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.begin(115200);
    lcd.setBrightness(BRIGHTNESS);
    lcd.setFont(&fonts::efontJA_16);   // accents and ñ

    s_photo = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    s_bmp = (uint8_t*)heap_caps_malloc(MAX_HEIGHT * PRINTER_ROW_BYTES, MALLOC_CAP_SPIRAM);
    s_view.setPsram(true);
    s_view.setColorDepth(16);
    s_view.createSprite(W, H - BAR_H);

    const bool base = boardInit();
    setBaseLeds(0, 0, 0);
    const bool cam = cameraInit();
    printerInit();
    Serial.printf("\n[Demo 07 photo booth] base:%s camera:%s\n", base ? "ok" : "NO", cam ? "ok" : "ERROR");
    if (!cam) { lcd.setTextColor(TFT_RED); lcd.println("ERROR: camera not detected"); for (;;) delay(1000); }

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("stackchan");
    WiFi.setSleep(true);   // mandatory with Bluetooth on: without modem sleep the Wi-Fi driver aborts
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    enter(State::Preview);
}

void loop() {
    M5.update();
    if (!s_linkStarted && WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] connected, IP %s\n", WiFi.localIP().toString().c_str());
        linkStart();
        linkSetCommandHandler(onCommand);
        linkSetMessageHandler(onMessage);
        s_linkStarted = true;
    }
    const bool tap = M5.Touch.getCount() && M5.Touch.getDetail().wasPressed();
    const int tx = tap ? M5.Touch.getDetail().x : -1, ty = tap ? M5.Touch.getDetail().y : -1;

    switch (s_state) {
        case State::Preview:
            drawLivePreview(nullptr);
            if (tap) {
                s_countFrom = tx < W / 2 ? 3 : 5;
                enter(State::Countdown);
            }
            break;

        case State::Countdown: {
            const uint32_t elapsed = millis() - s_stateMs;
            const int left = s_countFrom - elapsed / 1000;
            static int lastBeep = -1;
            if (left != lastBeep) { lastBeep = left; if (left > 0) M5.Speaker.tone(left == 1 ? 2000 : 1200, 80); }
            if (left <= 0) { lastBeep = -1; takePhoto(); break; }
            drawLivePreview(String(left).c_str());
            break;
        }

        case State::Menu:
            if (!tap) break;
            if (ty >= FILTER_Y) {
                const int i = min(1, (ty - FILTER_Y) / FILTER_H) * FILTER_COLS + min(FILTER_COLS - 1, tx / FILTER_W);
                if (i < FILTER_COUNT) chooseFilter(FILTERS[i]);   // the 8th cell is empty
            } else if (tx >= 160 && ty >= 64) enter(State::Preview);
            break;

        case State::Working: {
            if (s_statusChanged) {
                s_statusChanged = false;
                char text[96];
                portENTER_CRITICAL(&s_mux);
                strlcpy(text, s_status, sizeof(text));
                portEXIT_CRITICAL(&s_mux);
                message(text, TFT_WHITE);
            }
            if (s_bmpAckDue) {
                s_bmpAckDue = false;
                char json[64];
                snprintf(json, sizeof(json), "{\"event\":\"bitmap_ok\",\"job\":%u}", (unsigned)s_bmpJob);
                sendEvent(json);
            }
            if (s_bmpReady) {
                setBaseLeds(0, 0, 0);
                M5.Speaker.tone(2500, 40);
                enter(State::Result);
            } else if (s_errorArrived) {
                s_errorArrived = false;
                char text[128];
                portENTER_CRITICAL(&s_mux);
                strlcpy(text, s_error, sizeof(text));
                portEXIT_CRITICAL(&s_mux);
                showError(text, State::Menu);
            } else if (millis() - s_stateMs > WORK_TIMEOUT_MS) {
                showError("The laptop is not answering", State::Menu);
            }
            break;
        }

        case State::Result:
            if (s_bmpAckDue) {   // a resend of the same image: confirm again
                s_bmpAckDue = false;
                char json[64];
                snprintf(json, sizeof(json), "{\"event\":\"bitmap_ok\",\"job\":%u}", (unsigned)s_bmpJob);
                sendEvent(json);
            }
            if (!tap || ty < H - BAR_H) break;
            if (tx < 120) doPrint();
            else if (tx < 220) enter(State::Menu);
            else enter(State::Preview);
            break;

        default:
            break;
    }
    delay(5);
}
