"""Flap: a little bird through pipe gaps. Light, chirpy pulse waves."""

from synth import *

SOUNDS = {
    "GameSelected": lambda: mix(
        melody([("G5", 1), ("C6", 1), ("E6", 1), ("-", 1), ("D6", 1), ("E6", 1), ("G6", 3)], 0.09, duty=0.125),
        melody([("C4", 3), ("G3", 2), ("C4", 4)], 0.09, wave="triangle", vol=0.4)),
    "Start": lambda: melody([("C5", 1), ("E5", 1), ("G5", 2)], 0.05, duty=0.125),
    # A quick upward flutter: air over a chirp.
    "Flap": lambda: mix(noise(0.07, vol=0.25, smooth=0.6, decay=0.03),
                        tone("A4", 0.07, duty=0.125, freq_end="E5", vol=0.35, decay=0.03)),
    "Score": lambda: seq(tone("B5", 0.04, duty=0.25), tone("E6", 0.1, duty=0.25, decay=0.05)),
    "Crash": lambda: seq(
        mix(noise(0.12, vol=0.6, smooth=0.3, decay=0.05), tone("C4", 0.12, wave="square", freq_end="C3")),
        tone("G4", 0.35, duty=0.25, freq_end="C3", curve=0.7, vol=0.4, release=0.06)),
    "GameOver": lambda: mix(
        melody([("E5", 1), ("C5", 1), ("G4", 1), ("-", 1), ("F4", 1), ("E4", 1), ("D4", 1), ("C4", 3)], 0.12, duty=0.125),
        melody([("C3", 4), ("G2", 3), ("C3", 3)], 0.12, wave="triangle", vol=0.45)),
    # The end show's victory lap: a flutter, then We Wish You a Merry
    # Christmas, chirped high.
    "EndShow": lambda: seq(
        mix(noise(0.07, vol=0.25, smooth=0.6, decay=0.03), tone("A5", 0.07, duty=0.125, freq_end="E6", vol=0.35)),
        silence(0.05),
        mix(melody([("G5", 1), ("C6", 1), ("C6", 0.5), ("D6", 0.5), ("C6", 0.5), ("B5", 0.5), ("A5", 1), ("A5", 1),
                    ("A5", 1), ("D6", 1), ("D6", 0.5), ("E6", 0.5), ("D6", 0.5), ("C6", 0.5), ("B5", 1), ("G5", 1),
                    ("G5", 1), ("E6", 1), ("E6", 0.5), ("F6", 0.5), ("E6", 0.5), ("D6", 0.5), ("C6", 1), ("A5", 1),
                    ("G5", 0.5), ("G5", 0.5), ("A5", 1), ("D6", 1), ("B5", 1), ("C6", 3)], 0.18, duty=0.125),
            seq(silence(0.18), melody([("C3", 3), ("F3", 3), ("D3", 3), ("G3", 3), ("E3", 3), ("A3", 3),
                                       ("F3", 2), ("G3", 2), ("C3", 3)], 0.18, wave="triangle", vol=0.45, legato=0.7)))),
}
