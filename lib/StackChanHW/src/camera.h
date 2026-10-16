#pragma once
#include <Arduino.h>

// GC0308 on the CoreS3. The sensor has NO hardware JPEG: it outputs RGB565
// and we encode in software with frame2jpg(). Asking the driver for
// PIXFORMAT_JPEG fails with ESP_ERR_NOT_SUPPORTED (one of the reasons the
// September UDP attempt was reverted).

bool cameraInit();
// Captures one QVGA frame and encodes it. On success the caller owns *jpg
// and must free() it.
bool cameraCaptureJpeg(uint8_t quality, uint8_t** jpg, size_t* len);
