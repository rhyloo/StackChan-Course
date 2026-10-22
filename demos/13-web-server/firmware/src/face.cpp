#include "face.h"
#include <M5Unified.h>

const char* const MOOD_NAMES[MOOD_COUNT] = { "neutral", "happy", "sad", "surprised", "angry", "sleepy" };
enum { NEUTRAL, HAPPY, SAD, SURPRISED, ANGRY, SLEEPY };

int moodFromName(const char* name) {
    for (int i = 0; i < MOOD_COUNT; i++)
        if (!strcmp(name, MOOD_NAMES[i])) return i;
    return -1;
}

void faceDraw(int mood) {
    auto& d = M5.Display;
    d.fillRect(0, 0, 320, 196, TFT_BLACK);
    for (int side = 0; side < 2; side++) {
        const int x = side ? 220 : 100, y = 90;
        const int in = side ? -1 : 1;         // towards the middle of the face
        switch (mood) {
        case HAPPY:     d.fillArc(x, y + 8, 10, 17, 180, 360, TFT_WHITE); break;   // ^ ^
        case SAD:       d.fillCircle(x, y + 6, 12, TFT_WHITE);
                        d.drawWideLine(x - 20 * in, y - 14, x + 18 * in, y - 26, 3, TFT_WHITE); break;
        case SURPRISED: d.fillCircle(x, y, 19, TFT_WHITE); break;
        case ANGRY:     d.fillCircle(x, y + 6, 12, TFT_WHITE);
                        d.drawWideLine(x - 20 * in, y - 26, x + 18 * in, y - 12, 3, TFT_WHITE); break;
        case SLEEPY:    d.fillRect(x - 16, y + 4, 32, 6, TFT_WHITE); break;
        default:        d.fillCircle(x, y, 14, TFT_WHITE);
        }
    }
    switch (mood) {
    case HAPPY:     d.fillArc(160, 128, 20, 27, 15, 165, TFT_WHITE); break;    // smile
    case SAD:       d.fillArc(160, 170, 20, 27, 195, 345, TFT_WHITE); break;   // frown
    case SURPRISED: d.fillCircle(160, 145, 14, TFT_WHITE); d.fillCircle(160, 145, 8, TFT_BLACK); break;
    case ANGRY:     d.fillRect(140, 146, 40, 6, TFT_WHITE); break;
    case SLEEPY:    d.fillCircle(160, 146, 5, TFT_WHITE); break;
    default:        d.fillRect(135, 140, 50, 6, TFT_WHITE);
    }
}
