#include "link.h"
#include "config.h"
#include "board.h"
#include "camera.h"
#include <M5Unified.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <esp_heap_caps.h>

// --- Protocol (keep in sync with relay/protocol.py) ---
static constexpr uint8_t MAGIC0 = 0x53, MAGIC1 = 0x56, VERSION = 1;
static constexpr uint8_t T_HELLO = 0x20;      // robot -> laptop
static constexpr uint8_t T_ACK = 0x21;        // laptop -> robot
static constexpr uint8_t T_VIDEO = 0x22;      // robot -> laptop
static constexpr uint8_t T_TELEMETRY = 0x23;  // robot -> laptop
static constexpr uint8_t T_POSE = 0x24;       // laptop -> robot
static constexpr uint8_t T_CMD = 0x25;        // laptop -> robot

static int s_sock = -1;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static sockaddr_in s_relay = {};
static volatile uint32_t s_lastHeardMs = 0;
static volatile bool s_haveRelay = false;
static volatile uint8_t s_quality = JPEG_QUALITY_DEFAULT;
static volatile float s_battV = 0;
static volatile int s_battPct = -1;
static LinkStats s_stats = {};

static uint32_t s_lastPoseSeq = 0;
static uint32_t s_lastPoseMs = 0;

static inline void putHeader(uint8_t* b, uint8_t type) { b[0] = MAGIC0; b[1] = MAGIC1; b[2] = VERSION; b[3] = type; }
static inline void be32(uint8_t* b, uint32_t v) { b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v; }
static inline void be16(uint8_t* b, uint16_t v) { b[0] = v >> 8; b[1] = v; }
static inline uint32_t rd32(const uint8_t* b) { return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }
static inline float rdf32(const uint8_t* b) { uint32_t u = rd32(b); float f; memcpy(&f, &u, 4); return f; }

static bool relayOnline() {
    return s_haveRelay && (millis() - s_lastHeardMs) < LINK_TIMEOUT_MS;
}

static sockaddr_in relayAddr() {
    portENTER_CRITICAL(&s_mux);
    sockaddr_in a = s_relay;
    portEXIT_CRITICAL(&s_mux);
    return a;
}

// lwIP returns ENOMEM when its TX queue is full during a frame burst. Waiting
// a millisecond and retrying loses far fewer chunks than giving up.
static bool sendTo(const uint8_t* data, size_t len, const sockaddr_in& to) {
    for (int attempt = 0; attempt < 6; attempt++) {
        if (sendto(s_sock, data, len, 0, (const sockaddr*)&to, sizeof(to)) == (int)len) return true;
        if (errno != ENOMEM && errno != EAGAIN) break;
        vTaskDelay(1);
    }
    s_stats.sendErrors++;
    return false;
}

static void handleCommand(const char* cmd) {
    Serial.printf("[LINK] cmd: %s\n", cmd);
    if (!strcmp(cmd, "home")) servoHome();
    else if (!strcmp(cmd, "torque_on")) servoSetTorque(true);
    else if (!strcmp(cmd, "torque_off")) servoSetTorque(false);
    else if (!strncmp(cmd, "quality ", 8)) {
        int q = atoi(cmd + 8);
        if (q >= 5 && q <= 95) s_quality = q;
    } else if (!strncmp(cmd, "tone ", 5)) {
        int hz = 0, ms = 0;
        if (sscanf(cmd + 5, "%d %d", &hz, &ms) == 2 && hz >= 100 && hz <= 8000 && ms > 0 && ms <= 2000) {
            M5.Speaker.tone(hz, ms);
        }
    } else if (!strncmp(cmd, "leds ", 5)) {
        int r, g, b;
        if (sscanf(cmd + 5, "%d %d %d", &r, &g, &b) == 3) setBaseLeds(r, g, b);
    } else if (!strncmp(cmd, "text ", 5)) {
        portENTER_CRITICAL(&s_mux);
        strlcpy(s_stats.lastText, cmd + 5, sizeof(s_stats.lastText));
        portEXIT_CRITICAL(&s_mux);
    }
}

static void rxTask(void*) {
    static uint8_t buf[1500];
    for (;;) {
        sockaddr_in from = {};
        socklen_t fl = sizeof(from);
        int n = recvfrom(s_sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n < 4 || buf[0] != MAGIC0 || buf[1] != MAGIC1 || buf[2] != VERSION) continue;
        uint8_t type = buf[3];
        if (type != T_ACK && type != T_POSE && type != T_CMD) continue;  // e.g. other robots' HELLOs

        // Pair with the first relay that answers. While it stays online,
        // ignore other laptops on the same network (a classroom has many).
        bool online = relayOnline();
        portENTER_CRITICAL(&s_mux);
        bool other = online && (from.sin_addr.s_addr != s_relay.sin_addr.s_addr || from.sin_port != s_relay.sin_port);
        if (!other) s_relay = from;
        portEXIT_CRITICAL(&s_mux);
        if (other) continue;
        s_haveRelay = true;
        s_lastHeardMs = millis();

        switch (type) {
        case T_ACK:
            break;
        case T_POSE: {
            if (n < 16) break;
            uint32_t seq = rd32(buf + 4);
            uint32_t now = millis();
            // Drop stale/reordered packets, but accept anything after a pause
            // (the relay restarts its counter when it restarts).
            if ((int32_t)(seq - s_lastPoseSeq) <= 0 && now - s_lastPoseMs < 2000) break;
            s_lastPoseSeq = seq;
            s_lastPoseMs = now;
            servoSetTarget(rdf32(buf + 8), rdf32(buf + 12));
            s_stats.poses++;
            break;
        }
        case T_CMD:
            buf[n] = 0;
            handleCommand((const char*)buf + 4);
            break;
        }
    }
}

static void videoTask(void*) {
    static uint8_t pkt[MAX_DATAGRAM];
    uint32_t frameId = 0, fpsCount = 0, fpsStart = millis();
    for (;;) {
        if (!relayOnline() || WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        uint32_t t0 = millis();
        uint8_t* jpg = nullptr;
        size_t len = 0;
        if (!cameraCaptureJpeg(s_quality, &jpg, &len)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        uint32_t count = (len + VIDEO_PAYLOAD - 1) / VIDEO_PAYLOAD;
        if (count > 0 && count <= 1024) {
            sockaddr_in to = relayAddr();
            frameId++;
            putHeader(pkt, T_VIDEO);
            be32(pkt + 4, frameId);
            be16(pkt + 10, count);
            be32(pkt + 12, t0);
            for (uint32_t i = 0; i < count; i++) {
                size_t off = i * VIDEO_PAYLOAD;
                size_t n = len - off < VIDEO_PAYLOAD ? len - off : VIDEO_PAYLOAD;
                be16(pkt + 8, i);
                memcpy(pkt + VIDEO_HEADER, jpg + off, n);
                sendTo(pkt, VIDEO_HEADER + n, to);
            }
            s_stats.jpegBytes = len;
            fpsCount++;
        }
        free(jpg);

        uint32_t now = millis();
        if (now - fpsStart >= 1000) {
            s_stats.fps = fpsCount * 1000.0f / (now - fpsStart);
            fpsCount = 0;
            fpsStart = now;
        }
        uint32_t minFrame = 1000 / MAX_FPS;
        uint32_t spent = millis() - t0;
        vTaskDelay(pdMS_TO_TICKS(spent < minFrame ? minFrame - spent : 1));
    }
}

// HELLO (broadcast until a relay answers, then unicast) and TELEMETRY.
static void helloTask(void*) {
    static char json[300];
    static uint8_t pkt[MAX_DATAGRAM];
    uint32_t lastHello = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
        if (WiFi.status() != WL_CONNECTED) continue;
        uint32_t now = millis();

        if (now - lastHello >= HELLO_PERIOD_MS) {
            lastHello = now;
            int n = snprintf(json, sizeof(json),
                "{\"name\":\"stackchan-%s\",\"fw\":\"" FW_VERSION "\",\"ip\":\"%s\","
                "\"limits\":{\"pan\":[%.4f,%.4f],\"tilt\":[%.4f,%.4f]}}",
                robotId(), WiFi.localIP().toString().c_str(),
                PAN_MIN, PAN_MAX, TILT_MIN, TILT_MAX);
            putHeader(pkt, T_HELLO);
            memcpy(pkt + 4, json, n);
            sockaddr_in to = {};
            if (relayOnline()) {
                to = relayAddr();
            } else {
                to.sin_family = AF_INET;
                to.sin_port = htons(ROBOT_PORT);
                to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            }
            sendTo(pkt, 4 + n, to);
        }

        if (relayOnline()) {
            float pan, tilt;
            servoGetCurrent(&pan, &tilt);
            int n = snprintf(json, sizeof(json),
                "{\"batt_v\":%.2f,\"batt_pct\":%d,\"pan\":%.4f,\"tilt\":%.4f,\"fps\":%.1f,"
                "\"jpeg_bytes\":%u,\"rssi\":%d,\"heap\":%u,\"psram\":%u,\"send_errors\":%u,\"quality\":%u}",
                s_battV, s_battPct, pan, tilt, s_stats.fps, (unsigned)s_stats.jpegBytes,
                (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
                (unsigned)s_stats.sendErrors, (unsigned)s_quality);
            putHeader(pkt, T_TELEMETRY);
            memcpy(pkt + 4, json, n);
            sendTo(pkt, 4 + n, relayAddr());
        }
    }
}

void linkStart() {
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    int sndbuf = 16 * 1024;
    setsockopt(s_sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(ROBOT_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(s_sock, (sockaddr*)&local, sizeof(local));

    // Wi-Fi/lwIP run on core 0; camera encoding and servos on core 1.
    xTaskCreatePinnedToCore(rxTask, "link_rx", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(helloTask, "link_hello", 4096, nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(videoTask, "link_video", 8192, nullptr, 2, nullptr, 1);
}

void linkGetStats(LinkStats* out) {
    portENTER_CRITICAL(&s_mux);
    *out = s_stats;
    uint32_t ip = s_relay.sin_addr.s_addr;
    portEXIT_CRITICAL(&s_mux);
    out->relayOnline = relayOnline();
    IPAddress a(ip);
    strlcpy(out->relayIp, s_haveRelay ? a.toString().c_str() : "-", sizeof(out->relayIp));
}

const char* robotId() {
    static char id[5] = "";
    if (!id[0]) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(id, sizeof(id), "%02X%02X", mac[4], mac[5]);
    }
    return id;
}

void linkSetBattery(float volts, int pct) {
    s_battV = volts;
    s_battPct = pct;
}
