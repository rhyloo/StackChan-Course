#pragma once
// The robot's face (same moods as demo 02.1). Screen: call from loop() only.
static constexpr int MOOD_COUNT = 6;
extern const char* const MOOD_NAMES[MOOD_COUNT];   // neutral, happy, sad, surprised, angry, sleepy
int moodFromName(const char* name);                 // -1 if unknown
void faceDraw(int mood);                            // eyes and mouth, in the top 196 px
