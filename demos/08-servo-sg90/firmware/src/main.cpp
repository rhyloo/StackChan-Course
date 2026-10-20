// Demo 08 - Un servo SG90 en el PORT.B del Stack-chan, movido desde la pantalla.
//
// Conexión (PORT.B):
//   SG90 negro -> negro / - (GND)    SG90 rojo -> rojo / + (5 V)    SG90 azul (señal) -> PWM
// La señal puede ir al pin G9 (cable amarillo) o al G8 (blanco): el pulso sale
// por los dos a la vez, así funciona sea cual sea.
//
// Un servo de modelismo se controla con un pulso cada 20 ms (50 Hz): la anchura
// del pulso dice el ángulo. Con el SG90, ~500 µs es 0° y ~2400 µs es 180°.
// El ESP32 genera ese pulso por hardware con el periférico LEDC (su PWM).
//
// En la pantalla: arrastra la barra, o toca 0°, 90°, 180° o «Barrido».
// Por el monitor serie: "angulo 45".
#include <M5Unified.h>
#include "board.h"

static const int         SERVO_PINS[] = { 9, 8 }; // PORT.B: G9 (yellow) and G8 (white), same pulse on both
static constexpr int      PWM_CHANNEL  = 0;
static constexpr int      PWM_HZ       = 50;      // one pulse every 20 ms
static constexpr int      PWM_BITS     = 14;      // 20 ms / 16384 = 1.2 µs per step
static constexpr int      MIN_US       = 500;     // pulse for 0°   } adjust if the servo buzzes against its
static constexpr int      MAX_US       = 2400;    // pulse for 180° } end stop: 600/2300 or even 1000/2000
static constexpr float    SPEED_DEG_S  = 180;     // smooth moves: sudden jumps pull current peaks from the 5 V
static constexpr uint32_t TICK_MS      = 20;
static constexpr uint8_t  BRIGHTNESS   = 179;     // 70 %

static auto& lcd = M5.Display;
static M5Canvas s_dial(&M5.Display);              // drawn off-screen: no flicker
static constexpr int DIAL_Y = 24, DIAL_H = 136;   // dial area on the screen
static constexpr int SLIDER_Y = 162, SLIDER_H = 32, SLIDER_X0 = 20, SLIDER_X1 = 300;
static constexpr int BTN_Y = 198, BTN_H = 42;

static float s_angle = 90, s_target = 90;
static bool s_sweep = false;

static int pulseUs(float deg) { return MIN_US + (MAX_US - MIN_US) * deg / 180.0f; }

static void writeServo(float deg) {
    const uint32_t duty = (uint32_t)pulseUs(deg) * (1 << PWM_BITS) / (1000000 / PWM_HZ);
    ledcWrite(PWM_CHANNEL, duty);
}

// Half-circle dial with the servo arm: 0° on the left, 180° on the right.
static void drawDial() {
    const int cx = 160, cy = DIAL_H - 16, r = 96;
    s_dial.fillSprite(TFT_BLACK);
    for (int a = 0; a <= 180; a += 15) {
        const float rad = (180 - a) * DEG_TO_RAD;
        const int in = a % 45 ? r - 6 : r - 12;
        s_dial.drawLine(cx + cos(rad) * in, cy - sin(rad) * in, cx + cos(rad) * r, cy - sin(rad) * r, TFT_DARKGREY);
    }
    s_dial.drawArc(cx, cy, r, r - 2, 180, 360, TFT_DARKGREY);
    // Target (thin) and actual position (the arm).
    const float t = (180 - s_target) * DEG_TO_RAD, p = (180 - s_angle) * DEG_TO_RAD;
    s_dial.drawLine(cx, cy, cx + cos(t) * (r - 4), cy - sin(t) * (r - 4), TFT_DARKCYAN);
    s_dial.drawWideLine(cx, cy, cx + cos(p) * (r - 14), cy - sin(p) * (r - 14), 6, TFT_ORANGE);
    s_dial.fillCircle(cx, cy, 10, TFT_WHITE);

    s_dial.setTextDatum(middle_center);
    s_dial.setFont(&fonts::FreeSansBold24pt7b);
    s_dial.setTextColor(TFT_WHITE);
    const String num = String((int)lroundf(s_angle));
    s_dial.drawString(num, cx, cy - 46);
    s_dial.drawCircle(cx + s_dial.textWidth(num) / 2 + 8, cy - 62, 5, TFT_WHITE);   // the ° sign
    s_dial.setFont(&fonts::efontJA_16);
    s_dial.setTextColor(TFT_LIGHTGREY);
    s_dial.drawString(String(pulseUs(s_angle)) + " µs", cx, cy + 10);
    s_dial.pushSprite(0, DIAL_Y);
}

static void drawSlider() {
    lcd.fillRect(0, SLIDER_Y, lcd.width(), SLIDER_H, TFT_BLACK);
    const int y = SLIDER_Y + SLIDER_H / 2;
    lcd.fillRoundRect(SLIDER_X0, y - 4, SLIDER_X1 - SLIDER_X0, 8, 4, TFT_DARKGREY);
    const int x = SLIDER_X0 + (SLIDER_X1 - SLIDER_X0) * s_target / 180;
    lcd.fillRoundRect(SLIDER_X0, y - 4, x - SLIDER_X0, 8, 4, TFT_DARKCYAN);
    lcd.fillCircle(x, y, 12, TFT_CYAN);
}

static void drawButtons() {
    const char* labels[4] = { "0°", "90°", "180°", s_sweep ? "Parar" : "Barrido" };
    for (int i = 0; i < 4; i++) {
        const uint16_t bg = i == 3 ? (s_sweep ? TFT_RED : TFT_MAGENTA) : TFT_BLUE;
        lcd.fillRoundRect(i * 80 + 3, BTN_Y + 2, 74, BTN_H - 4, 8, bg);
        lcd.setTextColor(TFT_WHITE, bg);
        lcd.setTextDatum(middle_center);
        lcd.drawString(labels[i], i * 80 + 40, BTN_Y + BTN_H / 2);
    }
    lcd.setTextDatum(top_left);
}

static void setTarget(float deg) {
    s_target = constrain(deg, 0.0f, 180.0f);
    drawSlider();
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    lcd.setBrightness(BRIGHTNESS);
    lcd.setFont(&fonts::efontJA_16);   // accents, ñ and µ

    // 5 V to the Grove ports (and the base). The head servos are not used here.
    if (!boardInit()) Serial.println("Base K151 not answering (PY32)");
    setBaseLeds(0, 0, 0);

    ledcSetup(PWM_CHANNEL, PWM_HZ, PWM_BITS);
    for (int pin : SERVO_PINS) ledcAttachPin(pin, PWM_CHANNEL);   // one channel can drive several pins
    writeServo(s_angle);

    lcd.fillScreen(TFT_BLACK);
    lcd.fillRect(0, 0, lcd.width(), DIAL_Y, TFT_ORANGE);
    lcd.setTextColor(TFT_BLACK, TFT_ORANGE);
    lcd.setCursor(6, 4);
    lcd.print("Servo SG90 · PORT.B (G9/G8)");
    s_dial.setPsram(true);
    s_dial.setColorDepth(16);
    s_dial.createSprite(lcd.width(), DIAL_H);
    drawDial();
    drawSlider();
    drawButtons();
    Serial.printf("\n[Demo 08 SG90] signal on G9 and G8 (PORT.B), %d-%d us, write 'angulo 45'\n", MIN_US, MAX_US);
}

void loop() {
    M5.update();
    static uint32_t lastTick = 0;

    // Touch: drag on the slider, tap the buttons.
    if (M5.Touch.getCount()) {
        const auto& d = M5.Touch.getDetail();
        if (d.isPressed() && d.y >= SLIDER_Y - 10 && d.y < SLIDER_Y + SLIDER_H + 4) {
            s_sweep = false;
            setTarget((d.x - SLIDER_X0) * 180.0f / (SLIDER_X1 - SLIDER_X0));
        } else if (d.wasPressed() && d.y >= BTN_Y) {
            const int b = d.x / 80;
            if (b < 3) { s_sweep = false; setTarget(b * 90); }
            else { s_sweep = !s_sweep; if (s_sweep) setTarget(s_angle < 90 ? 180 : 0); }
            drawButtons();
        }
    }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.startsWith("angulo ")) { s_sweep = false; setTarget(line.substring(7).toFloat()); drawButtons(); }
    }

    // Move towards the target at SPEED_DEG_S, one step per tick.
    if (millis() - lastTick >= TICK_MS) {
        lastTick = millis();
        const float step = SPEED_DEG_S * TICK_MS / 1000.0f;
        const float before = s_angle;
        if (fabsf(s_target - s_angle) <= step) s_angle = s_target;
        else s_angle += s_target > s_angle ? step : -step;
        if (s_angle != before) {
            writeServo(s_angle);
            drawDial();
        } else if (s_sweep) {
            setTarget(s_angle < 90 ? 180 : 0);   // reached one end: back to the other
        }
    }
    delay(2);
}
