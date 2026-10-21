"""Light Memory: Simon-style. Each triangle has its own note (an A major
chord across the four), sounding the same when shown and when pressed."""

from synth import *

NOTES = {"Up": "A4", "Right": "C#5", "Down": "E5", "Left": "A5"}


def chime(n, dur=0.4):
    # Square plus a triangle an octave down: full, but still chiptune.
    return mix(tone(n, dur, duty=0.5, vol=0.35, release=0.1, decay=0.4),
               tone(note(n) / 2, dur, wave="triangle", vol=0.5, release=0.1, decay=0.5))


SOUNDS = {
    "GameSelected": lambda: seq(
        chime("A4", 0.14), chime("C#5", 0.14), chime("E5", 0.14), chime("A5", 0.14),
        silence(0.06), chime("E5", 0.14), chime("A5", 0.5)),
    **{name: (lambda n=n: chime(n)) for name, n in NOTES.items()},
    # The low razz for a wrong press.
    "Wrong": lambda: mix(tone("A1", 0.6, wave="saw", vol=0.5, vibrato=0.3, vib_rate=20, release=0.05),
                         tone("A#1", 0.6, duty=0.3, vol=0.4, release=0.05)),
    "RoundClear": lambda: melody([("A5", 1), ("C#6", 1), ("E6", 1), ("A6", 2)], 0.05, duty=0.25, vol=0.4),
    "GameOver": lambda: seq(
        chime("A5", 0.2), chime("E5", 0.2), chime("C#5", 0.2), chime("A4", 0.2), silence(0.08),
        mix(chime("A3", 0.7), chime("E4", 0.7) * 0.6)),
}
