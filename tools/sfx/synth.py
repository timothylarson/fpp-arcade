"""A tiny retro sound-effect synthesizer for the arcade games' bundled sounds.

Each game has a recipe module in games/<game>.py with a SOUNDS dict mapping a
sound name (the name the game's code plays, e.g. "LineClear") to a function
that returns a mono float array at RATE. make_sounds.py renders them to
sounds/<game>/<name>.wav in the plugin, as 16-bit mono 44.1 kHz WAV.

Building blocks, all returning numpy float arrays in -1..1:

    tone(freq, dur, wave="square", ...)   one note, optionally sliding to freq_end
    noise(dur, ...)                       a burst of noise (explosions, hits)
    melody([("C5", 1), ("E5", 1)], beat)  a run of notes; "-" is a rest
    seq(a, b, ...)                        one after another
    mix(a, b, ...)                        on top of each other, from the start
    silence(dur)
    note("A4") -> 440.0

Waves: square (with duty), pulse (duty 0.125/0.25), triangle, saw, sine. Keep
effects short (most under 0.3 s) so rapid repeats don't pile up, and jingles
under about 2 s. make_sounds.py normalises every sound to the same peak, so
write them at whatever level is convenient.
"""

import numpy as np

RATE = 44100

_NOTES = {"C": -9, "D": -7, "E": -5, "F": -4, "G": -2, "A": 0, "B": 2}


def note(name):
    """'A4' -> 440.0; accepts sharps and flats: 'C#5', 'Bb3'. Numbers pass through."""
    if isinstance(name, (int, float)):
        return float(name)
    n = _NOTES[name[0].upper()]
    rest = name[1:]
    if rest and rest[0] in "#b":
        n += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    octave = int(rest)
    return 440.0 * 2 ** ((n + 12 * (octave - 4)) / 12)


def silence(dur):
    return np.zeros(int(RATE * dur))


def _envelope(n, attack, release, decay=None):
    t = np.arange(n) / RATE
    env = np.ones(n)
    a = min(n, max(1, int(RATE * attack)))
    r = min(n, max(1, int(RATE * release)))
    env[:a] *= np.linspace(0, 1, a)
    env[-r:] *= np.linspace(1, 0, r)
    if decay:
        env *= np.exp(-t / decay)
    return env


def tone(freq, dur, wave="square", duty=0.5, vol=0.5, attack=0.003, release=0.02,
         freq_end=None, decay=None, vibrato=0.0, vib_rate=6.0, curve=1.0):
    """One note of `dur` seconds. freq_end slides the pitch (curve > 1 slides
    late, < 1 early). decay is an exponential fade time constant in seconds.
    vibrato is depth in semitones."""
    n = max(1, int(RATE * dur))
    t = np.arange(n) / RATE
    f0 = note(freq)
    if freq_end is None:
        f = np.full(n, f0)
    else:
        f1 = note(freq_end)
        x = (t / max(dur, 1e-6)) ** curve
        f = f0 * (f1 / f0) ** x   # glide in pitch, not in Hz
    if vibrato:
        f = f * 2 ** (vibrato * np.sin(2 * np.pi * vib_rate * t) / 12)
    phase = np.cumsum(f) / RATE % 1.0
    if wave in ("square", "pulse"):
        w = np.where(phase < duty, 1.0, -1.0)
    elif wave == "triangle":
        w = 4 * np.abs(phase - 0.5) - 1
    elif wave == "saw":
        w = 2 * phase - 1
    elif wave == "sine":
        w = np.sin(2 * np.pi * phase)
    else:
        raise ValueError(wave)
    return w * vol * _envelope(n, attack, release, decay)


def noise(dur, vol=0.5, attack=0.002, release=0.05, decay=None, smooth=0.0, seed=1):
    """White noise; smooth 0..0.99 low-passes it (higher is duller, a thud
    rather than a hiss)."""
    n = max(1, int(RATE * dur))
    rng = np.random.default_rng(seed)
    w = rng.uniform(-1, 1, n)
    if smooth:
        out = np.empty(n)
        acc = 0.0
        for i in range(n):
            acc = smooth * acc + (1 - smooth) * w[i]
            out[i] = acc
        w = out / max(1e-9, np.max(np.abs(out)))
    return w * vol * _envelope(n, attack, release, decay)


def melody(notes, beat, wave="square", duty=0.5, vol=0.5, legato=0.85, **kw):
    """notes: [(name or freq or "-", beats), ...]. Each note sounds for
    `legato` of its length, so repeated notes stay distinct."""
    parts = []
    for name, beats in notes:
        d = beat * beats
        if name == "-":
            parts.append(silence(d))
        else:
            parts.append(seq(tone(name, d * legato, wave=wave, duty=duty, vol=vol, **kw), silence(d * (1 - legato))))
    return seq(*parts)


def seq(*parts):
    return np.concatenate([p for p in parts if len(p)]) if parts else silence(0)


def mix(*parts):
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[: len(p)] += p
    return out
