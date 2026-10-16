#pragma once
#include <Arduino.h>

// Audio for the voice demo, over the same UDP link as the video:
//   microphone -> AUDIO_IN (16 kHz) -> relay -> Gemini Live
//   Gemini's voice -> relay -> AUDIO_OUT (24 kHz) -> speaker
// The CoreS3 shares one I2S bus between the microphones and the speaker, so
// it is half-duplex: talking stops whatever the robot was saying.

void audioStart();               // registers the link handlers and starts the audio task
void audioSetMic(bool on);       // safe from any task
bool audioMicOn();
bool audioSpeaking();
