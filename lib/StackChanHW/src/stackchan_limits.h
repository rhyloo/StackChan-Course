#pragma once
#include <stdint.h>

// Mechanical limits and calibration of the Stack-chan K151 head.
// Why these values: see docs/ROBOT.md, section 4.

// --- Head limits in radians (same values the phone app uses) ---
static constexpr float PAN_MIN  = -2.2689f;   // -130.0 deg
static constexpr float PAN_MAX  =  2.7925f;   // +160.0 deg
static constexpr float TILT_MIN = -0.050f;    //   -2.8 deg (chin hits the base below this)
static constexpr float TILT_MAX =  0.480f;    //  +27.5 deg

// --- Base LEDs: maximum level per channel (0-255). See setBaseLeds().
static constexpr uint8_t LED_MAX_LEVEL = 64;

// --- Servo calibration (Feetech SCS0009, 3.2 steps/deg) ---
static constexpr int   PAN_CENTER_RAW  = 460;
static constexpr int   TILT_CENTER_RAW = 620;
static constexpr float STEPS_PER_RAD   = 183.3465f;
static constexpr int   PAN_RAW_MIN = 44,  PAN_RAW_MAX = 972;
static constexpr int   TILT_RAW_MIN = 611, TILT_RAW_MAX = 708;
static constexpr uint32_t SERVO_PERIOD_MS = 40;   // 25 Hz servo loop
static constexpr int   PAN_MAX_STEP  = 12;        // per tick: ~94 deg/s
static constexpr int   TILT_MAX_STEP = 6;         // per tick: ~47 deg/s
