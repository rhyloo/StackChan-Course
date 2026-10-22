#pragma once
#include <Arduino.h>

// Optional MQTT. With a broker set (from the page; saved on the robot), it publishes
//   <base>/state         the same JSON as the WebSocket, every 2 s
//   <base>/availability  "online", or "offline" when the robot disappears (last will)
// and runs every JSON command published to <base>/cmd. <base> is "stackchan/<id>",
// e.g. stackchan/4F48. It also announces itself to Home Assistant (MQTT discovery).
void mqttStart();                                 // call once Wi-Fi is up
void mqttConfigure(const char* host, int port);   // saves the broker and reconnects; "" = off
void mqttPublishState(const char* json, size_t len);
const char* mqttStatus();                         // "off", "connecting", "connected", "error"
const char* mqttBroker();                         // "host:port" or ""
