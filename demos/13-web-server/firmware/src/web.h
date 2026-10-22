#pragma once
#include <Arduino.h>

// Port 80: the page (/), the WebSocket (/ws), a photo (/photo.jpg) and an HDR photo (/hdr.jpg).
// Port 81: the camera as MJPEG (/stream), in its own server so a video that
// never ends doesn't block the page and the WebSocket.
void webStart();
void webBroadcast(const char* json, size_t len);   // to every WebSocket client
int webClients();                                  // WebSocket clients at the last broadcast
