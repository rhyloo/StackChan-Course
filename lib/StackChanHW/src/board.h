#pragma once
#include <Arduino.h>

// Power rails, base coprocessor (PY32) and the two SCS0009 servos of the
// Stack-chan K151 base. Ported from tests-pre-curso/custom_firmware, where
// these sequences were verified on the real robot.
#include "stackchan_limits.h"

bool boardInit();                      // power rails + base coprocessor; true if the base answered
void servoInit();                      // UART1 @ 1 Mbps on GPIO 6/7, torque on, head to home

// Thread-safe head target, in radians. The servo task moves towards it with a
// slew-rate limit, so callers can set it at any rate.
void servoSetTarget(float panRad, float tiltRad);
void servoHome();                      // slow, direct move to (0, 0)
void servoSetTorque(bool on);
void servoGetCurrent(float* panRad, float* tiltRad);
void servoStartTask();                 // starts the 25 Hz servo loop

void setBaseLeds(uint8_t r, uint8_t g, uint8_t b);

// Battery current from the base's INA226 (I2C 0x41, 10 mOhm shunt).
// Positive = the battery is discharging. Call from the loop task only.
bool readBatteryCurrent(float* busVolts, float* milliamps);
