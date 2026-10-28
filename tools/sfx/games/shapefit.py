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
    # Halloween's: the same conga, but creeping in a minor key (step, step,
    # step, kick) over the walking bass, the woodblock roll rattling like
    # bones, and a held diminished chord with a ghostly wobble.
    "EndShowHalloween": lambda: seq(
        mix(
            melody([("A4", 1), ("C5", 1), ("E5", 1), ("A5", 2), ("-", 1),
                    ("Bb4", 1), ("D5", 1), ("F5", 1), ("Bb5", 2), ("-", 1),
                    ("G#4", 1), ("B4", 1), ("E5", 1), ("G#5", 2), ("-", 1)], 0.13, "pulse", duty=0.25, vol=0.4),
            melody([("A2", 2), ("E3", 2), ("A2", 2), ("Bb2", 2), ("F3", 2), ("Bb2", 2),
                    ("E2", 2), ("B2", 2), ("E3", 2)], 0.13, "triangle", vol=0.5),
        ),
        seq(*[click("C6" if k % 2 else "F#5", 0.3 + 0.03 * k) for k in range(10)]),
        mix(
            chord(["A4", "C5", "Eb5", "F#5", "A5"], 1.8, "pulse", duty=0.25, vol=0.22, decay=0.9, vibrato=0.5),
            tone("A2", 1.8, "triangle", vol=0.5, decay=0.9),
        ),
    ),
    # The end show after a good game: the game-show band's big finish. A
    # bouncy conga riff (step, step, step, kick) over a walking bass, then a
    # drum-roll of woodblocks into a held major chord with the camera's flash.
    "EndShow": lambda: seq(
        mix(
            melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 2), ("-", 1),
                    ("D5", 1), ("F5", 1), ("A5", 1), ("D6", 2), ("-", 1),
                    ("E5", 1), ("G5", 1), ("B5", 1), ("E6", 2), ("-", 1)], 0.12, "pulse", duty=0.25, vol=0.4),
            melody([("C3", 2), ("G3", 2), ("C3", 2), ("D3", 2), ("A3", 2), ("D3", 2),
                    ("E3", 2), ("B3", 2), ("E3", 2)], 0.12, "triangle", vol=0.5),
        ),
        seq(*[click("G5" if k % 2 else "D6", 0.3 + 0.03 * k) for k in range(10)]),
        mix(
            chord(["C5", "E5", "G5", "C6", "E6"], 1.8, "pulse", duty=0.25, vol=0.22, decay=0.9, vibrato=0.15),
            tone("C3", 1.8, "triangle", vol=0.5, decay=0.9),
            tone(1800, 0.2, "sine", vol=0.1, freq_end=3200, decay=0.08),
        ),
    ),
}
