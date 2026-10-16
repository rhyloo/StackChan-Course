#pragma once
#include <Arduino.h>

// UDP link with the laptop relay (see demos/01-vr-cardboard/PROTOCOL.md).
// Shared by the robot demos: it pairs with the relay, streams the camera when
// someone watches, sends telemetry and drives the head from POSE packets.
// Demos extend it with their own commands and message types (see below).

struct LinkStats {
    bool     relayOnline;     // relay heard from in the last LINK_TIMEOUT_MS
    char     relayIp[16];
    float    fps;             // frames sent per second
    uint32_t jpegBytes;       // size of the last frame
    uint32_t sendErrors;
    uint32_t poses;           // POSE packets accepted
    char     lastText[64];    // last "text" command, shown on the screen
};

void linkStart();             // call once Wi-Fi is up; starts the RX, video and hello tasks
void linkGetStats(LinkStats* out);
void linkSetBattery(float volts, int pct, float currentMa);

// Commands that touch I2C or the display must run on the loop task: the link
// queues them here and main.cpp applies them.
struct PendingCommands {
    int brightness = -1;      // 0-255
    int wifiSleep = -1;       // 0/1
    int leds[3] = { -1, -1, -1 };
};
void linkTakePending(PendingCommands* out);
bool linkVideoWanted();       // the relay has someone watching
const char* robotId();        // last 4 hex digits of the MAC, e.g. "4F48": the robot is "stackchan-4F48"

// --- Extension points for demos ---
// Text commands the link doesn't know ("mic on"...). Return true if handled.
// Runs on the network task: keep it short and don't touch I2C or the display.
void linkSetCommandHandler(bool (*handler)(const char* cmd));
// Binary messages from the relay with a type the link doesn't know.
// payload/len exclude the 4-byte header. Runs on the network task.
void linkSetMessageHandler(void (*handler)(uint8_t type, const uint8_t* payload, size_t len));
// Sends header + payload to the paired relay. False if offline or the send failed.
bool linkSend(uint8_t type, const uint8_t* payload, size_t len);
bool linkRelayOnline();
