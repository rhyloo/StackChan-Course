#pragma once
#include <stdint.h>

// Mechanical limits and calibration of the Stack-chan K151 head.
// Why these values: see docs/ROBOT.md, section 4.

// --- Head limits in radians (same values the phone app uses) ---
// Measured on the robot with the "probe" command of demo 01 (2026-10-21), see docs/ROBOT.md.
// Pan: the base turns 360 deg. Position mode covers raw 20..1003 (the servo's own angle
// limits, read at boot): about 302 deg. The other ~58 deg (the "blind zone", behind) are
// crossed in motor mode (see board.cpp), so the robot accepts any pan in -PI..PI and
// always takes the short way round. PAN_MIN / PAN_MAX are the ends of the position arc.
static constexpr float PAN_MIN  = -2.3998f;   // -137.5 deg (raw 20)
static constexpr float PAN_MAX  =  2.9616f;   // +169.7 deg (raw 1003)
static constexpr int   PAN_CROSS_PWM = 250;   // motor-mode speed across the blind zone (~100 deg/s)
static constexpr uint32_t PAN_CROSS_TIMEOUT_MS = 3000;
// Tilt: 0 = head level. Mechanical ends at raw 582 (-12 deg, chin on the base) and 871
// (+78 deg); M5 recommends keeping 5 deg away from each end, or the servo can jam.
static constexpr float TILT_MIN = -0.1200f;   //  -6.9 deg (raw 598)
static constexpr float TILT_MAX =  1.2763f;   // +73.1 deg (raw 854)

// --- Base LEDs: maximum level per channel (0-255). See setBaseLeds().
static constexpr uint8_t LED_MAX_LEVEL = 64;

// --- Servo calibration (Feetech SCS0009, 3.2 steps/deg) ---
static constexpr int   PAN_CENTER_RAW  = 460;
static constexpr int   TILT_CENTER_RAW = 620;
static constexpr float STEPS_PER_RAD   = 183.3465f;
static constexpr int   PAN_RAW_MIN = 20,  PAN_RAW_MAX = 1003;   // = PAN_MIN / PAN_MAX
static constexpr int   TILT_RAW_MIN = 598, TILT_RAW_MAX = 854;   // = TILT_MIN / TILT_MAX
static constexpr uint32_t SERVO_PERIOD_MS = 40;   // 25 Hz servo loop
// After this long without moving, both servos are released (torque off): they don't buzz,
// heat or draw current, and the head can be turned by hand. The next move takes the head
// from wherever it is then. 0 = always hold.
static constexpr uint32_t SERVO_RELEASE_MS = 3000;
static constexpr int   PAN_MAX_STEP  = 12;        // per tick: ~94 deg/s
static constexpr int   TILT_MAX_STEP = 6;         // per tick: ~47 deg/s
