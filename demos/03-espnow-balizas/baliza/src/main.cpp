// Demo 03 - Baliza ESP-NOW.
//
// Emite un mensaje ESP-NOW por broadcast 10 veces por segundo. Quien lo reciba
// (el Stack-chan) mide su intensidad (RSSI) y con tres balizas estima dónde está.
// Si el receptor le dice que es la baliza más cercana, enciende su LED.
//
// ESP-NOW no necesita router, pero emisor y receptor tienen que estar en el
// MISMO canal. El Stack-chan está conectado a la Wi-Fi, así que la baliza busca
// esa red al arrancar y se pone en su canal.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "espnow_msg.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Falta config/wifi_secrets.h: ejecuta ./setup.sh --wifi"
#endif

#ifndef BEACON_ID
#define BEACON_ID 1
#endif
#ifndef LED_PIN
#define LED_PIN 2            // LED azul de casi todas las DevKit
#endif

static constexpr uint32_t PERIOD_MS = 100;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static volatile bool s_ledOn = false;
static volatile uint32_t s_ledUntil = 0;

static int findChannel() {
    Serial.printf("Buscando la red \"%s\" para usar su canal...\n", WIFI_SSID);
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
        s_ledUntil = millis() + 1500;   // se apaga solo si el receptor deja de hablarnos
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    int channel = 0;
    while ((channel = findChannel()) == 0) {
        Serial.println("No encuentro la red. Reintento en 3 s (¿está a 2,4 GHz y encendida?)");
        for (int i = 0; i < 6; i++) { digitalWrite(LED_PIN, i % 2); delay(500); }
    }
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_max_tx_power(78);   // 19.5 dBm: siempre la misma potencia, para que el RSSI sea comparable

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

    Serial.printf("Baliza %d lista: canal %d, MAC %s\n", BEACON_ID, channel, WiFi.macAddress().c_str());
}

void loop() {
    static uint32_t seq = 0, last = 0;
    uint32_t now = millis();
    if (now - last >= PERIOD_MS) {
        last = now;
        BeaconMsg m = { ESPNOW_MAGIC0, ESPNOW_MAGIC1, MSG_BEACON, BEACON_ID, seq++, now };
        esp_now_send(BROADCAST, (const uint8_t*)&m, sizeof(m));
    }
    // LED: fijo si es la más cercana; si no, un destello corto cada segundo ("estoy viva").
    bool on = (s_ledOn && (int32_t)(s_ledUntil - now) > 0) || (now % 1000) < 30;
    digitalWrite(LED_PIN, on);
    if (Serial.available() && Serial.read() == 'i') {
        Serial.printf("Baliza %d, canal %d, enviados %u\n", BEACON_ID, WiFi.channel(), seq);
    }
    delay(5);
}
