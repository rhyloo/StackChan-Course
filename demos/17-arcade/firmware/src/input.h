#pragma once
// Demo 17 - The controls: a KY-023 joystick and two buttons on the Grove ports.
//   joystick VRx -> G9, VRy -> G8 (PORT.B), each through a 10k/20k divider (the port gives 5 V)
//   button A -> G17, button B -> G18 (PORT.C), to GND: the internal pull-up does the rest
//   joystick SW -> G2 (PORT.A), optional: pause
// The screen also counts: a tap is a "touch" (menus, pause).
#include <Arduino.h>

struct Input {
    float x, y;                        // -1..1; + = right, down. 0 in the dead zone
    bool left, right, up, down;        // held
    bool leftP, rightP, upP, downP;    // just pressed, and again every REPEAT_MS while held (menus, Tetris)
    bool a, b, sw;                     // held
    bool aP, bP, swP;                  // just pressed
    bool touchP;                       // a tap on the screen, at tx, ty
    int tx, ty;
};

void inputBegin();                     // the joystick must be at rest: its center is measured here
Input inputRead();                     // once per frame
bool inputHasJoystick();               // false if both axes read nothing at start (not wired)
void inputRaw(int* xMv, int* yMv);     // the last voltages, for the controls screen
