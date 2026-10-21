"""Sound effects and a dance song, synthesized with numpy (no files).

Everything comes out in the format the robot's speaker plays: 16-bit PCM,
24 kHz, mono (the same as Gemini's voice, message AUDIO_OUT).
The song is original: kick on every beat, claps, hi-hat, bass and an arpeggio.
"""
import numpy as np

RATE = 24000
BPM = 124
BEAT = 60.0 / BPM                      # seconds per beat
BARS = 8
CHORDS = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)]   # Am, F, C, G (MIDI notes)


def _hz(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def _t(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def _pcm(x, volume=0.6):
    x = x / max(1e-9, np.max(np.abs(x))) * volume
    return (x * 32767).astype("<i2").tobytes()


def pew():
    """Laser shot: a beep that falls from 1800 to 250 Hz in 0.22 s."""
    t = _t(0.22)
    f = 1800 * (250 / 1800) ** (t / t[-1])         # exponential fall: sounds more "laser" than a linear one
    phase = 2 * np.pi * np.cumsum(f) / RATE
    wave = np.sign(np.sin(phase)) * 0.6 + np.sin(phase) * 0.4
    return _pcm(wave * np.exp(-t * 9))


def horn():
    """A "meep-meep!": two honks of a two-note horn."""
    parts = []
    for _ in range(2):
        t = _t(0.16)
        wave = sum(np.sign(np.sin(2 * np.pi * f * t)) for f in (392, 494))
        parts += [wave * np.minimum(1, (0.16 - t) * 60), np.zeros(int(0.08 * RATE))]
    return _pcm(np.concatenate(parts), 0.5)


def _kick():
    t = _t(0.18)
    f = 50 + 110 * np.exp(-t * 30)
    return np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 14)


def _noise(sec, decay, bright):
    t = _t(sec)
    r = np.random.default_rng(7).standard_normal(len(t))
    if bright:
        r = np.diff(r, prepend=0)                   # removes the lows: hi-hat
    return r * np.exp(-t * decay)


def song():
    """The whole song, as a list of chunks, one per beat."""
    n_beat = int(BEAT * RATE)
    total = BARS * 4 * n_beat
    mix = np.zeros(total + RATE)
    kick, claps, hihat = _kick(), _noise(0.12, 25, False), _noise(0.04, 80, True)

    def put(x, at, vol):
        mix[at:at + len(x)] += x * vol

    for beat in range(BARS * 4):
        start = beat * n_beat
        bar, pulse = divmod(beat, 4)
        chord = CHORDS[bar % 4]
        put(kick, start, 1.0)
        if pulse in (1, 3):
            put(claps, start, 0.35)
        put(hihat, start + n_beat // 2, 0.25)
        # Bass: two eighth notes per beat, the second one an octave up.
        for k, jump in enumerate((0, 12)):
            t = _t(BEAT / 2)
            note = chord[0] - 24 + jump
            wave = np.sin(2 * np.pi * _hz(note) * t) + 0.3 * np.sin(4 * np.pi * _hz(note) * t)
            put(wave * np.exp(-t * 6), start + k * n_beat // 2, 0.45)
        # Sixteenth-note arpeggio from bar 3 on (the tune "comes in").
        if bar >= 2:
            for k in range(4):
                t = _t(BEAT / 4)
                note = chord[k % 3] + 12 + (12 if k == 3 else 0)
                wave = np.sign(np.sin(2 * np.pi * _hz(note) * t))
                put(wave * np.exp(-t * 12), start + k * n_beat // 4, 0.12)
    mix = mix[:total]
    mix = mix / np.max(np.abs(mix)) * 0.6
    pcm = (mix * 32767).astype("<i2").tobytes()
    step = n_beat * 2                                # bytes per beat
    return [pcm[i:i + step] for i in range(0, len(pcm), step)]
