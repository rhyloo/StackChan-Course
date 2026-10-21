// Demo 12 - Mini TV: the robot plays videos from its microSD card, like a tiny old TV.
//
// The laptop (laptop/tv.py) turns any video into a .tv file and uploads it over Wi-Fi;
// the robot saves it in /tv on the SD card. Every file in /tv is a channel:
//   tap               next channel, with static in between
//   hold              pause / resume
//   tap the top band  volume: left half down, right half up
//
// .tv file (little endian): a 128-byte header, then one chunk per frame.
//   header  "STV1", u16 width, u16 height, u16 fps, u16 audio rate, u32 frames, char title[112]
//   chunk   u32 JPEG bytes, u32 audio bytes, the JPEG, the audio (16-bit mono PCM)
// The sound sets the pace: a frame is read when the speaker has room for its audio, and
// a frame that comes late is not drawn, so the picture never falls behind the sound.
//
// Upload server (port 80, also stackchan-tv.local):
//   GET  /list                 one line per channel: name, bytes, frames, fps, title
//   GET  /info                 SD card size and space used, in MB
//   POST /upload?size=N        multipart file "<name>.tv"
//   POST /delete?name=X.tv
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <vector>
#include <algorithm>
#include "board.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static constexpr int      SD_SCK = 36, SD_MISO = 35, SD_MOSI = 37, SD_CS = 4;   // shared with the screen
static constexpr const char* TV_DIR   = "/tv";
static constexpr const char* HOSTNAME = "stackchan-tv";
static constexpr int      W = 320, H = 240;
static constexpr size_t   HEADER_BYTES = 128;
static constexpr size_t   JPEG_MAX  = 64 * 1024;
static constexpr size_t   AUDIO_MAX = 4096;          // samples per frame (16 kHz at 4 fps)
static constexpr int      AUDIO_SLOTS = 3;           // one playing, one queued, one being filled
static constexpr int      VOLUME_DEFAULT = 120, VOLUME_MAX = 160, VOLUME_STEP = 20;
static constexpr int      TOP_BAND = 40;             // volume touch zone
static constexpr uint32_t OSD_MS = 2500;
static constexpr uint32_t STATIC_MS = 450;
static constexpr size_t   UPLOAD_BUF = 32 * 1024;    // SD writes in big blocks are much faster

struct Channel {
    String path, title;
    uint32_t frames = 0, bytes = 0;
    uint16_t width = 0, height = 0, fps = 0, rate = 0;
};

static auto& lcd = M5.Display;
static WebServer s_server(80);
static std::vector<Channel> s_channels;
static bool s_sdOk = false, s_serverStarted = false;

// playback
static File s_file;
static int s_ch = -1;
static uint32_t s_frame = 0, s_t0 = 0;
static bool s_paused = false;
static uint8_t* s_jpeg = nullptr;
static int16_t s_audio[AUDIO_SLOTS][AUDIO_MAX];
static int s_slot = 0;
static int16_t s_noise[2048];
static int s_volume = VOLUME_DEFAULT;
static uint32_t s_osdUntil = 0, s_volUntil = 0;
static uint32_t s_statDrawn = 0, s_statSkipped = 0, s_statMs = 0;

// upload
static File s_upFile;
static String s_upPath;
static uint8_t* s_upBuf = nullptr;
static size_t s_upLen = 0;
static uint32_t s_upBytes = 0, s_upTotal = 0, s_upDrawn = 0;
static String s_upError;

// ---------------------------------------------------------------- channels
static bool readHeader(File& f, Channel& c) {
    uint8_t h[HEADER_BYTES];
    if (f.read(h, HEADER_BYTES) != HEADER_BYTES || memcmp(h, "STV1", 4) != 0) return false;
    memcpy(&c.width, h + 4, 2); memcpy(&c.height, h + 6, 2);
    memcpy(&c.fps, h + 8, 2);   memcpy(&c.rate, h + 10, 2);
    memcpy(&c.frames, h + 12, 4);
    char title[113] = {};
    memcpy(title, h + 16, 112);
    c.title = title;
    return c.fps > 0 && c.rate > 0 && c.width <= W && c.height <= H;
}

static void scanChannels() {
    s_channels.clear();
    if (!s_sdOk) return;
    if (!SD.exists(TV_DIR)) SD.mkdir(TV_DIR);
    File dir = SD.open(TV_DIR);
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = f.name();
        if (!f.isDirectory() && name.endsWith(".tv")) {
            Channel c;
            c.path = String(TV_DIR) + "/" + name.substring(name.lastIndexOf('/') + 1);
            c.bytes = f.size();
            if (readHeader(f, c)) s_channels.push_back(c);
            else Serial.printf("[TV] %s: not a .tv file, skipped\n", c.path.c_str());
        }
        f.close();
    }
    dir.close();
    std::sort(s_channels.begin(), s_channels.end(), [](const Channel& a, const Channel& b) { return a.path < b.path; });
    Serial.printf("[TV] %u channels on the SD card\n", (unsigned)s_channels.size());
}

// ---------------------------------------------------------------- screens
static void drawCentered(const char* s, int y, uint16_t color, const lgfx::IFont* font = &fonts::efontJA_16) {
    lcd.setFont(font);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(color, TFT_BLACK);
    lcd.drawString(s, W / 2, y);
}

static String robotAddress() {
    if (WiFi.status() != WL_CONNECTED) return "Connecting to Wi-Fi...";
    return WiFi.localIP().toString() + "  (" + HOSTNAME + ".local)";
}

static void drawIdle() {
    lcd.fillScreen(TFT_BLACK);
    drawCentered("Mini TV", 40, TFT_WHITE, &fonts::FreeSansBold18pt7b);
    if (!s_sdOk) {
        drawCentered("No microSD card", 100, TFT_ORANGE);
        drawCentered("Insert a FAT32 card and restart", 126, TFT_LIGHTGREY);
    } else {
        drawCentered("No videos on the SD card yet", 100, TFT_ORANGE);
        drawCentered("On the laptop:", 132, TFT_LIGHTGREY);
        drawCentered("demos/12-mini-tv/run.sh sample", 156, TFT_WHITE);
    }
    drawCentered(robotAddress().c_str(), 214, TFT_DARKGREY);
}

static void drawOsd() {
    if (s_ch < 0) return;
    const uint32_t now = millis();
    if (now < s_osdUntil) {
        lcd.setFont(&fonts::FreeSansBold18pt7b);
        lcd.setTextDatum(top_right);
        lcd.setTextColor(TFT_GREEN);
        lcd.drawString(("CH " + String(s_ch + 1)).c_str(), W - 8, 6);
        lcd.setFont(&fonts::efontJA_12);
        lcd.setTextDatum(bottom_left);
        lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        lcd.drawString(s_channels[s_ch].title.c_str(), 4, H - 4);
    }
    if (now < s_volUntil) {
        const int w = 120 * s_volume / VOLUME_MAX;
        lcd.drawRect(8, 10, 122, 14, TFT_WHITE);
        lcd.fillRect(9, 11, w, 12, TFT_GREEN);
        lcd.fillRect(9 + w, 11, 120 - w, 12, TFT_BLACK);
    }
    if (s_paused) {
        lcd.fillRect(W / 2 - 18, H / 2 - 22, 12, 44, TFT_WHITE);
        lcd.fillRect(W / 2 + 6, H / 2 - 22, 12, 44, TFT_WHITE);
    }
}

// Old-TV static between channels: grey bands on screen and hiss on the speaker.
static void showStatic(uint32_t ms) {
    M5.Speaker.stop(0);
    M5.Speaker.playRaw(s_noise, 2048, 16000, false, 1 + ms * 16000 / 1000 / 2048, 0);
    const uint32_t end = millis() + ms;
    while (millis() < end) {
        lcd.startWrite();
        for (int y = 0; y < H; y += 2) {
            const uint8_t g = random(40, 230);
            lcd.fillRect(0, y, W, 2, lcd.color565(g, g, g));
        }
        lcd.endWrite();
    }
    M5.Speaker.stop(0);
}

// ---------------------------------------------------------------- playback
static void stopPlayback() {
    if (s_file) s_file.close();
    M5.Speaker.stop(0);
    s_ch = -1;
}

static void openChannel(int i, bool withStatic) {
    stopPlayback();
    if (s_channels.empty()) { drawIdle(); return; }
    i = (i % (int)s_channels.size() + s_channels.size()) % s_channels.size();
    if (withStatic) showStatic(STATIC_MS);
    s_file = SD.open(s_channels[i].path);
    if (!s_file || !s_file.seek(HEADER_BYTES)) {
        Serial.printf("[TV] can't open %s\n", s_channels[i].path.c_str());
        drawIdle();
        return;
    }
    s_ch = i;
    s_frame = 0;
    s_t0 = millis();
    s_paused = false;
    s_osdUntil = millis() + OSD_MS;
    const Channel& c = s_channels[i];
    lcd.fillScreen(TFT_BLACK);
    Serial.printf("[TV] CH %d %s: %ux%u, %u fps, %u frames, %s\n", i + 1, c.path.c_str(), c.width, c.height, c.fps,
                  (unsigned)c.frames, c.title.c_str());
}

// One frame, if the speaker has room for its sound. Returns without waiting otherwise.
static void playStep() {
    if (s_ch < 0 || s_paused || M5.Speaker.isPlaying(0) == 2) return;
    const Channel& c = s_channels[s_ch];
    uint32_t sizes[2];
    if (s_file.read((uint8_t*)sizes, 8) != 8) {   // end of the video: start again
        s_file.seek(HEADER_BYTES);
        s_frame = 0;
        s_t0 = millis();
        return;
    }
    const uint32_t frameMs = 1000 / c.fps;
    const uint32_t due = s_t0 + s_frame * 1000 / c.fps, now = millis();
    if (now > due + 500) s_t0 = now - s_frame * 1000 / c.fps;   // paused or stalled: catch up
    const bool late = now > due + 2 * frameMs;

    const bool draw = !late && sizes[0] <= JPEG_MAX;
    if (draw) s_file.read(s_jpeg, sizes[0]);
    else s_file.seek(s_file.position() + sizes[0]);

    const size_t samples = std::min<size_t>(sizes[1] / 2, AUDIO_MAX);
    s_file.read((uint8_t*)s_audio[s_slot], samples * 2);
    if (sizes[1] > samples * 2) s_file.seek(s_file.position() + sizes[1] - samples * 2);
    if (samples) {
        M5.Speaker.playRaw(s_audio[s_slot], samples, c.rate, false, 1, 0);
        s_slot = (s_slot + 1) % AUDIO_SLOTS;
    }

    if (draw) {
        lcd.drawJpg(s_jpeg, sizes[0], (W - c.width) / 2, (H - c.height) / 2);
        drawOsd();
        s_statDrawn++;
    } else {
        s_statSkipped++;
    }
    s_frame++;
    if (now - s_statMs >= 5000) {
        Serial.printf("[TV] CH %d frame %u/%u: drawn %.1f fps, %u skipped\n", s_ch + 1, (unsigned)s_frame,
                      (unsigned)c.frames, s_statDrawn * 1000.0f / (now - s_statMs), (unsigned)s_statSkipped);
        s_statMs = now; s_statDrawn = s_statSkipped = 0;
    }
}

static void togglePause() {
    if (s_ch < 0) return;
    s_paused = !s_paused;
    if (s_paused) { M5.Speaker.stop(0); drawOsd(); }
    else s_t0 = millis() - s_frame * 1000 / s_channels[s_ch].fps;
    Serial.printf("[TV] %s\n", s_paused ? "paused" : "playing");
}

static void changeVolume(int delta) {
    s_volume = constrain(s_volume + delta, 0, VOLUME_MAX);
    M5.Speaker.setVolume(s_volume);
    s_volUntil = millis() + OSD_MS;
    if (s_paused || s_ch < 0) drawOsd();
    Serial.printf("[TV] volume %d\n", s_volume);
}

// ---------------------------------------------------------------- upload server
static bool validName(const String& n) {
    if (n.length() < 4 || n.length() > 48 || !n.endsWith(".tv")) return false;
    for (char ch : n) if (!isalnum((unsigned char)ch) && ch != '-' && ch != '_' && ch != '.') return false;
    return true;
}

static void drawUpload() {
    lcd.fillRect(0, 90, W, 70, TFT_BLACK);
    drawCentered(("Receiving " + s_upPath.substring(strlen(TV_DIR) + 1)).c_str(), 100, TFT_WHITE);
    String p = String(s_upBytes / 1024) + " KB";
    if (s_upTotal) {
        p += " of " + String(s_upTotal / 1024) + " KB";
        lcd.drawRect(30, 136, W - 60, 14, TFT_WHITE);
        lcd.fillRect(31, 137, (uint64_t)(W - 62) * std::min(s_upBytes, s_upTotal) / s_upTotal, 12, TFT_GREEN);
    }
    drawCentered(p.c_str(), 122, TFT_LIGHTGREY);
}

static void flushUpload() {
    if (s_upLen && s_upFile && s_upFile.write(s_upBuf, s_upLen) != s_upLen) s_upError = "SD card full or write error";
    s_upLen = 0;
}

static void handleUploadData() {
    HTTPUpload& up = s_server.upload();
    if (up.status == UPLOAD_FILE_START) {
        stopPlayback();
        s_upError = "";
        s_upBytes = s_upLen = 0;
        s_upTotal = s_server.arg("size").toInt();
        String name = up.filename.substring(up.filename.lastIndexOf('/') + 1);
        if (!s_sdOk) { s_upError = "no SD card"; return; }
        if (!validName(name)) { s_upError = "bad name (letters, digits, - _ . and ending in .tv)"; return; }
        s_upPath = String(TV_DIR) + "/" + name;
        SD.remove(s_upPath + ".part");
        s_upFile = SD.open(s_upPath + ".part", FILE_WRITE);
        if (!s_upFile) { s_upError = "can't create the file on the SD card"; return; }
        Serial.printf("[UPLOAD] %s, %u bytes\n", s_upPath.c_str(), (unsigned)s_upTotal);
        lcd.fillScreen(TFT_BLACK);
        drawCentered("Mini TV", 40, TFT_WHITE, &fonts::FreeSansBold18pt7b);
        s_upDrawn = 0;
        drawUpload();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!s_upFile || s_upError.length()) return;
        size_t off = 0;
        while (off < up.currentSize) {
            const size_t n = std::min(up.currentSize - off, UPLOAD_BUF - s_upLen);
            memcpy(s_upBuf + s_upLen, up.buf + off, n);
            s_upLen += n; off += n;
            if (s_upLen == UPLOAD_BUF) flushUpload();
        }
        s_upBytes += up.currentSize;
        if (s_upBytes - s_upDrawn >= 256 * 1024) { s_upDrawn = s_upBytes; drawUpload(); }
    } else if (up.status == UPLOAD_FILE_END) {
        if (!s_upFile) return;
        flushUpload();
        s_upFile.close();
        if (s_upError.length()) { SD.remove(s_upPath + ".part"); return; }
        SD.remove(s_upPath);
        SD.rename(s_upPath + ".part", s_upPath);
        Serial.printf("[UPLOAD] %s saved, %u bytes\n", s_upPath.c_str(), (unsigned)s_upBytes);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (s_upFile) s_upFile.close();
        SD.remove(s_upPath + ".part");
        s_upError = "upload aborted";
        Serial.println("[UPLOAD] aborted");
    }
}

static void handleUploadDone() {
    if (s_upError.length()) {
        s_server.send(400, "text/plain", s_upError + "\n");
        Serial.printf("[UPLOAD] error: %s\n", s_upError.c_str());
    } else {
        s_server.send(200, "text/plain", "ok " + s_upPath + " " + String(s_upBytes) + "\n");
    }
    scanChannels();
    int i = 0;
    for (size_t k = 0; k < s_channels.size(); k++) if (s_channels[k].path == s_upPath) i = k;
    openChannel(i, true);   // show the new channel
}

static void handleList() {
    String out;
    for (const Channel& c : s_channels)
        out += c.path.substring(strlen(TV_DIR) + 1) + "\t" + c.bytes + "\t" + c.frames + "\t" + c.fps + "\t" + c.title + "\n";
    s_server.send(200, "text/plain", out);
}

static void handleInfo() {
    if (!s_sdOk) { s_server.send(503, "text/plain", "no SD card\n"); return; }
    s_server.send(200, "text/plain", String((uint32_t)(SD.totalBytes() >> 20)) + " " + String((uint32_t)(SD.usedBytes() >> 20)) + "\n");
}

static void handleDelete() {
    const String name = s_server.arg("name");
    if (!validName(name)) { s_server.send(400, "text/plain", "bad name\n"); return; }
    const String path = String(TV_DIR) + "/" + name;
    const bool playing = s_ch >= 0 && s_channels[s_ch].path == path;
    if (playing) stopPlayback();
    const bool ok = SD.remove(path);
    s_server.send(ok ? 200 : 404, "text/plain", ok ? "deleted\n" : "not found\n");
    Serial.printf("[TV] delete %s: %s\n", path.c_str(), ok ? "ok" : "not found");
    scanChannels();
    if (playing || s_channels.empty()) openChannel(0, false);
}

// ---------------------------------------------------------------- main
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    lcd.setBrightness(179);
    // 5 V to the base; the servos stay unpowered: the TV doesn't move its head.
    if (!boardInit(false)) Serial.println("Base K151 not answering (PY32)");
    setBaseLeds(0, 0, 0);

    M5.Mic.end();   // the mic and the speaker share I2S
    M5.Speaker.begin();
    M5.Speaker.setVolume(s_volume);
    for (auto& s : s_noise) s = random(-2500, 2500);

    s_jpeg = (uint8_t*)ps_malloc(JPEG_MAX);
    s_upBuf = (uint8_t*)ps_malloc(UPLOAD_BUF);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    s_sdOk = SD.begin(SD_CS, SPI, 25000000);
    if (s_sdOk) Serial.printf("[SD] %llu MB, %llu MB used\n", SD.totalBytes() >> 20, SD.usedBytes() >> 20);
    else Serial.println("[SD] no card (or not FAT32)");
    scanChannels();

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    s_server.on("/list", HTTP_GET, handleList);
    s_server.on("/info", HTTP_GET, handleInfo);
    s_server.on("/delete", HTTP_POST, handleDelete);
    s_server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);

    Serial.println("\n[Demo 12 mini TV] tap: next channel, hold: pause, top band: volume");
    openChannel(0, !s_channels.empty());
}

void loop() {
    M5.update();
    auto t = M5.Touch.getDetail();
    if (t.wasClicked()) {
        if (t.y < TOP_BAND) changeVolume(t.x < W / 2 ? -VOLUME_STEP : VOLUME_STEP);
        else if (!s_channels.empty()) openChannel(s_ch + 1, true);
    } else if (t.wasHold()) {
        togglePause();
    }

    if (!s_serverStarted && WiFi.status() == WL_CONNECTED) {
        s_serverStarted = true;
        MDNS.begin(HOSTNAME);
        MDNS.addService("http", "tcp", 80);
        s_server.begin();
        Serial.printf("[WIFI] connected, IP %s, http://%s.local\n", WiFi.localIP().toString().c_str(), HOSTNAME);
        if (s_ch < 0) drawIdle();   // now with the address
    }
    if (s_serverStarted) s_server.handleClient();

    playStep();
    if (s_ch >= 0 && !s_paused) {
        // the OSD is drawn over each frame; when it times out, clear what a smaller video doesn't cover
        static bool osdShown = false;
        const bool on = millis() < s_osdUntil || millis() < s_volUntil;
        if (osdShown && !on) lcd.fillScreen(TFT_BLACK);
        osdShown = on;
    }
    delay(1);
}
