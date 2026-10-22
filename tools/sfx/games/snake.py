"""Snake: a snake-charmer feel - a reedy pulse lead on a Phrygian-ish scale
over a drone - with a quick upward gulp for each bite and a soft tick for a
turn, since turns come often."""

from synth import *


def reed(notes, beat, **kw):
    return melody(notes, beat, wave="pulse", duty=0.25, vol=0.5, vibrato=0.15, vib_rate=7, **kw)


SOUNDS = {
    "GameSelected": lambda: mix(
        reed([("D5", 1), ("Eb5", 1), ("F#5", 2), ("G5", 1), ("F#5", 1), ("Eb5", 1), ("D5", 1),
              ("Eb5", 1), ("F#5", 1), ("Eb5", 1), ("D5", 1), ("C#5", 1), ("D5", 3)], 0.1),
        tone("D3", 1.7, wave="triangle", vol=0.35, release=0.15)),
    "StartGame": lambda: reed([("D5", 1), ("F#5", 1), ("A5", 1), ("D6", 3)], 0.06),
    "Turn": lambda: tone("A5", 0.018, wave="triangle", decay=0.01),
    "Eat": lambda: tone("C5", 0.08, wave="square", duty=0.5, freq_end="C6", curve=0.6, decay=0.08),
    "Milestone": lambda: melody([("D6", 1), ("F#6", 1), ("A6", 1), ("D7", 3)], 0.055,
                                wave="pulse", duty=0.25),
    # The end show for a good game: the charmer's tune again, triumphant, over
    # the drone, climbing to a long high D.
    "EndShow": lambda: mix(
        seq(reed([("D5", 1), ("F#5", 1), ("G5", 1), ("A5", 2), ("G5", 1), ("A5", 1), ("C6", 1),
                  ("A5", 1), ("G5", 1), ("F#5", 2), ("D5", 2)], 0.14),
            reed([("D5", 1), ("F#5", 1), ("A5", 1), ("D6", 1), ("F#6", 1), ("A6", 2), ("D7", 5)], 0.11)),
        tone("D3", 5.0, wave="triangle", vol=0.3, release=0.6)),
    # The crunch of hitting something, then the tune sinking away.
    "GameOver": lambda: seq(
        mix(noise(0.18, smooth=0.5, decay=0.06),
            tone(300, 0.18, wave="square", freq_end=60, decay=0.08)),
        silence(0.08),
        reed([("A4", 1), ("G#4", 1), ("G4", 1), ("F#4", 1), ("F4", 1), ("E4", 1), ("D4", 4)], 0.1,
             legato=0.95)),
}
