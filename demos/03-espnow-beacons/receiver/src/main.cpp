// Demo 03 - ESP-NOW receiver on Stack-chan.
//
// 1. Connects to the Wi-Fi (so it is on the same channel as the beacons and can
//    talk to the laptop).
// 2. Listens to the beacons' ESP-NOW messages and measures their RSSI.
// 3. Draws one bar per beacon on screen, colours the base LEDs with the
//    closest one and asks it over ESP-NOW to turn its LED on.
// 4. Sends the readings to the laptop (UDP 5602), which estimates the position.
//
// The ESP-NOW callback in Arduino-ESP32 2.x has no RSSI, so we get it
// from promiscuous mode: we see the whole 802.11 frame, header included.
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "board.h"
#include "espnow_msg.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi"
#endif

static constexpr uint16_t LAPTOP_PORT = 5602;
static constexpr int MAX_BEACONS = 3;
static constexpr uint32_t STALE_MS = 2000;     // no news from a beacon in 2 s = lost
static constexpr float RSSI_ALPHA = 0.2f;      // smoothing: the RSSI jumps several dB between packets

struct Beacon {
    uint8_t mac[6];
    float rssi;          // exponential average, dBm
    int lastRaw;
    uint32_t count;
    uint32_t lastMs;
    bool seen;
};
static Beacon s_beacons[MAX_BEACONS + 1];    // index = id (1..3)
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// Last RSSI seen per source MAC (promiscuous mode arrives before ESP-NOW).
struct RssiSeen { uint8_t mac[6]; int8_t rssi; };
static RssiSeen s_rssiByMac[8];
static int s_rssiNext = 0;

static void IRAM_ATTR onPromiscuous(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    auto* pkt = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* f = pkt->payload;
    // Action frame (0xD0), category 127 "vendor", Espressif OUI = ESP-NOW.
    if (pkt->rx_ctrl.sig_len < 28 || f[0] != 0xD0 || f[24] != 127 || f[25] != 0x18 || f[26] != 0xFE || f[27] != 0x34) return;
    portENTER_CRITICAL_ISR(&s_mux);
    for (auto& r : s_rssiByMac) {
        if (!memcmp(r.mac, f + 10, 6)) { r.rssi = pkt->rx_ctrl.rssi; portEXIT_CRITICAL_ISR(&s_mux); return; }
    }
    memcpy(s_rssiByMac[s_rssiNext].mac, f + 10, 6);
    s_rssiByMac[s_rssiNext].rssi = pkt->rx_ctrl.rssi;
    s_rssiNext = (s_rssiNext + 1) % 8;
    portEXIT_CRITICAL_ISR(&s_mux);
}

static void onEspNow(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(BeaconMsg) || data[0] != ESPNOW_MAGIC0 || data[1] != ESPNOW_MAGIC1 || data[2] != MSG_BEACON) return;
    const BeaconMsg* m = (const BeaconMsg*)data;
    if (m->id < 1 || m->id > MAX_BEACONS) return;
    int rssi = 0;
    portENTER_CRITICAL(&s_mux);
    for (auto& r : s_rssiByMac) if (!memcmp(r.mac, mac, 6)) rssi = r.rssi;
    Beacon& b = s_beacons[m->id];
    if (rssi != 0) {
        b.rssi = b.seen && millis() - b.lastMs < STALE_MS ? b.rssi + RSSI_ALPHA * (rssi - b.rssi) : rssi;
        b.lastRaw = rssi;
        memcpy(b.mac, mac, 6);
        b.count++;
        b.lastMs = millis();
        b.seen = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

static bool alive(const Beacon& b) { return b.seen && millis() - b.lastMs < STALE_MS; }

static int closest(Beacon* snap) {
    int best = 0;
    for (int i = 1; i <= MAX_BEACONS; i++)
        if (alive(snap[i]) && (best == 0 || snap[i].rssi > snap[best].rssi)) best = i;
    return best;
}

static void tellBeaconLed(const Beacon& b, bool on) {
    if (!esp_now_is_peer_exist(b.mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, b.mac, 6);
        peer.channel = 0;          // the current channel (the Wi-Fi one)
        peer.ifidx = WIFI_IF_STA;
        esp_now_add_peer(&peer);
    }
    LedMsg m = { ESPNOW_MAGIC0, ESPNOW_MAGIC1, MSG_LED, (uint8_t)on };
    esp_now_send(b.mac, (const uint8_t*)&m, sizeof(m));
}

static const uint16_t COLORS[MAX_BEACONS + 1] = { TFT_DARKGREY, TFT_RED, TFT_GREEN, TFT_BLUE };
static const uint8_t LED_RGB[MAX_BEACONS + 1][3] = { {0, 0, 0}, {60, 0, 0}, {0, 60, 0}, {0, 0, 60} };

static void draw(Beacon* snap, int best) {
    static M5Canvas c(&M5.Display);
    if (!c.width()) { c.setColorDepth(16); c.createSprite(320, 240); }
    c.fillSprite(TFT_BLACK);
    c.setTextColor(TFT_WHITE);
    c.setTextSize(2);
    c.setCursor(8, 6);
    c.print("ESP-NOW beacons");
    c.setTextSize(1);
    c.setCursor(8, 28);
    c.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_LIGHTGREY : TFT_ORANGE);
    c.printf("%s  channel %d  %s", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "no Wi-Fi",
             WiFi.channel(), WIFI_SSID);
    for (int i = 1; i <= MAX_BEACONS; i++) {
        int y = 50 + (i - 1) * 60;
        c.setTextColor(COLORS[i]);
        c.setTextSize(2);
        c.setCursor(8, y);
        c.printf("%d", i);
        if (!alive(snap[i])) {
            c.setTextSize(1);
            c.setCursor(40, y + 4);
            c.setTextColor(TFT_DARKGREY);
            c.print(snap[i].seen ? "lost" : "waiting...");
            continue;
        }
        // Bar: -90 dBm (far) to -30 dBm (touching).
        int w = constrain((int)((snap[i].rssi + 90) * 240 / 60), 2, 240);
        c.fillRect(40, y, w, 22, COLORS[i]);
        if (i == best) c.drawRect(38, y - 2, 244, 26, TFT_WHITE);
        c.setTextSize(1);
        c.setTextColor(TFT_LIGHTGREY);
        c.setCursor(40, y + 28);
        c.printf("%.1f dBm  (%u packets)%s", snap[i].rssi, snap[i].count, i == best ? "  <- closest" : "");
    }
    c.pushSprite(0, 0);
}

static int s_sock = -1;

static void sendToLaptop(Beacon* snap) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (s_sock < 0) {
        s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        int yes = 1;
        setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    }
    char json[400];
    int n = snprintf(json, sizeof(json), "SV\x01\x30{\"robot\":\"%s\",\"t\":%u,\"beacons\":[", WiFi.macAddress().c_str(), (unsigned)millis());
    bool first = true;
    for (int i = 1; i <= MAX_BEACONS; i++) {
        if (!alive(snap[i])) continue;
        n += snprintf(json + n, sizeof(json) - n, "%s{\"id\":%d,\"rssi\":%.1f,\"raw\":%d,\"n\":%u}",
                      first ? "" : ",", i, snap[i].rssi, snap[i].lastRaw, snap[i].count);
        first = false;
    }
    n += snprintf(json + n, sizeof(json) - n, "]}");
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(LAPTOP_PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(s_sock, json, n, 0, (sockaddr*)&to, sizeof(to));
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(30);
    boardInit();
    setBaseLeds(0, 0, 0);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);   // with power saving the radio "sleeps" and half the packets are lost
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    M5.Display.setTextSize(2);
    M5.Display.println("Connecting to Wi-Fi...");
    for (int i = 0; i < 300 && WiFi.status() != WL_CONNECTED; i++) delay(100);

    esp_now_init();
    esp_now_register_recv_cb(onEspNow);
    esp_wifi_set_promiscuous_rx_cb(onPromiscuous);
    wifi_promiscuous_filter_t filter = { WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous(true);
    Serial.printf("Receiver ready. Wi-Fi %s, channel %d\n", WiFi.status() == WL_CONNECTED ? "connected" : "NOT connected", WiFi.channel());
}

void loop() {
    M5.update();
    static uint32_t lastUi = 0, lastLed = 0;
    static int lastBest = -1;
    uint32_t now = millis();
    if (now - lastUi < 200) { delay(5); return; }
    lastUi = now;

    Beacon snap[MAX_BEACONS + 1];
    portENTER_CRITICAL(&s_mux);
    memcpy(snap, s_beacons, sizeof(snap));
    portEXIT_CRITICAL(&s_mux);

    int best = closest(snap);
    draw(snap, best);
    sendToLaptop(snap);
    if (best != lastBest) {
        lastBest = best;
        setBaseLeds(LED_RGB[best][0], LED_RGB[best][1], LED_RGB[best][2]);
    }
    // Every 0.5 s: LED on for the closest one and off for the rest.
    if (now - lastLed >= 500) {
        lastLed = now;
        for (int i = 1; i <= MAX_BEACONS; i++)
            if (alive(snap[i])) tellBeaconLed(snap[i], i == best);
    }
}
