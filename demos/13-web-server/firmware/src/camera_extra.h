#pragma once
#include <Arduino.h>

// Beyond lib/StackChanHW/src/camera.*: the GC0308's exposure registers (datasheet in
// docs/hardware/datasheets). The camera's SCCB shares GPIO 11/12 with the internal I2C,
// so every register access holds the I2C lock.
void cameraLock();      // the photo, the HDR photo and the stream share the camera
void cameraUnlock();
void cameraNight(bool on);   // brighter in dim light: higher AEC target and gain (more noise)
bool cameraIsNight();
// A photo (320x240, like the video). The caller frees *jpg.
bool cameraPhotoJpeg(uint8_t quality, uint8_t** jpg, size_t* len);
// Three shots at 0.35x, 1x and 2.8x the current exposure, fused on the robot: each pixel
// takes most from the shot where it is best exposed. On success the caller frees *jpg.
bool cameraHdrJpeg(uint8_t quality, uint8_t** jpg, size_t* len);
