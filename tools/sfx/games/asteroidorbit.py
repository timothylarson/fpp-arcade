"""Asteroid Orbit: spacey chiptune - triangle and sine glides for the ship,
low-passed noise for the rocks, a wobbling sweep for the black hole."""

from synth import *


def crunch(dur, smooth, low, high, seed=1):
    """Rock breaking: a dull noise burst over a falling square."""
    return mix(
        noise(dur, vol=0.6, smooth=smooth, decay=dur / 3, seed=seed),
        tone(high, dur * 0.8, wave="square", duty=0.25, vol=0.25, freq_end=low, decay=dur / 4),
    )


SOUNDS = {
    "GameSelected": lambda: mix(
        melody([("C5", 1), ("G5", 1), ("C6", 1), ("E6", 1), ("G6", 1), ("-", 1), ("C7", 3)], 0.11,
               wave="triangle", vol=0.6),
        melody([("C3", 4), ("G3", 4)], 0.11, wave="square", duty=0.25, vol=0.2),
    ),
    "Fire": lambda: tone("C7", 0.08, wave="pulse", duty=0.25, vol=0.4, freq_end="C6", decay=0.05),
    "Empty": lambda: seq(noise(0.02, vol=0.4, release=0.01), silence(0.03), noise(0.015, vol=0.25, release=0.01, seed=2)),
    "Thrust": lambda: noise(0.13, vol=0.5, smooth=0.93, attack=0.03, release=0.08, seed=3),
    "BreakLarge": lambda: crunch(0.32, 0.9, "C2", "G3", seed=4),
    "BreakMedium": lambda: crunch(0.2, 0.75, "G2", "D4", seed=5),
    "BreakSmall": lambda: crunch(0.12, 0.45, "D3", "A4", seed=6),
    "Land": lambda: seq(tone("G4", 0.06, wave="triangle", vol=0.6), tone("C5", 0.1, wave="triangle", vol=0.6, decay=0.06)),
    "Bounce": lambda: tone("C4", 0.14, wave="triangle", vol=0.6, freq_end="C5", curve=0.5, decay=0.08, vibrato=0.6, vib_rate=30),
    "TakeOff": lambda: mix(
        tone("C4", 0.2, wave="square", duty=0.25, vol=0.3, freq_end="C6", curve=0.7),
        noise(0.2, vol=0.3, smooth=0.85, attack=0.02, seed=7),
    ),
    "Reload": lambda: tone("E6", 0.035, wave="pulse", duty=0.125, vol=0.4, release=0.01),
    "CrystalFuel": lambda: melody([("C6", 1), ("E6", 1), ("G6", 1.5)], 0.045, wave="triangle", vol=0.6),
    "CrystalAmmo": lambda: melody([("A5", 1), ("A6", 2)], 0.05, wave="square", duty=0.25, vol=0.45),
    "CrystalPulse": lambda: mix(
        tone("G6", 0.22, wave="sine", vol=0.5, freq_end="D7", vibrato=0.5, vib_rate=18, decay=0.12),
        tone("G5", 0.22, wave="triangle", vol=0.3, decay=0.1),
    ),
    "Shockwave": lambda: mix(
        tone("C4", 0.45, wave="sine", vol=0.6, freq_end="C2", decay=0.2),
        noise(0.45, vol=0.4, smooth=0.8, attack=0.05, decay=0.15, seed=8),
        tone("G4", 0.15, wave="square", duty=0.25, vol=0.15, freq_end="G3"),
    ),
    "Crash": lambda: mix(
        noise(0.65, vol=0.7, smooth=0.6, decay=0.2, seed=9),
        tone("A4", 0.5, wave="square", vol=0.3, freq_end="A1", decay=0.2),
    ),
    "HoleOpen": lambda: mix(
        tone("E5", 0.8, wave="triangle", vol=0.5, freq_end="E3", vibrato=1.5, vib_rate=7, attack=0.05, release=0.2),
        tone("B4", 0.8, wave="sine", vol=0.3, freq_end="B2", vibrato=1.5, vib_rate=5, attack=0.1, release=0.2),
    ),
    "Warp": lambda: mix(
        tone("C3", 0.42, wave="square", duty=0.25, vol=0.3, freq_end="C7", curve=1.6, release=0.05),
        tone("G3", 0.42, wave="triangle", vol=0.4, freq_end="G6", curve=1.6, vibrato=0.4, vib_rate=20),
    ),
    "SectorStart": lambda: mix(
        melody([("G5", 1), ("C6", 1), ("D6", 1), ("G6", 3)], 0.1, wave="square", duty=0.25, vol=0.4),
        melody([("C4", 3), ("G4", 3)], 0.1, wave="triangle", vol=0.5),
    ),
    # Halloween's, for the harvest moon: the warp's sweep, then the opening of
    # Beethoven's Moonlight Sonata - its rippling triplets on the triangle,
    # the bass under them and, in the last two bars, the tolling G# on a sine
    # - coming to rest on C# minor.
    "EndShowHalloween": lambda: mix(
        seq(tone("C3", 0.45, wave="triangle", vol=0.45, freq_end="C6", curve=1.6, release=0.05),
            melody([(n, 1) for n in ["G#3", "C#4", "E4"] * 4 + ["A3", "C#4", "E4"] * 2 + ["A3", "D4", "F#4"] * 2
                    + ["G#3", "C4", "F#4", "G#3", "C#4", "E4", "G#3", "C#4", "D#4", "F#3", "C4", "D#4"]
                    + ["E3", "G#3", "C#4"] * 4], 0.11, wave="triangle", vol=0.55, legato=0.9)),
        seq(silence(0.45),
            melody([("C#3", 12), ("B2", 12), ("A2", 6), ("F#2", 6), ("G#2", 6), ("C#2", 6)], 0.11,
                   wave="square", duty=0.25, vol=0.18, legato=0.95)),
        seq(silence(0.45 + 0.11 * 24),
            melody([("G#5", 2.25), ("G#5", 0.75), ("G#5", 3), ("G#5", 2.25), ("G#5", 0.75), ("G#5", 3),
                    ("G#5", 3), ("A5", 3), ("G#5", 3), ("C#6", 3)], 0.11, wave="sine", vol=0.4, legato=0.9)),
    ),
    # The end show: the warp's rising sweep, then a bright triangle fanfare over
    # a square bass, with a sine shimmer high above, landing on a long C.
    "EndShow": lambda: mix(
        seq(
            tone("C3", 0.45, wave="triangle", vol=0.45, freq_end="C6", curve=1.6, release=0.05),
            melody([("C6", 1), ("G5", 1), ("C6", 1), ("E6", 1), ("G6", 2), ("E6", 1), ("G6", 1),
                    ("A6", 2), ("G6", 1), ("E6", 1), ("F6", 2), ("D6", 2),
                    ("E6", 1), ("G6", 1), ("C7", 1), ("B6", 1), ("C7", 6)], 0.14, wave="triangle", vol=0.6),
        ),
        seq(
            silence(0.45),
            melody([("C3", 4), ("G3", 4), ("F3", 4), ("G3", 4), ("C3", 8)], 0.14, wave="square", duty=0.25,
                   vol=0.2),
        ),
        seq(
            silence(0.45 + 0.14 * 16),
            tone("G7", 1.4, wave="sine", vol=0.15, vibrato=0.5, vib_rate=9, attack=0.2, release=0.6),
        ),
    ),
    "GameOver": lambda: mix(
        melody([("G5", 2), ("Eb5", 2), ("C5", 2), ("G4", 2), ("Ab4", 3), ("G4", 5)], 0.11, wave="triangle", vol=0.6),
        melody([("C3", 4), ("Ab2", 4), ("G2", 8)], 0.11, wave="square", duty=0.25, vol=0.2),
    ),
    # A ship earned for clearing sectors: a shimmering rise.
    "ExtraLife": lambda: mix(
        melody([("C6", 1), ("E6", 1), ("G6", 1), ("C7", 1), ("E7", 3)], 0.07, wave="square", duty=0.25, vol=0.4),
        tone("C5", 0.5, wave="sine", vol=0.4, freq_end="C6", vibrato=0.4, vib_rate=16, decay=0.3),
    ),
}
