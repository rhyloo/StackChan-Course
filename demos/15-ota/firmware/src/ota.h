#pragma once
// Demo 15 - The OTA base: what a test sees of it.
//
// The base (ota.cpp) owns setup() and loop(). It connects to the Wi-Fi, waits for
// new firmware (ArduinoOTA on port 3232 and a web page on port 80) and calls the test
// like an Arduino sketch: testSetup() once, then testLoop() over and over, both from
// the loop() task. The network runs in its own task on core 0, so an update still
// gets in while a test is stuck in a loop.
//
// A test is one folder in src/tests/ with an environment in platformio.ini.
#include <M5Unified.h>

void testSetup();
void testLoop();
// Optional: an update is arriving, leave the hardware quiet (servos, LEDs, sound).
// Called from the loop() task; after it testLoop() isn't called again.
void testStop();

// The top OTA_BAR_H pixels of the screen are the base's status bar (name, IP, test,
// state), redrawn every second: draw below it.
constexpr int OTA_BAR_H = 20;

const char* otaRobotName();   // "stackchan-4F48", also its mDNS name (.local)

// Like Serial, but each line also goes over the Wi-Fi to `run.sh log` (and to
// `run.sh send` once the test is running): no USB cable needed to see it.
//     Log.printf("angle %d\n", a);
// Only from the loop() task. A line is sent when it ends in '\n'.
class OtaLog : public Print {
public:
    size_t write(uint8_t c) override;
    using Print::write;
private:
    char _line[160];
    size_t _len = 0;
};
extern OtaLog Log;
