// ESP-NOW messages of demo 03. Shared by beacon/ and receiver/.
#pragma once
#include <stdint.h>

static constexpr uint8_t ESPNOW_MAGIC0 = 'S', ESPNOW_MAGIC1 = 'B';

enum : uint8_t {
    MSG_BEACON = 1,   // beacon -> everyone (broadcast), 10 times per second
    MSG_LED = 2,      // receiver -> beacon: turn its LED on/off
};

struct __attribute__((packed)) BeaconMsg {
    uint8_t magic0, magic1, type;   // 'S', 'B', MSG_BEACON
    uint8_t id;                     // 1, 2 or 3
    uint32_t seq;
    uint32_t uptimeMs;
};

struct __attribute__((packed)) LedMsg {
    uint8_t magic0, magic1, type;   // 'S', 'B', MSG_LED
    uint8_t on;
};
