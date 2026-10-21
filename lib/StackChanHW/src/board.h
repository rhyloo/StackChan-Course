#pragma once
#include <Arduino.h>

// Power rails, base coprocessor (PY32) and the two SCS0009 servos of the
// Stack-chan K151 base. Ported from tests-pre-curso/custom_firmware, where
// these sequences were verified on the real robot.
#include "stackchan_limits.h"

// Power rails + base coprocessor; true if the base answered. servoPower = false leaves
// VM_EN off: the servos' boost converter stays off (demos that don't move the head).
bool boardInit(bool servoPower = true);
// AXP2101 REG 20h (power-on source) and 21h (last power-off source), read in boardInit().
// Bits are decoded in lib/python/stackchan/relay.py and docs/ROBOT.md.
uint8_t pmicPowerOnStatus();
uint8_t pmicPowerOffStatus();
void servoInit();                      // UART1 @ 1 Mbps on GPIO 6/7, torque on, head to home

// Thread-safe head target, in radians. The servo task moves towards it with a
// slew-rate limit, so callers can set it at any rate.
void servoSetTarget(float panRad, float tiltRad);
void servoHome();                      // slow, direct move to (0, 0)
void servoSetTorque(bool on);
void servoGetCurrent(float* panRad, float* tiltRad);
void servoStartTask();                 // starts the 25 Hz servo loop
// Diagnostics: with the probe on, torque is off (move the head by hand) and the servo
// task reads each servo's present position (register 0x38, raw steps; -1 = no answer).
void servoSetProbe(bool on);
void servoWheelTest(int pwm, int ms);  // spin the pan in motor mode (pwm -1000..1000), print positions
void servoProbeRead(int* panRaw, int* tiltRaw);

void setBaseLeds(uint8_t r, uint8_t g, uint8_t b);

// The internal I2C bus (M5.In_I2C) has no lock of its own: two tasks using it at
// once corrupt each other's transfers and the robot panics. Every task other
// than loop() that touches I2C (e.g. the audio task switching mic <-> speaker,
// which reconfigures the ES7210 and AW88298) must hold this lock, and so must
// loop() around its I2C work (touch, PMIC, NFC...). Recursive: nesting is fine.
void i2cLock();
void i2cUnlock();
struct I2CGuard {
    I2CGuard() { i2cLock(); }
    ~I2CGuard() { i2cUnlock(); }
};

// Battery current from the base's INA226 (I2C 0x41, 10 mOhm shunt).
// Positive = the battery is discharging. Call from the loop task only.
bool readBatteryCurrent(float* busVolts, float* milliamps);
