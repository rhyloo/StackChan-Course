// Mensajes ESP-NOW de la demo 03. Lo comparten baliza/ y receptor/.
#pragma once
#include <stdint.h>

static constexpr uint8_t ESPNOW_MAGIC0 = 'S', ESPNOW_MAGIC1 = 'B';

enum : uint8_t {
    MSG_BEACON = 1,   // baliza -> todos (broadcast), 10 veces por segundo
    MSG_LED = 2,      // receptor -> baliza: enciende/apaga su LED
};

struct __attribute__((packed)) BeaconMsg {
    uint8_t magic0, magic1, type;   // 'S', 'B', MSG_BEACON
    uint8_t id;                     // 1, 2 o 3
    uint32_t seq;
    uint32_t uptimeMs;
};

struct __attribute__((packed)) LedMsg {
    uint8_t magic0, magic1, type;   // 'S', 'B', MSG_LED
    uint8_t on;
};
