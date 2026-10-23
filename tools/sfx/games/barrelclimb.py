"""Barrel Climb: girders, ladders, an ape throwing barrels. Bouncy square
waves in the spirit of the 1981 original, without copying its tunes."""

from synth import *


def thud(dur=0.12, low="C2", vol=0.6):
    # A dull low knock: smoothed noise over a fast falling sine.
    return mix(noise(dur, vol=vol, smooth=0.97, decay=dur / 3),
               tone(low, dur, wave="sine", freq_end="A1", vol=vol, decay=dur / 3))


def with_bass(lead, bass):
    return mix(lead, bass * 0.6)


SOUNDS = {
    "GameSelected": lambda: with_bass(
        melody([("C5", 1), ("-", 0.5), ("C5", 0.5), ("D5", 1), ("E5", 1), ("G5", 1), ("-", 0.5), ("E5", 0.5), ("G5", 2)],
               0.11, duty=0.25, vol=0.5),
        melody([("C3", 2), ("G2", 2), ("C3", 2), ("G2", 1), ("C3", 2.5)], 0.11, wave="triangle", vol=0.6, legato=0.7)),
    "LevelStart": lambda: with_bass(
        melody([("G4", 1), ("C5", 1), ("E5", 1), ("G5", 1), ("-", 0.5), ("E5", 0.5), ("G5", 2.5)], 0.1, duty=0.25),
        melody([("C3", 2), ("G2", 2), ("C3", 3.5)], 0.1, wave="triangle", vol=0.6, legato=0.7)),
    # Soft and short: it repeats three or four times a second.
    "Step": lambda: tone("E3", 0.035, wave="triangle", freq_end="C3", vol=0.5, decay=0.015),
    "Climb": lambda: tone("C5", 0.045, duty=0.25, vol=0.4, decay=0.02),
    "Climb2": lambda: tone("E5", 0.045, duty=0.25, vol=0.4, decay=0.02),
    "Jump": lambda: tone("C4", 0.16, duty=0.25, freq_end="C6", curve=0.6, decay=0.12),
    "Score": lambda: seq(tone("E6", 0.05, duty=0.25), tone("B6", 0.12, duty=0.25, decay=0.06)),
    "Throw": lambda: mix(thud(0.2, "E2", 0.7), tone("A3", 0.12, wave="saw", freq_end="A2", vol=0.25, decay=0.05)),
    "BarrelLand": lambda: thud(0.09, "G2", 0.5),
    "Fall": lambda: tone("C6", 0.35, wave="triangle", freq_end="C4", curve=1.4, vol=0.5),
    "Die": lambda: seq(
        mix(noise(0.08, vol=0.4, smooth=0.6), tone("C5", 0.08)),
        tone("B4", 0.7, duty=0.25, freq_end="C3", vibrato=1.5, vib_rate=14, curve=0.8, release=0.1),
        silence(0.08),
        melody([("C3", 1), ("G2", 2)], 0.13, wave="triangle", vol=0.7)),
    "Rescue": lambda: with_bass(
        melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 2), ("-", 1), ("A5", 1), ("C6", 1), ("D6", 1), ("E6", 4)],
               0.12, duty=0.25, vol=0.5),
        melody([("C3", 2), ("G3", 2), ("C3", 2), ("F3", 2), ("C3", 5)], 0.12, wave="triangle", vol=0.6, legato=0.7)),
    # The end show's tune: a bouncy fanfare up to a held top C, about 4.5 s,
    # under the headline and the stats.
    "EndShow": lambda: with_bass(
        melody([("C5", 1), ("C5", 0.5), ("D5", 0.5), ("E5", 1), ("C5", 1), ("G5", 2), ("E5", 1), ("G5", 1),
                ("A5", 1), ("G5", 0.5), ("F5", 0.5), ("E5", 1), ("D5", 1), ("C5", 1), ("E5", 1), ("G5", 2),
                ("F5", 1), ("A5", 1), ("C6", 2), ("B5", 1), ("A5", 1), ("G5", 2),
                ("E5", 0.5), ("F5", 0.5), ("G5", 1), ("A5", 0.5), ("B5", 0.5), ("C6", 1), ("-", 0.5), ("G5", 0.5),
                ("C6", 4)],
               0.13, duty=0.25, vol=0.5),
        melody([("C3", 2), ("G2", 2), ("C3", 2), ("G2", 2), ("F3", 2), ("G3", 2), ("C3", 2), ("E3", 2),
                ("F3", 2), ("F3", 2), ("G3", 2), ("G2", 2), ("C3", 2), ("G2", 2), ("C3", 5)],
               0.13, wave="triangle", vol=0.6, legato=0.7)),
    "GameOver": lambda: with_bass(
        melody([("G4", 1), ("F#4", 1), ("F4", 1), ("E4", 1), ("-", 0.5), ("C4", 3)], 0.2, duty=0.25, vibrato=0.2),
        melody([("C3", 2), ("B2", 2), ("C2", 3.5)], 0.2, wave="triangle", vol=0.7)),
}
