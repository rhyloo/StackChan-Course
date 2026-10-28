#pragma once
// Demo 15 - OTA on demand: what a test sees of the base.
//
// The base (ota.cpp) is in every firmware of this folder, but the OTA is OFF while the
// test runs: the Wi-Fi radio, the whole screen and the CPU are the test's (Wi-Fi in any
// mode, ESP-NOW, BLE...). To send the next firmware, turn the OTA on:
//   - hold the screen 3 s while the test runs: a menu, "Activate OTA";
//   - hold the screen while the robot starts;
//   - from the test, otaEnable().
// The robot then restarts in OTA mode: the test doesn't run, the base connects to the
// Wi-Fi of config/wifi_secrets.h and waits for `demos/15-ota/run.sh send <test>`.
#include <M5Unified.h>

void testSetup();      // once, like setup()
void testLoop();       // over and over, like loop(); M5.update() is already called
void testStop();       // optional: going into OTA mode, leave the hardware quiet (LEDs, motors, sound)
void testRedraw();     // optional: the OTA menu covered the screen and was closed: draw it again

// Taps: use M5.Touch.getDetail().wasClicked() (it fires when the finger lifts), not
// wasPressed(): a long press opens the OTA menu and shouldn't also count as a tap.

void otaEnable();              // stop the test and restart in OTA mode (doesn't return)
const char* otaRobotName();    // "stackchan-4F48", also its mDNS name (.local) in OTA mode
