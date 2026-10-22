#include "board.h"
#include "stackchan_limits.h"
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

static SemaphoreHandle_t s_i2cMutex = nullptr;
static portMUX_TYPE s_i2cInitMux = portMUX_INITIALIZER_UNLOCKED;

void i2cLock() {
    if (!s_i2cMutex) {
        SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
        portENTER_CRITICAL(&s_i2cInitMux);
        if (!s_i2cMutex) { s_i2cMutex = m; m = nullptr; }
        portEXIT_CRITICAL(&s_i2cInitMux);
        if (m) vSemaphoreDelete(m);
    }
    xSemaphoreTakeRecursive(s_i2cMutex, portMAX_DELAY);
}

void i2cUnlock() { xSemaphoreGiveRecursive(s_i2cMutex); }

static uint8_t s_pmicOn = 0, s_pmicOff = 0;
uint8_t pmicPowerOnStatus() { return s_pmicOn; }
uint8_t pmicPowerOffStatus() { return s_pmicOff; }

bool boardInit(bool servoPower) {
    I2CGuard lock;
    // Why the PMIC last switched on and off (AXP2101 REG 20h / 21h), read before
    // anything else: the ESP32 only sees "power_on" when the whole robot lost power.
    s_pmicOn = M5.Power.Axp2101.readRegister8(0x20);
    s_pmicOff = M5.Power.Axp2101.readRegister8(0x21);
    Serial.printf("[PMIC] power-on source 0x%02X, last power-off source 0x%02X\n", s_pmicOn, s_pmicOff);
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
    if (servoPower) {
        M5.In_I2C.bitOff(PY32_ADDR, REG_PY32_GPIO_PD_L, 0x01, 100000);
        M5.In_I2C.bitOn(PY32_ADDR, REG_PY32_GPIO_PU_L, 0x01, 100000);
        M5.In_I2C.bitOn(PY32_ADDR, REG_PY32_GPIO_O_L, 0x01, 100000);
    } else {
        M5.In_I2C.bitOff(PY32_ADDR, REG_PY32_GPIO_PU_L, 0x01, 100000);
        M5.In_I2C.bitOn(PY32_ADDR, REG_PY32_GPIO_PD_L, 0x01, 100000);
        M5.In_I2C.bitOff(PY32_ADDR, REG_PY32_GPIO_O_L, 0x01, 100000);
    }
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
    I2CGuard lock;
    // 12 WS2812 at full white draw ~0.7 A. Together with Wi-Fi, the speaker and
    // battery charging that collapsed the supply and the PMIC cut the power, so
    // the brightness is capped here whatever the caller asks for.
    r = r * LED_MAX_LEVEL / 255; g = g * LED_MAX_LEVEL / 255; b = b * LED_MAX_LEVEL / 255;
    uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    uint8_t buf[NUM_BASE_LEDS * 2];
    for (int i = 0; i < NUM_BASE_LEDS; i++) { buf[i * 2] = c & 0xFF; buf[i * 2 + 1] = c >> 8; }
    M5.In_I2C.writeRegister(PY32_ADDR, REG_PY32_LED_RAM, buf, sizeof(buf), 100000);
    uint8_t cfg = 0;
    if (M5.In_I2C.readRegister(PY32_ADDR, REG_PY32_LED_CFG, &cfg, 1, 100000)) {
        M5.In_I2C.writeRegister8(PY32_ADDR, REG_PY32_LED_CFG, cfg | 0x40, 100000);  // latch
    }
}

bool readBatteryCurrent(float* busVolts, float* milliamps) {
    I2CGuard lock;
    static constexpr uint8_t INA226_ADDR = 0x41;
    uint8_t b[2];
    if (!M5.In_I2C.readRegister(INA226_ADDR, 0x01, b, 2, 400000)) return false;   // shunt voltage, 2.5 uV/LSB
    int16_t shunt = (int16_t)((b[0] << 8) | b[1]);
    if (!M5.In_I2C.readRegister(INA226_ADDR, 0x02, b, 2, 400000)) return false;   // bus voltage, 1.25 mV/LSB
    uint16_t bus = (b[0] << 8) | b[1];
    *busVolts = bus * 0.00125f;
    *milliamps = shunt * 0.0025f / 0.010f;   // mV / mOhm -> mA: 2.5 uV / 10 mOhm = 0.25 mA per LSB
    return true;
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

// Reads n bytes from register reg (big-endian, like the writes). The bus may echo
// our own request back, so take the last frame with a valid checksum that isn't it.
// Returns false if the servo didn't answer.
static bool scsRead(uint8_t id, uint8_t reg, uint8_t n, uint8_t* out) {
    uart_flush_input(UART_NUM_1);
    scsSend(id, 0x02, reg, &n, 1);
    uint8_t buf[48];
    int got = uart_read_bytes(UART_NUM_1, buf, sizeof(buf), pdMS_TO_TICKS(8));
    bool found = false;
    for (int i = 0; i + 6 + n <= got; i++) {
        if (buf[i] != 0xFF || buf[i + 1] != 0xFF || buf[i + 2] != id || buf[i + 3] != n + 2) continue;
        uint8_t sum = 0;
        for (int k = 2; k < 5 + n; k++) sum += buf[i + k];
        if ((uint8_t)~sum != buf[i + 5 + n]) continue;
        if (buf[i + 4] == 0x02 && buf[i + 5] == reg) continue;   // our own request, echoed
        memcpy(out, buf + i + 5, n);
        found = true;
    }
    return found;
}

static bool scsRead16(uint8_t id, uint8_t reg, int* v) {
    uint8_t b[2];
    if (!scsRead(id, reg, 2, b)) return false;
    *v = b[0] << 8 | b[1];
    return true;
}

// --- Pan in motor (wheel) mode, to cross the ~50 deg the position sensor can't see ---
// Angle limits min = max = 0 switch an SCS servo to motor mode; the speed then goes in
// register 0x2C (bit 10 = reverse). Writing the limits back returns to position mode.
// Written with the EEPROM locked, so the change lives in RAM until the servo loses power.
static int s_panLimitMin = PAN_RAW_MIN, s_panLimitMax = PAN_RAW_MAX;   // read from the servo in servoInit()

static void panWheelMode(bool on) {
    uint8_t b[4] = { 0, 0, 0, 0 };
    if (!on) {
        b[0] = s_panLimitMin >> 8; b[1] = s_panLimitMin;
        b[2] = s_panLimitMax >> 8; b[3] = s_panLimitMax;
    }
    scsSend(1, 0x03, 0x09, b, 4);
}

static void panWheelSpeed(int pwm) {   // -1000..1000
    uint16_t v = pwm < 0 ? ((uint16_t)(-pwm) | 0x400) : (uint16_t)pwm;
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    scsSend(1, 0x03, 0x2C, b, 2);
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
static volatile int s_wheelTestPwm = 0, s_wheelTestMs = 0;   // diagnostics: spin the pan
void servoWheelTest(int pwm, int ms) { s_wheelTestPwm = pwm; s_wheelTestMs = ms; }
static volatile bool s_probe = false;                 // diagnostics: torque off, read positions
static volatile int s_probePan = -1, s_probeTilt = -1;

void servoSetProbe(bool on) { s_probe = on; }
void servoProbeRead(int* panRaw, int* tiltRaw) { *panRaw = s_probePan; *tiltRaw = s_probeTilt; }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float wrapPi(float a) {      // -> (-PI, PI]
    while (a > (float)M_PI) a -= 2 * (float)M_PI;
    while (a <= -(float)M_PI) a += 2 * (float)M_PI;
    return a;
}
static inline float wrap2Pi(float a) {     // -> [0, 2 PI)
    a = fmodf(a, 2 * (float)M_PI);
    return a < 0 ? a + 2 * (float)M_PI : a;
}
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
    // Pan angle limits: if a reset caught it in motor mode (0/0), go back to the defaults.
    int lo, hi, lock;
    if (scsRead16(1, 0x09, &lo) && scsRead16(1, 0x0B, &hi) && hi > lo) {
        s_panLimitMin = lo;
        s_panLimitMax = hi;
    }
    panWheelSpeed(0);
    panWheelMode(false);
    uint8_t lk = 0xFF;
    lock = scsRead(1, 0x30, 1, &lk) ? lk : -1;
    Serial.printf("[SERVO] pan limits %d..%d, EEPROM lock %d\n", s_panLimitMin, s_panLimitMax, lock);
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
    s_targetPan = wrapPi(panRad);   // any angle: see panCross()
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

// Crosses the pan's blind zone in motor mode, in direction dir (+1 = angle and raw going up),
// until the position sensor reads something trustworthy on the far side again. Readings
// right after the blind zone can be garbage near 0 (seen with the probe), so it waits for
// a value well inside the arc. Then back to position mode, with the goal where it stopped.
static void panCross(int dir) {
    const uint32_t t0 = millis();
    panWheelMode(true);
    panWheelSpeed(dir > 0 ? -PAN_CROSS_PWM : PAN_CROSS_PWM);   // positive speed = raw going down
    bool seenEdge = false, done = false;
    int r = -1;
    while (millis() - t0 < PAN_CROSS_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!scsRead16(1, 0x38, &r)) { seenEdge = true; continue; }
        if (dir > 0) {
            if (r >= 990) seenEdge = true;
            else if (seenEdge && r >= 60 && r <= 400) { done = true; break; }
        } else {
            if (r <= 30) seenEdge = true;
            else if (seenEdge && r >= 620 && r <= 960) { done = true; break; }
        }
    }
    panWheelSpeed(0);
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!scsRead16(1, 0x38, &r) || r < PAN_RAW_MIN || r > PAN_RAW_MAX) r = dir > 0 ? PAN_RAW_MIN : PAN_RAW_MAX;
    scsWritePos(1, r, 0);
    panWheelMode(false);
    s_rawPan = r;
    Serial.printf("[PAN] crossed the blind zone %s: %s in %lu ms, raw %d\n", dir > 0 ? "+" : "-",
                  done ? "ok" : "TIMEOUT", (unsigned long)(millis() - t0), r);
}

// The only task that writes to the servo UART, so commands never interleave.
static void servoTask(void*) {
    TickType_t last = xTaskGetTickCount();
    uint32_t lastTorqueOnMs = 0, lastMoveMs = millis();
    bool released = false;   // torque off after SERVO_RELEASE_MS still

    // Back from released: the head may have been turned by hand, so start from where it
    // really is (goal = present position before the torque comes on: no jump).
    auto engage = [&]() {
        if (!released) return;
        int p, t;
        if (scsRead16(1, 0x38, &p) && p >= PAN_RAW_MIN && p <= PAN_RAW_MAX) s_rawPan = p;
        if (scsRead16(2, 0x38, &t) && t >= TILT_RAW_MIN - 40 && t <= TILT_RAW_MAX + 40)
            s_rawTilt = clampi(t, TILT_RAW_MIN, TILT_RAW_MAX);
        scsWritePos(1, s_rawPan, 0);
        scsWritePos(2, s_rawTilt, 0);
        scsTorque(1, true);
        scsTorque(2, true);
        released = false;
        lastTorqueOnMs = lastMoveMs = millis();
    };
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SERVO_PERIOD_MS));

        if (s_wheelTestMs > 0) {
            // Spin the pan in motor mode for a while, printing where the sensor says it is.
            const int pwm = s_wheelTestPwm, ms = s_wheelTestMs;
            s_wheelTestMs = 0;
            int p = -1;
            scsRead16(1, 0x38, &p);
            Serial.printf("[WHEEL] start pwm %d for %d ms, pan raw %d\n", pwm, ms, p);
            panWheelMode(true);
            panWheelSpeed(pwm);
            for (uint32_t t0 = millis(); millis() - t0 < (uint32_t)ms;) {
                vTaskDelay(pdMS_TO_TICKS(40));
                int r = -1;
                scsRead16(1, 0x38, &r);
                Serial.printf("[WHEEL] t=%lu raw %d\n", (unsigned long)(millis() - t0), r);
            }
            panWheelSpeed(0);
            vTaskDelay(pdMS_TO_TICKS(100));
            p = -1;
            scsRead16(1, 0x38, &p);
            // Goal = where it is now, then back to position mode: no jump.
            if (p >= s_panLimitMin && p <= s_panLimitMax) {
                s_rawPan = p;
                scsWritePos(1, p, 0);
                portENTER_CRITICAL(&s_mux);
                s_targetPan = (p - PAN_CENTER_RAW) / STEPS_PER_RAD;
                portEXIT_CRITICAL(&s_mux);
            }
            panWheelMode(false);
            Serial.printf("[WHEEL] stop, pan raw %d, back in position mode\n", p);
            last = xTaskGetTickCount();
            continue;
        }

        // Probe: torque off so the head can be moved by hand; read where each servo is.
        static bool probing = false;
        if (s_probe != probing) {
            probing = s_probe;
            if (!probing && s_probePan >= 0 && s_probeTilt >= 0) {
                // Resume from where the hand left the head, not from the old target: no jump.
                s_rawPan = s_probePan;
                s_rawTilt = s_probeTilt;
                portENTER_CRITICAL(&s_mux);
                s_targetPan = (s_rawPan - PAN_CENTER_RAW) / STEPS_PER_RAD;
                s_targetTilt = (s_rawTilt - TILT_CENTER_RAW) / STEPS_PER_RAD;
                portEXIT_CRITICAL(&s_mux);
            }
            scsTorque(1, !probing);
            scsTorque(2, !probing);
            released = false;
            lastMoveMs = millis();
        }
        if (probing) {
            uint8_t b[2];
            s_probePan = scsRead(1, 0x38, 2, b) ? (b[0] << 8 | b[1]) : -1;
            s_probeTilt = scsRead(2, 0x38, 2, b) ? (b[0] << 8 | b[1]) : -1;
            continue;
        }

        int tq = s_torqueRequest;
        if (tq >= 0) {
            s_torqueRequest = -1;
            if (tq == 1) {
                engage();
            } else {
                scsTorque(1, false);
                scsTorque(2, false);
                released = true;
            }
        }
        if (s_homeRequest) {
            s_homeRequest = false;
            engage();
            lastMoveMs = millis();
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

        // Pan: a target in the blind zone goes to the nearest end of the position arc. If the
        // short way round crosses the blind zone (it points away from where position mode
        // would go), cross it in motor mode. Not when the target is right behind (|d| near
        // PI): there the short way flips from tick to tick.
        const float cur = (s_rawPan - PAN_CENTER_RAW) / STEPS_PER_RAD;
        float panGoal = tp;
        if (tp < PAN_MIN || tp > PAN_MAX)
            panGoal = wrap2Pi(tp - PAN_MAX) <= wrap2Pi(PAN_MIN - tp) ? PAN_MAX : PAN_MIN;
        const float d = wrapPi(tp - cur);
        if (fabsf(d) < (float)M_PI - 0.3f && ((d > 0 && panGoal < cur - 0.05f) || (d < 0 && panGoal > cur + 0.05f))) {
            engage();
            panCross(d > 0 ? 1 : -1);
            lastMoveMs = millis();
            last = xTaskGetTickCount();
            continue;
        }

        // A move asked for while released: torque back on, from where the head is now.
        if (released && (abs(panToRaw(panGoal) - s_rawPan) >= 2 || abs(tiltToRaw(tt) - s_rawTilt) >= 2)) engage();

        // Slew-rate limit: big jumps make current spikes that brown out the base.
        int dp = clampi(panToRaw(panGoal) - s_rawPan, -PAN_MAX_STEP, PAN_MAX_STEP);
        int dt = clampi(tiltToRaw(tt) - s_rawTilt, -TILT_MAX_STEP, TILT_MAX_STEP);
        uint16_t moveMs = SERVO_PERIOD_MS + 10;
        // The overload protection (80 % of stall torque for 2 s) switches a servo's torque
        // off and it stays off. Switch it back on when a move is asked for, at most every
        // 2 s, so a head held by hand doesn't get a stall-current spike every tick.
        if (!released && (abs(dp) >= 2 || abs(dt) >= 2) && millis() - lastTorqueOnMs >= 2000) {
            lastTorqueOnMs = millis();
            scsTorque(1, true);
            scsTorque(2, true);
        }
        if (abs(dp) >= 2) {
            s_rawPan += dp;
            scsWritePos(1, s_rawPan, moveMs);
            vTaskDelay(pdMS_TO_TICKS(4));   // stagger writes to avoid overlapping current peaks
        }
        if (abs(dt) >= 2) {
            s_rawTilt += dt;
            scsWritePos(2, s_rawTilt, moveMs);
        }
        if (abs(dp) >= 2 || abs(dt) >= 2) {
            lastMoveMs = millis();
        } else if (!released && SERVO_RELEASE_MS && millis() - lastMoveMs >= SERVO_RELEASE_MS) {
            scsTorque(1, false);   // still for a while: let go
            scsTorque(2, false);
            released = true;
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
