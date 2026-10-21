// Demo 03 - ESP-NOW beacon.
//
// Broadcasts an ESP-NOW message 10 times per second. Whoever receives it
// (Stack-chan) measures its strength (RSSI) and, with three beacons, estimates where it is.
// If the receiver says this is the closest beacon, it turns its LED on.
//
// ESP-NOW needs no router, but sender and receiver must be on the
// SAME channel. Stack-chan is connected to the Wi-Fi, so the beacon looks for
// that network at boot and switches to its channel.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "espnow_msg.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi"
#endif

#ifndef BEACON_ID
#define BEACON_ID 1
#endif
#ifndef LED_PIN
#define LED_PIN 2            // blue LED on almost every DevKit
#endif

static constexpr uint32_t PERIOD_MS = 100;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static volatile bool s_ledOn = false;
static volatile uint32_t s_ledUntil = 0;

static int findChannel() {
    Serial.printf("Looking for network \"%s\" to use its channel...\n", WIFI_SSID);
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == WIFI_SSID) return WiFi.channel(i);
    }
    return 0;
}

static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(LedMsg) || data[0] != ESPNOW_MAGIC0 || data[1] != ESPNOW_MAGIC1) return;
    if (data[2] == MSG_LED) {
        const LedMsg* m = (const LedMsg*)data;
        s_ledOn = m->on;
        s_ledUntil = millis() + 1500;   // turns itself off if the receiver stops talking to us
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    int channel = 0;
    while ((channel = findChannel()) == 0) {
        Serial.println("Network not found. Retrying in 3 s (is it on and 2.4 GHz?)");
        for (int i = 0; i < 6; i++) { digitalWrite(LED_PIN, i % 2); delay(500); }
    }
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_max_tx_power(78);   // 19.5 dBm: always the same power, so the RSSI is comparable

    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: esp_now_init");
        ESP.restart();
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST, 6);
    peer.channel = channel;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    esp_now_register_recv_cb(onReceive);

    Serial.printf("Beacon %d ready: channel %d, MAC %s\n", BEACON_ID, channel, WiFi.macAddress().c_str());
}

void loop() {
    static uint32_t seq = 0, last = 0;
    uint32_t now = millis();
    if (now - last >= PERIOD_MS) {
        last = now;
        BeaconMsg m = { ESPNOW_MAGIC0, ESPNOW_MAGIC1, MSG_BEACON, BEACON_ID, seq++, now };
        esp_now_send(BROADCAST, (const uint8_t*)&m, sizeof(m));
    }
    // LED: steady if it is the closest; otherwise a short blink every second ("I'm alive").
    bool on = (s_ledOn && (int32_t)(s_ledUntil - now) > 0) || (now % 1000) < 30;
    digitalWrite(LED_PIN, on);
    if (Serial.available() && Serial.read() == 'i') {
        Serial.printf("Beacon %d, channel %d, sent %u\n", BEACON_ID, WiFi.channel(), seq);
    }
    delay(5);
}
