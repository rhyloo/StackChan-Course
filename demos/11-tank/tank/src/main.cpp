// Demo 11 - Firmware for the Keyestudio KS0428 tank (Arduino UNO + L298P + HM-10).
//
// The HM-10 (Bluetooth LE) is wired to the Arduino's serial port (D0/D1) at
// 9600 baud: what the laptop writes over Bluetooth arrives here through Serial.
//
// Commands, one per line:
//   M<left>,<right>  speed of each track, from -255 (back) to 255 (forward). E.g.: "M200,-200"
//                    If no other one arrives within TIMEOUT_MS, the tank STOPS: if the
//                    Bluetooth drops, it doesn't keep driving on its own.
//   F B L R S        the ones from the "keyes BT car" app (forward, back, left, right, stop),
//                    with no time limit: the app sends S on release.
//
// Motors (L298P shield): a direction pin (LOW = forward) and a speed pin (PWM).
#include <Arduino.h>

const int ML_CTRL = 13, ML_PWM = 11;   // left track
const int MR_CTRL = 12, MR_PWM = 3;    // right track
const unsigned long TIMEOUT_MS = 400;  // no commands for this long = stop
const int APP_SPEED = 200;             // speed of the F/B/L/R commands
const int RAMP_STEP = 20;              // max PWM change every RAMP_MS: soft starts,
const unsigned long RAMP_MS = 10;      // no current spikes that reset the board or the HM-10

int targetL = 0, targetR = 0;          // where we are going
int speedL = 0, speedR = 0;            // where we are (with the ramp)
bool watchdog = false;                 // true with the M commands
unsigned long lastCmdMs = 0, lastRampMs = 0;
char line[24];
uint8_t lineLen = 0;

void drive(int ctrlPin, int pwmPin, int speed) {
    digitalWrite(ctrlPin, speed < 0 ? HIGH : LOW);
    analogWrite(pwmPin, abs(speed));
}

void setTarget(int l, int r, bool withWatchdog) {
    targetL = constrain(l, -255, 255);
    targetR = constrain(r, -255, 255);
    watchdog = withWatchdog;
    lastCmdMs = millis();
}

void handleLine() {
    line[lineLen] = '\0';
    if (line[0] == 'M') {
        const char* comma = strchr(line, ',');
        if (comma) setTarget(atoi(line + 1), atoi(comma + 1), true);
    }
}

void handleChar(char c) {
    switch (c) {   // "keyes BT car" app
        case 'F': setTarget(APP_SPEED, APP_SPEED, false); return;
        case 'B': setTarget(-APP_SPEED, -APP_SPEED, false); return;
        case 'L': setTarget(-APP_SPEED, APP_SPEED, false); return;
        case 'R': setTarget(APP_SPEED, -APP_SPEED, false); return;
        case 'S': setTarget(0, 0, false); return;
    }
    if (c == '\n' || c == '\r') {
        if (lineLen) handleLine();
        lineLen = 0;
    } else if (lineLen < sizeof(line) - 1) {
        line[lineLen++] = c;
    } else {
        lineLen = 0;   // a line too long is garbage: drop it
    }
}

int approach(int now, int target) {
    if (now < target) return min(now + RAMP_STEP, target);
    if (now > target) return max(now - RAMP_STEP, target);
    return now;
}

void setup() {
    pinMode(ML_CTRL, OUTPUT);
    pinMode(MR_CTRL, OUTPUT);
    pinMode(ML_PWM, OUTPUT);
    pinMode(MR_PWM, OUTPUT);
    drive(ML_CTRL, ML_PWM, 0);
    drive(MR_CTRL, MR_PWM, 0);
    Serial.begin(9600);
}

void loop() {
    while (Serial.available()) handleChar((char)Serial.read());

    if (watchdog && millis() - lastCmdMs > TIMEOUT_MS) setTarget(0, 0, false);

    if (millis() - lastRampMs >= RAMP_MS) {
        lastRampMs = millis();
        // Changing direction goes through 0 first: the ramp takes care of it.
        speedL = approach(speedL, targetL);
        speedR = approach(speedR, targetR);
        drive(ML_CTRL, ML_PWM, speedL);
        drive(MR_CTRL, MR_PWM, speedR);
    }
}
