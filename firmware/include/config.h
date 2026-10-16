#pragma once
#include <stdint.h>

#define FW_VERSION "1.0.0"

// --- Network (see PROTOCOL.md) ---
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

// --- Head limits in radians (same values the phone app uses) ---
static constexpr float PAN_MIN  = -2.2689f;   // -130.0 deg
static constexpr float PAN_MAX  =  2.7925f;   // +160.0 deg
static constexpr float TILT_MIN = -0.050f;    //   -2.8 deg (chin hits the base below this)
static constexpr float TILT_MAX =  0.480f;    //  +27.5 deg

// --- Servo calibration (Feetech SCS0009, 3.2 steps/deg) ---
static constexpr int   PAN_CENTER_RAW  = 460;
static constexpr int   TILT_CENTER_RAW = 620;
static constexpr float STEPS_PER_RAD   = 183.3465f;
static constexpr int   PAN_RAW_MIN = 44,  PAN_RAW_MAX = 972;
static constexpr int   TILT_RAW_MIN = 611, TILT_RAW_MAX = 708;
static constexpr uint32_t SERVO_PERIOD_MS = 40;   // 25 Hz servo loop
static constexpr int   PAN_MAX_STEP  = 12;        // per tick: ~94 deg/s
static constexpr int   TILT_MAX_STEP = 6;         // per tick: ~47 deg/s
