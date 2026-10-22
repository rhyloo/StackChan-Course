#include "camera_extra.h"
#include "board.h"
#include "camera.h"
#include "esp_camera.h"
#include "img_converters.h"

static SemaphoreHandle_t s_lock = xSemaphoreCreateMutex();
static bool s_night = false;
static int s_defaultTarget = -1, s_defaultGain = -1;

void cameraLock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
void cameraUnlock() { xSemaphoreGive(s_lock); }

// GC0308 registers (page 0)
static constexpr int REG_PAGE = 0xFE, REG_EXP_H = 0x03, REG_EXP_L = 0x04, REG_GLOBAL_GAIN = 0x50;
static constexpr int REG_AEC_MODE3 = 0xD2, REG_AEC_TARGET = 0xD3;

static int reg(int r) {
    I2CGuard lock;
    sensor_t* s = esp_camera_sensor_get();
    return s ? s->get_reg(s, r, 0xFF) : -1;
}

static void setReg(int r, int value) {
    I2CGuard lock;
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    s->set_reg(s, REG_PAGE, 0xFF, 0x00);
    s->set_reg(s, r, 0xFF, value);
}

void cameraNight(bool on) {
    if (s_defaultTarget < 0) {          // remember the driver's values, to go back
        s_defaultTarget = reg(REG_AEC_TARGET);
        s_defaultGain = reg(REG_GLOBAL_GAIN);
    }
    s_night = on;
    setReg(REG_AEC_TARGET, on ? 0x68 : s_defaultTarget);           // expected luminance (default 0x48)
    setReg(REG_GLOBAL_GAIN, on ? 0x24 : s_defaultGain);            // 2.25x (0x10 = 1x)
}

bool cameraIsNight() { return s_night; }

static void dropFrames(int n) {         // the new exposure shows up a frame or two later
    for (int i = 0; i < n; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }
}

bool cameraPhotoJpeg(uint8_t quality, uint8_t** jpg, size_t* len) {
    cameraLock();
    bool ok = cameraCaptureJpeg(quality, jpg, len);
    cameraUnlock();
    return ok;
}

static inline void unpack(const uint8_t* p, float* rgb) {   // RGB565, high byte first
    const uint16_t v = p[0] << 8 | p[1];
    rgb[0] = (v >> 11) << 3;
    rgb[1] = ((v >> 5) & 63) << 2;
    rgb[2] = (v & 31) << 3;
}

bool cameraHdrJpeg(uint8_t quality, uint8_t** jpg, size_t* len) {
    static const float FACTORS[3] = { 0.35f, 1.0f, 2.8f };
    uint8_t* shots[3] = {};
    size_t size = 0;
    int w = 0, h = 0;
    bool ok = true;

    cameraLock();
    const int hi = reg(REG_EXP_H), lo = reg(REG_EXP_L), aec = reg(REG_AEC_MODE3);
    const int exposure = (hi & 0x0F) << 8 | lo, gain = reg(REG_GLOBAL_GAIN);
    // An exposure longer than the frame (window + vertical blanking, 0x0F[7:4] and 0x02)
    // stretches the frame and the capture breaks: the long shot stops there and makes up
    // the rest with gain.
    const int frameRows = 488 + (((reg(0x0F) >> 4) & 0x0F) << 8 | reg(0x02));
    Serial.printf("[HDR] exposure %d rows, frame %d rows, gain 0x%02X\n", exposure, frameRows, gain);
    setReg(REG_AEC_MODE3, aec & 0x7F);  // AEC off: the exposure is ours
    for (int k = 0; k < 3 && ok; k++) {
        const int want = max(1, (int)(exposure * FACTORS[k])), e = min(want, frameRows - 1);
        setReg(REG_EXP_H, e >> 8);
        setReg(REG_EXP_L, e & 0xFF);
        setReg(REG_GLOBAL_GAIN, constrain(gain * want / e, 0, 0x3F));
        dropFrames(4);
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { ok = false; break; }
        size = fb->len; w = fb->width; h = fb->height;
        shots[k] = (uint8_t*)ps_malloc(size);
        if (shots[k]) memcpy(shots[k], fb->buf, size); else ok = false;
        esp_camera_fb_return(fb);
    }
    setReg(REG_EXP_H, hi);              // back to automatic
    setReg(REG_EXP_L, lo);
    setReg(REG_GLOBAL_GAIN, gain);
    setReg(REG_AEC_MODE3, aec);
    cameraUnlock();

    // Exposure fusion, single scale: weight = how well exposed the pixel is in that shot
    // (a bell around mid-grey), then the weighted average of the three.
    uint8_t* out = ok ? (uint8_t*)ps_malloc(size) : nullptr;
    if (out) {
        float weight[256];
        for (int y = 0; y < 256; y++) {
            const float d = y / 255.0f - 0.5f;
            weight[y] = expf(-d * d / (2 * 0.2f * 0.2f)) + 1e-3f;
        }
        for (size_t i = 0; i < size; i += 2) {
            float acc[3] = { 0, 0, 0 }, total = 0;
            for (int k = 0; k < 3; k++) {
                float rgb[3];
                unpack(shots[k] + i, rgb);
                const int luma = constrain((int)(0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2]), 0, 255);
                const float wk = weight[luma];
                for (int c = 0; c < 3; c++) acc[c] += wk * rgb[c];
                total += wk;
            }
            const int r = constrain((int)(acc[0] / total), 0, 255), g = constrain((int)(acc[1] / total), 0, 255),
                      b = constrain((int)(acc[2] / total), 0, 255);
            const uint16_t v = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
            out[i] = v >> 8;
            out[i + 1] = v & 0xFF;
        }
        ok = fmt2jpg(out, size, w, h, PIXFORMAT_RGB565, quality, jpg, len);
    } else {
        ok = false;
    }
    free(out);
    for (uint8_t* s : shots) free(s);
    return ok;
}
