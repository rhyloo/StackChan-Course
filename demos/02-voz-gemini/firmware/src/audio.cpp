#include "audio.h"
#include "link.h"
#include <M5Unified.h>

static constexpr uint8_t T_AUDIO_IN = 0x26;    // robot -> laptop: u32 seq + PCM16LE 16 kHz mono
static constexpr uint8_t T_AUDIO_OUT = 0x27;   // laptop -> robot: u32 seq + PCM16LE 24 kHz mono
static constexpr uint32_t MIC_RATE = 16000;
static constexpr size_t MIC_SAMPLES = 320;     // 20 ms per packet: 640 bytes
static constexpr uint32_t OUT_RATE = 24000;
static constexpr size_t OUT_BLOCK = 2400;      // 100 ms per playRaw call
static constexpr size_t OUT_RING = OUT_RATE * 20;   // 20 s of speech in PSRAM
static constexpr uint32_t OUT_IDLE_MS = 400;   // no AUDIO_OUT for this long = the answer is over
static constexpr uint32_t REOPEN_MIC_MS = 250; // after speaking, wait so the mic doesn't catch the echo

static volatile int s_volume = 120;           // 0-255. Loud + LEDs + Wi-Fi can brown out the robot
static volatile bool s_volumeChanged = false;
static volatile bool s_handsFree = true;
static volatile bool s_muted = false;          // hands-free: the user silenced the mic
static volatile bool s_pushToTalk = false;     // push-to-talk: "mic on" received
static volatile bool s_micOn = false;
static int16_t* s_ring = nullptr;              // AUDIO_OUT jitter buffer
static volatile size_t s_head = 0, s_tail = 0; // written by the network task, read by the audio task
static volatile uint32_t s_lastOutMs = 0;
static volatile bool s_speaking = false;
static volatile uint32_t s_spokeUntilMs = 0;
static uint32_t s_lastOutSeq = 0;

static size_t buffered() { return (s_head + OUT_RING - s_tail) % OUT_RING; }
static bool answerArriving() { return millis() - s_lastOutMs < OUT_IDLE_MS; }

// Should the microphone be open right now?
static bool wantMic() {
    if (!s_handsFree) return s_pushToTalk;
    if (s_muted || s_speaking || buffered() > 0 || answerArriving()) return false;
    return millis() - s_spokeUntilMs > REOPEN_MIC_MS;
}

static void onMessage(uint8_t type, const uint8_t* p, size_t len) {
    if (type != T_AUDIO_OUT || len < 6) return;
    if (!s_handsFree && s_pushToTalk) return;      // push-to-talk: talking wins over listening
    uint32_t seq = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    if (!answerArriving()) s_lastOutSeq = seq - 1;   // new answer
    if ((int32_t)(seq - s_lastOutSeq) <= 0) return;  // duplicate or late
    s_lastOutSeq = seq;
    const int16_t* pcm = (const int16_t*)(p + 4);
    size_t n = (len - 4) / 2;
    for (size_t i = 0; i < n; i++) {
        size_t next = (s_head + 1) % OUT_RING;
        if (next == s_tail) break;               // full: drop the rest
        s_ring[s_head] = pcm[i];
        s_head = next;
    }
    s_lastOutMs = millis();
}

static bool onCommand(const char* cmd) {
    if (!strcmp(cmd, "mic on")) { s_pushToTalk = true; return true; }
    if (!strcmp(cmd, "mic off")) { s_pushToTalk = false; return true; }
    if (!strcmp(cmd, "mic mute")) { s_muted = true; return true; }
    if (!strcmp(cmd, "mic unmute")) { s_muted = false; return true; }
    if (!strcmp(cmd, "listen auto")) { s_handsFree = true; return true; }
    if (!strcmp(cmd, "listen manual")) { s_handsFree = false; s_pushToTalk = false; return true; }
    if (!strncmp(cmd, "volume ", 7)) {
        s_volume = constrain(atoi(cmd + 7), 0, 255);
        s_volumeChanged = true;
        return true;
    }
    return false;
}

// The only task that touches M5.Mic and M5.Speaker. It blocks in Mic.record()
// while listening and sleeps 10 ms between checks while speaking.
static void audioTask(void*) {
    static int16_t mic[3][MIC_SAMPLES];
    static int16_t out[3][OUT_BLOCK];
    uint8_t pkt[4 + MIC_SAMPLES * 2];
    uint32_t micSeq = 0;
    int micIdx = 0, queued = 0, outIdx = 0;
    for (;;) {
        bool listen = wantMic();
        if (listen && !s_micOn) {
            M5.Speaker.stop();
            M5.Speaker.end();
            if (!s_handsFree) { s_tail = s_head; s_speaking = false; }   // push-to-talk cuts the answer
            M5.Mic.begin();
            s_micOn = true;
            micIdx = queued = 0;
        } else if (!listen && s_micOn) {
            M5.Mic.end();
            M5.Speaker.begin();
            M5.Speaker.setVolume(s_volume);
            s_micOn = false;
        }

        if (s_micOn) {
            // record() queues the buffer and returns; with two in flight, the
            // one queued two calls ago is complete. It paces this loop at 20 ms.
            M5.Mic.record(mic[micIdx], MIC_SAMPLES, MIC_RATE);
            int ready = (micIdx + 1) % 3;
            micIdx = (micIdx + 1) % 3;
            if (++queued >= 3) {
                pkt[0] = micSeq >> 24; pkt[1] = micSeq >> 16; pkt[2] = micSeq >> 8; pkt[3] = micSeq;
                memcpy(pkt + 4, mic[ready], MIC_SAMPLES * 2);
                linkSend(T_AUDIO_IN, pkt, sizeof(pkt));
                micSeq++;
            }
            continue;
        }

        if (s_volumeChanged) {
            s_volumeChanged = false;
            M5.Speaker.setVolume(s_volume);
        }

        // --- speaker: play from the jitter buffer in 100 ms blocks ---
        size_t avail = buffered();
        if (!s_speaking && (avail >= OUT_RATE / 5 || (avail > 0 && !answerArriving()))) s_speaking = true;   // 200 ms cushion
        if (s_speaking && avail > 0 && M5.Speaker.isPlaying(0) < 2) {
            size_t n = avail < OUT_BLOCK ? avail : OUT_BLOCK;
            for (size_t i = 0; i < n; i++) { out[outIdx][i] = s_ring[s_tail]; s_tail = (s_tail + 1) % OUT_RING; }
            M5.Speaker.playRaw(out[outIdx], n, OUT_RATE, false, 1, 0);
            outIdx = (outIdx + 1) % 3;
        } else if (s_speaking && avail == 0 && !answerArriving() && !M5.Speaker.isPlaying(0)) {
            s_speaking = false;
            s_spokeUntilMs = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void audioStart() {
    s_ring = (int16_t*)heap_caps_malloc(OUT_RING * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    linkSetCommandHandler(onCommand);
    linkSetMessageHandler(onMessage);
    M5.Speaker.setVolume(s_volume);
    xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 1);
}

void audioTap() {
    if (s_handsFree) s_muted = !s_muted;
    else s_pushToTalk = !s_pushToTalk;
}
bool audioMicOn() { return s_micOn; }
bool audioSpeaking() { return s_speaking; }
bool audioMuted() { return s_handsFree && s_muted; }
bool audioHandsFree() { return s_handsFree; }
