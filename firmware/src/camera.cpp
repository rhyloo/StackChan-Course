#include "camera.h"
#include <M5Unified.h>
#include "esp_camera.h"
#include "img_converters.h"

// CoreS3 DVP pins. XCLK comes from a crystal on the camera flex; reset is on
// the AW9523 expander (P1_0); PWDN is tied to GND.
static constexpr uint8_t AW9523_ADDR = 0x58;

static void resetSensor() {
    M5.In_I2C.bitOff(AW9523_ADDR, 0x05, 0x01, 400000);  // P1_0 as output
    M5.In_I2C.bitOff(AW9523_ADDR, 0x03, 0x01, 400000);  // reset low
    delay(25);
    M5.In_I2C.bitOn(AW9523_ADDR, 0x03, 0x01, 400000);   // release, PLL settles
    delay(50);
}

bool cameraInit() {
    M5.Power.Axp2101.setALDO1(1800);   // DVDD
    M5.Power.Axp2101.setALDO2(2800);   // AVDD
    M5.Power.Axp2101.setALDO4(3300);   // IOVDD
    delay(20);
    resetSensor();
    M5.In_I2C.release();               // the camera's SCCB shares GPIO 11/12
    delay(20);

    camera_config_t c = {};
    c.pin_d0 = 39; c.pin_d1 = 40; c.pin_d2 = 41; c.pin_d3 = 42;
    c.pin_d4 = 15; c.pin_d5 = 16; c.pin_d6 = 48; c.pin_d7 = 47;
    c.pin_vsync = 46; c.pin_href = 38; c.pin_pclk = 45;
    c.pin_sccb_sda = 12; c.pin_sccb_scl = 11;
    c.pin_xclk = -1; c.pin_reset = -1; c.pin_pwdn = -1;
    c.xclk_freq_hz = 10000000;
    c.ledc_timer = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_RGB565;
    c.frame_size = FRAMESIZE_QVGA;
    c.jpeg_quality = 12;               // unused with RGB565
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;  // always the newest frame, never a stale one
    c.sccb_i2c_port = -1;

    // A dropped VSYNC must never try to log from the camera ISR.
    esp_log_level_set("cam_hal", ESP_LOG_NONE);
    esp_log_level_set("esp_camera", ESP_LOG_NONE);
    esp_log_level_set("camera", ESP_LOG_NONE);
    esp_log_level_set("gdma", ESP_LOG_NONE);

    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) {
        Serial.printf("[CAM] esp_camera_init failed: 0x%x (%s)\n", err, esp_err_to_name(err));
        return false;
    }
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        Serial.printf("[CAM] sensor PID 0x%02X\n", s->id.PID);
        s->set_vflip(s, 1);
        s->set_hmirror(s, 0);
    }
    return true;
}

bool cameraCaptureJpeg(uint8_t quality, uint8_t** jpg, size_t* len) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return false;
    bool ok = frame2jpg(fb, quality, jpg, len);
    esp_camera_fb_return(fb);
    return ok;
}
