#pragma once
#include <stdint.h>

#define FW_VERSION "1.0.0"

// --- Network (see demos/01-vr-cardboard/PROTOCOL.md) ---
// The robot announces itself by broadcast on ROBOT_PORT until the laptop relay
// answers; from then on it talks only to that laptop.
static constexpr uint16_t ROBOT_PORT = 5600;
static constexpr uint32_t LINK_TIMEOUT_MS = 3000;   // stop video if the relay goes silent
static constexpr uint32_t HELLO_PERIOD_MS = 1000;
static constexpr uint32_t TELEMETRY_PERIOD_MS = 500;
static constexpr size_t   MAX_DATAGRAM = 1200;
static constexpr size_t   VIDEO_HEADER = 16;
static constexpr size_t   VIDEO_PAYLOAD = MAX_DATAGRAM - VIDEO_HEADER;  // 1184

// --- Camera ---
static constexpr uint8_t  JPEG_QUALITY_DEFAULT = 60;   // frame2jpg scale: 1..100, higher = better
static constexpr uint32_t MAX_FPS = 20;
