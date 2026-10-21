"""Shape Fit: a game show. Clean triangle and pulse tones, a ticking clock,
a camera shutter at time-up and a verdict sting per grade. MISS is a soft
"aww" rather than a buzzer, since the players are mostly kids."""

from synth import *


def chord(names, dur, wave="triangle", vol=0.4, **kw):
    return mix(*[tone(n, dur, wave, vol=vol, **kw) for n in names])


def click(freq, vol=0.5):
    """A woodblock: a pitched blip over a tiny knock of noise."""
    return mix(
        tone(freq, 0.05, "triangle", vol=vol, decay=0.012, release=0.01),
        noise(0.02, vol=vol * 0.5, decay=0.004, smooth=0.5, seed=4),
    )


SOUNDS = {
    "GameSelected": lambda: seq(
        melody([("G4", 1), ("C5", 1), ("E5", 1), ("G5", 1)], 0.09, "pulse", duty=0.25, vol=0.4),
        chord(["C5", "E5", "G5", "C6"], 0.5, "pulse", duty=0.25, vol=0.25, decay=0.25),
    ),
    # The pose name appears: a bright two-note "ta-da".
    "Pose": lambda: seq(
        tone("E5", 0.07, "pulse", duty=0.25, vol=0.4),
        chord(["A5", "C#6", "E6"], 0.22, "pulse", duty=0.25, vol=0.25, decay=0.1),
    ),
    "Tick": lambda: click("G5"),
    # The last three seconds: higher, with a second knock so it reads as urgent.
    "TickUrgent": lambda: mix(click("D6", 0.6), seq(silence(0.07), click("D6", 0.35))),
    # Time's up: a camera shutter, two fast clicks of noise with a flash whine.
    "Snap": lambda: mix(
        noise(0.03, vol=0.6, decay=0.008, seed=7),
        seq(silence(0.06), noise(0.04, vol=0.5, decay=0.01, smooth=0.3, seed=8)),
        tone(1800, 0.15, "sine", vol=0.12, freq_end=3200, decay=0.06),
    ),
    "Perfect": lambda: seq(
        melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 1), ("E6", 1)], 0.06, "pulse", duty=0.25, vol=0.4),
        chord(["C6", "E6", "G6", "C7"], 0.7, "pulse", duty=0.25, vol=0.25, decay=0.3, vibrato=0.15),
    ),
    "Great": lambda: seq(
        melody([("C5", 1), ("E5", 1), ("G5", 1)], 0.07, "pulse", duty=0.25, vol=0.4),
        chord(["C6", "E6", "G6"], 0.45, "pulse", duty=0.25, vol=0.25, decay=0.2),
    ),
    "Good": lambda: melody([("G5", 1), ("C6", 2.5)], 0.09, "triangle", vol=0.5, legato=0.9),
    "Ok": lambda: melody([("E5", 1), ("G5", 2)], 0.09, "triangle", vol=0.5, legato=0.9),
    # A gentle falling "aww", not a buzzer.
    "Miss": lambda: seq(
        tone("E4", 0.16, "triangle", vol=0.5),
        tone("Eb4", 0.16, "triangle", vol=0.5),
        tone("D4", 0.45, "triangle", vol=0.5, freq_end="C#4", vibrato=0.25, release=0.15),
    ),
    "Total": lambda: seq(
        melody([("G4", 1), ("C5", 1), ("E5", 1), ("G5", 2), ("E5", 1), ("G5", 1)], 0.1, "pulse", duty=0.25, vol=0.4),
        mix(
            chord(["C5", "E5", "G5", "C6"], 0.9, "pulse", duty=0.25, vol=0.22, decay=0.45, vibrato=0.15),
            tone("C3", 0.9, "triangle", vol=0.5, decay=0.45),
        ),
    ),
}
