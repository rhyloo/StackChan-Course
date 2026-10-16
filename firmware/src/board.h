#pragma once
#include <Arduino.h>

// Power rails, base coprocessor (PY32) and the two SCS0009 servos of the
// Stack-chan K151 base. Ported from tests-pre-curso/custom_firmware, where
// these sequences were verified on the real robot.

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
