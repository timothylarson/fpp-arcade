"""Stacker: drop a sliding row onto the tower. Each drop is a plucked note
one step higher up a pentatonic scale as the tower nears the top."""

from synth import *

# Two octaves and a bit of C major pentatonic, for Drop1..Drop16.
SCALE = ["C4", "D4", "E4", "G4", "A4", "C5", "D5", "E5", "G5", "A5", "C6", "D6", "E6", "G6", "A6", "C7"]


def drop(n):
    return mix(noise(0.02, vol=0.3, smooth=0.5),
               tone(n, 0.12, duty=0.25, vol=0.5, decay=0.05),
               tone(n, 0.12, wave="triangle", vol=0.4, decay=0.06) * 0.5)


SOUNDS = {
    "GameSelected": lambda: mix(
        melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 1), ("E6", 1), ("G6", 3)], 0.09, duty=0.25),
        melody([("C3", 3), ("G3", 3), ("C4", 2)], 0.09, wave="triangle", vol=0.4)),
    "LevelStart": lambda: melody([("G4", 1), ("C5", 1), ("G5", 2)], 0.09, duty=0.25),
    "Perfect": lambda: seq(silence(0.04), melody([("E6", 1), ("G6", 1), ("C7", 2)], 0.045, duty=0.125, vol=0.4)),
    "Chop": lambda: mix(noise(0.08, vol=0.35, smooth=0.8, decay=0.04),
                        tone("G4", 0.2, duty=0.125, freq_end="G3", vol=0.3, decay=0.08)),
    "Miss": lambda: seq(
        mix(noise(0.06, vol=0.4, smooth=0.7), tone("E4", 0.06)),
        tone("E4", 0.55, duty=0.25, freq_end="E2", curve=0.8, vibrato=0.6, vib_rate=10, release=0.08)),
    "LevelClear": lambda: mix(
        melody([("C5", 1), ("C5", 1), ("C5", 1), ("E5", 2), ("C5", 1), ("G5", 1), ("-", 1),
                ("A5", 1), ("G5", 1), ("C6", 4)], 0.1, duty=0.25),
        melody([("C3", 3), ("E3", 3), ("G3", 3), ("C3", 5)], 0.1, wave="triangle", vol=0.4, legato=0.7)),
    "GameOver": lambda: mix(
        melody([("C5", 1), ("G4", 1), ("E4", 1), ("A4", 1.5), ("B4", 1.5), ("A4", 1), ("G#4", 1), ("Bb4", 1), ("G#4", 1), ("G4", 3)],
               0.11, duty=0.25),
        melody([("C3", 3), ("F3", 4), ("E3", 3), ("C3", 3)], 0.11, wave="triangle", vol=0.4)),
}
SOUNDS.update({f"Drop{i + 1}": (lambda n=n: drop(n)) for i, n in enumerate(SCALE)})
