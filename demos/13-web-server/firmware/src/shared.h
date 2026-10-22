#pragma once
#include <Arduino.h>

// What the web page and MQTT see, and the commands they send. The loop() task owns
// I2C and the screen: it reads the sensors into a State and runs the commands.

struct State {
    float battV = 0, currentMa = NAN;   // INA226: negative = charging
    int battPct = 0;
    float pan = 0, tilt = 0;            // degrees, 0 = straight ahead
    float ax = 0, ay = 0, az = 0;       // accelerometer, g
    float pitch = 0, roll = 0;          // head angle from the accelerometer, degrees (+ pitch = up)
    float gyro = 0;                     // rotation speed, deg/s (all axes together)
    float lux = 0;                      // LTR-553 ambient light
    int prox = 0;                       // LTR-553 proximity, 0-2047 (higher = closer)
    int touches = 0;                    // screen taps since boot
    int rssi = 0;
    uint32_t uptime = 0, heap = 0;
    float chipTemp = 0;
    char mood[12] = "neutral";
    char text[48] = "";
};

// Fills json with the state (the WebSocket and MQTT message). Returns its length.
size_t stateToJson(char* json, size_t size);
State stateGet();

// Commands as JSON, e.g. {"cmd":"head","pan":90,"tilt":10}. Any task can submit one;
// loop() runs it. See README.md for the list.
void commandSubmit(const char* json, size_t len);

const char* robotName();   // "stackchan-4F48": also the mDNS name (stackchan-4F48.local)
