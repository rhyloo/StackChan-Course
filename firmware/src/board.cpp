#include "board.h"
#include "config.h"
#include <M5Unified.h>
#include <driver/uart.h>

// --- I2C devices on the CoreS3 and the K151 base ---
static constexpr uint8_t AW9523_ADDR        = 0x58;
static constexpr uint8_t PY32_ADDR          = 0x6F;
static constexpr uint8_t REG_PY32_VERSION   = 0x02;
static constexpr uint8_t REG_PY32_GPIO_M_L  = 0x03;
static constexpr uint8_t REG_PY32_GPIO_M_H  = 0x04;
static constexpr uint8_t REG_PY32_GPIO_O_L  = 0x05;
static constexpr uint8_t REG_PY32_GPIO_PU_L = 0x09;
static constexpr uint8_t REG_PY32_GPIO_PU_H = 0x0A;
static constexpr uint8_t REG_PY32_GPIO_PD_L = 0x0B;
static constexpr uint8_t REG_PY32_GPIO_PD_H = 0x0C;
static constexpr uint8_t REG_PY32_GPIO_DRV_H= 0x14;
static constexpr uint8_t REG_PY32_LED_CFG   = 0x24;
static constexpr uint8_t REG_PY32_LED_RAM   = 0x30;
static constexpr uint8_t NUM_BASE_LEDS      = 12;

bool boardInit() {
    // 5 V to the base connector (AW9523 BUS_EN) and LCD backlight boost (BOOST_EN).
    M5.In_I2C.bitOn(AW9523_ADDR, 0x02, 0b00000010, 400000);
    M5.In_I2C.bitOn(AW9523_ADDR, 0x03, 0b10000000, 400000);
    M5.Power.setExtOutput(true);

    // AXP2101 LDOs: ALDO1 1.8 V, ALDO2 2.8 V (camera AVDD), ALDO3/4 3.3 V.
    M5.Power.Axp2101.begin();
    static constexpr uint8_t axp_init[] = {
        0x90, 0xBF, 0x92, 18 - 5, 0x93, 28 - 5, 0x94, 33 - 5, 0x95, 33 - 5,
        0x27, 0x00, 0x69, 0x11, 0x10, 0x30, 0x30, 0x0F
    };
    M5.Power.Axp2101.writeRegister8Array(axp_init, sizeof(axp_init));

    // Wait for the base coprocessor to boot once it gets 5 V.
    uint8_t ver = 0;
    for (int i = 0; i < 20; i++) {
        if (M5.In_I2C.readRegister(PY32_ADDR, REG_PY32_VERSION, &ver, 1, 100000) && ver != 0 && ver != 0xFF) break;
        delay(100);
    }
    // PY32 pin 0 = VM_EN (5 V to the servos).
    M5.In_I2C.bitOn(PY32_ADDR, REG_PY32_GPIO_M_L, 0x01, 100000);
    M5.In_I2C.bitOff(PY32_ADDR, REG_PY32_GPIO_PD_L, 0x01, 100000);
    M5.In_I2C.bitOn(PY32_ADDR, REG_PY32_GPIO_PU_L, 0x01, 100000);
    M5.In_I2C.bitOn(PY32_ADDR, REG_PY32_GPIO_O_L, 0x01, 100000);
    // PY32 pin 13 = WS2812 data for the 12 base LEDs.
    static constexpr uint8_t LED_PIN_MASK = 0x20;
    M5.In_I2C.bitOn(PY32_ADDR, REG_PY32_GPIO_M_H, LED_PIN_MASK, 100000);
    M5.In_I2C.bitOff(PY32_ADDR, REG_PY32_GPIO_PD_H, LED_PIN_MASK, 100000);
    M5.In_I2C.bitOn(PY32_ADDR, REG_PY32_GPIO_PU_H, LED_PIN_MASK, 100000);
    M5.In_I2C.bitOff(PY32_ADDR, REG_PY32_GPIO_DRV_H, LED_PIN_MASK, 100000);
    M5.In_I2C.writeRegister8(PY32_ADDR, REG_PY32_LED_CFG, NUM_BASE_LEDS & 0x3F, 100000);
    delay(50);
    return ver != 0 && ver != 0xFF;
}

void setBaseLeds(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    uint8_t buf[NUM_BASE_LEDS * 2];
    for (int i = 0; i < NUM_BASE_LEDS; i++) { buf[i * 2] = c & 0xFF; buf[i * 2 + 1] = c >> 8; }
    M5.In_I2C.writeRegister(PY32_ADDR, REG_PY32_LED_RAM, buf, sizeof(buf), 100000);
    uint8_t cfg = 0;
    if (M5.In_I2C.readRegister(PY32_ADDR, REG_PY32_LED_CFG, &cfg, 1, 100000)) {
        M5.In_I2C.writeRegister8(PY32_ADDR, REG_PY32_LED_CFG, cfg | 0x40, 100000);  // latch
    }
}

// --- Feetech SCS0009 over UART1 ---
static void scsSend(uint8_t id, uint8_t inst, uint8_t reg, const uint8_t* params, uint8_t n) {
    uint8_t len = n + 3;
    uint8_t sum = id + len + inst + reg;
    for (uint8_t i = 0; i < n; i++) sum += params[i];
    uint8_t checksum = ~sum;
    uint8_t header[6] = { 0xFF, 0xFF, id, len, inst, reg };
    uart_write_bytes(UART_NUM_1, (const char*)header, 6);
    if (n) uart_write_bytes(UART_NUM_1, (const char*)params, n);
    uart_write_bytes(UART_NUM_1, (const char*)&checksum, 1);
    uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(50));
}

static void scsTorque(uint8_t id, bool on) {
    uint8_t v = on ? 1 : 0;
    scsSend(id, 0x03, 0x28, &v, 1);
}

static void scsWritePos(uint8_t id, uint16_t pos, uint16_t timeMs) {
    uint8_t b[6] = { (uint8_t)(pos >> 8), (uint8_t)pos, (uint8_t)(timeMs >> 8), (uint8_t)timeMs, 0, 0 };
    scsSend(id, 0x03, 0x2A, b, 6);
}

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static float s_targetPan = 0, s_targetTilt = 0;
static float s_curPan = 0, s_curTilt = 0;
static volatile bool s_homeRequest = false;
static volatile int s_torqueRequest = -1;   // -1 none, 0 off, 1 on
static int s_rawPan = PAN_CENTER_RAW, s_rawTilt = TILT_CENTER_RAW;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int panToRaw(float r)  { return clampi(PAN_CENTER_RAW  + (int)roundf(r * STEPS_PER_RAD), PAN_RAW_MIN, PAN_RAW_MAX); }
static inline int tiltToRaw(float r) { return clampi(TILT_CENTER_RAW + (int)roundf(r * STEPS_PER_RAD), TILT_RAW_MIN, TILT_RAW_MAX); }

void servoInit() {
    uart_config_t cfg = {};
    cfg.baud_rate = 1000000;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_APB;
    uart_driver_install(UART_NUM_1, 256, 256, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &cfg);
    uart_set_pin(UART_NUM_1, 6, 7, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    delay(50);
    scsTorque(1, true);
    scsTorque(2, true);
    delay(50);
    scsWritePos(1, PAN_CENTER_RAW, 500);
    delay(6);
    scsWritePos(2, TILT_CENTER_RAW, 500);
}

void servoSetTarget(float panRad, float tiltRad) {
    if (isnan(panRad) || isnan(tiltRad)) return;
    portENTER_CRITICAL(&s_mux);
    s_targetPan = clampf(panRad, PAN_MIN, PAN_MAX);
    s_targetTilt = clampf(tiltRad, TILT_MIN, TILT_MAX);
    portEXIT_CRITICAL(&s_mux);
}

void servoHome() { s_homeRequest = true; }
void servoSetTorque(bool on) { s_torqueRequest = on ? 1 : 0; }

void servoGetCurrent(float* panRad, float* tiltRad) {
    portENTER_CRITICAL(&s_mux);
    *panRad = s_curPan;
    *tiltRad = s_curTilt;
    portEXIT_CRITICAL(&s_mux);
}

// The only task that writes to the servo UART, so commands never interleave.
static void servoTask(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SERVO_PERIOD_MS));

        int tq = s_torqueRequest;
        if (tq >= 0) {
            s_torqueRequest = -1;
            scsTorque(1, tq == 1);
            scsTorque(2, tq == 1);
        }
        if (s_homeRequest) {
            s_homeRequest = false;
            portENTER_CRITICAL(&s_mux);
            s_targetPan = s_targetTilt = 0;
            portEXIT_CRITICAL(&s_mux);
            s_rawPan = PAN_CENTER_RAW;
            s_rawTilt = TILT_CENTER_RAW;
            scsWritePos(1, s_rawPan, 500);
            vTaskDelay(pdMS_TO_TICKS(6));
            scsWritePos(2, s_rawTilt, 500);
        }

        float tp, tt;
        portENTER_CRITICAL(&s_mux);
        tp = s_targetPan;
        tt = s_targetTilt;
        portEXIT_CRITICAL(&s_mux);

        // Slew-rate limit: big jumps make current spikes that brown out the base.
        int dp = clampi(panToRaw(tp) - s_rawPan, -PAN_MAX_STEP, PAN_MAX_STEP);
        int dt = clampi(tiltToRaw(tt) - s_rawTilt, -TILT_MAX_STEP, TILT_MAX_STEP);
        uint16_t moveMs = SERVO_PERIOD_MS + 10;
        if (abs(dp) >= 2) {
            s_rawPan += dp;
            scsWritePos(1, s_rawPan, moveMs);
            vTaskDelay(pdMS_TO_TICKS(4));   // stagger writes to avoid overlapping current peaks
        }
        if (abs(dt) >= 2) {
            s_rawTilt += dt;
            scsWritePos(2, s_rawTilt, moveMs);
        }

        portENTER_CRITICAL(&s_mux);
        s_curPan = (s_rawPan - PAN_CENTER_RAW) / STEPS_PER_RAD;
        s_curTilt = (s_rawTilt - TILT_CENTER_RAW) / STEPS_PER_RAD;
        portEXIT_CRITICAL(&s_mux);
    }
}

void servoStartTask() {
    xTaskCreatePinnedToCore(servoTask, "servo", 4096, nullptr, 4, nullptr, 1);
}
