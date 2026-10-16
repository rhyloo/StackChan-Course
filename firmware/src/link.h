#pragma once
#include <Arduino.h>

// UDP link with the laptop relay (see PROTOCOL.md, "Robot <-> laptop").

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
void linkSetBattery(float volts, int pct);
const char* robotId();        // last 4 hex digits of the MAC, e.g. "4F48": the robot is "stackchan-4F48"
