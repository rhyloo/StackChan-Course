#pragma once
#include <Arduino.h>

// Audio for the voice demo, over the same UDP link as the video:
//   microphone -> AUDIO_IN (16 kHz) -> relay -> Gemini Live
//   Gemini's voice -> relay -> AUDIO_OUT (24 kHz) -> speaker
// The CoreS3 shares one I2S bus between the microphones and the speaker, so
// it is half-duplex: while the robot speaks it cannot hear.
//
// Two modes, chosen by the relay:
//   hands-free ("listen auto", default): the mic is open whenever the robot
//     isn't speaking; Gemini detects when you start and stop talking.
//   push-to-talk ("listen manual"): the mic opens only with "mic on".

void audioStart();               // registers the link handlers and starts the audio task
bool audioCommand(const char* cmd);   // the audio text commands; for demos that chain their own handler
// Beep from any task. Never call M5.Speaker.tone() outside the audio task: it
// restarts the speaker's I2S driver under the open mic and the robot panics.
void audioBeep(int hz, int ms);
void audioTap();                 // screen tap: mute/unmute (hands-free) or talk/stop (push-to-talk)
bool audioMicOn();
bool audioSpeaking();
bool audioMuted();
bool audioHandsFree();
