"""Tetris: Game Boy flavoured - a thin pulse lead over a triangle bass, with
short clicky effects so the constant moving and rotating stays out of the way
of the line clears."""

from synth import *


def lead_bass(lead, bass, beat):
    return mix(melody(lead, beat, wave="pulse", duty=0.25, vol=0.5),
               melody(bass, beat, wave="triangle", vol=0.45, legato=0.95))


def sweep(start, end, dur, vol=0.5):
    return tone(start, dur, wave="pulse", duty=0.25, vol=vol, freq_end=end, curve=0.7)


SOUNDS = {
    # The opening of Korobeiniki, the folk tune everyone knows as Tetris.
    "GameSelected": lambda: lead_bass(
        [("E5", 2), ("B4", 1), ("C5", 1), ("D5", 2), ("C5", 1), ("B4", 1),
         ("A4", 2), ("A4", 1), ("C5", 1), ("E5", 2), ("D5", 1), ("C5", 1),
         ("B4", 3), ("C5", 1), ("D5", 2), ("E5", 2), ("C5", 2), ("A4", 2), ("A4", 2)],
        [("A2", 4), ("E2", 4), ("A2", 4), ("A2", 4), ("E2", 4), ("E2", 4), ("A2", 4), ("A2", 2)],
        0.058),
    "StartGame": lambda: melody([("A4", 1), ("C5", 1), ("E5", 1), ("A5", 3)], 0.055,
                                wave="pulse", duty=0.25),
    "Move": lambda: tone("E6", 0.022, wave="pulse", duty=0.125, decay=0.012),
    "Rotate": lambda: tone("A5", 0.05, wave="pulse", duty=0.25, freq_end="E6", decay=0.04),
    "Lock": lambda: mix(noise(0.06, smooth=0.7, decay=0.02),
                        tone(180, 0.08, wave="triangle", freq_end=70, decay=0.04)),
    # More lines, a longer and higher sweep; four at once gets a fanfare.
    "Single": lambda: sweep("C5", "C6", 0.14),
    "Double": lambda: seq(sweep("C5", "C6", 0.1), sweep("E5", "E6", 0.12)),
    "Triple": lambda: seq(sweep("C5", "C6", 0.08), sweep("E5", "E6", 0.08), sweep("G5", "G6", 0.12)),
    "Tetris": lambda: seq(
        sweep("C5", "C7", 0.18),
        mix(melody([("C6", 1), ("E6", 1), ("G6", 1), ("C7", 4)], 0.06, wave="pulse", duty=0.25),
            melody([("C4", 3), ("G4", 4)], 0.06, wave="triangle", legato=0.95))),
    "LevelUp": lambda: seq(
        sweep("G4", "G6", 0.16),
        mix(melody([("G5", 1), ("B5", 1), ("D6", 1), ("G6", 1), ("D6", 1), ("G6", 4)], 0.06,
                   wave="pulse", duty=0.25),
            melody([("G3", 3), ("D4", 2), ("G4", 4)], 0.06, wave="triangle", legato=0.95))),
    # The end show: the Korobeiniki opening turned to a major key, over an
    # octave-hopping bass, then a fanfare run up to a held top note.
    "EndShow": lambda: seq(
        lead_bass(
            [("E5", 2), ("B4", 1), ("C#5", 1), ("D5", 2), ("C#5", 1), ("B4", 1),
             ("A4", 2), ("A4", 1), ("C#5", 1), ("E5", 2), ("D5", 1), ("C#5", 1),
             ("B4", 3), ("C#5", 1), ("D5", 2), ("E5", 2), ("C#5", 2), ("A4", 2), ("A4", 2), ("-", 2)],
            [("A2", 1), ("A3", 1), ("A2", 1), ("A3", 1), ("E2", 1), ("E3", 1), ("E2", 1), ("E3", 1),
             ("A2", 1), ("A3", 1), ("A2", 1), ("A3", 1), ("E2", 1), ("E3", 1), ("E2", 1), ("E3", 1),
             ("D2", 1), ("D3", 1), ("D2", 1), ("D3", 1), ("E2", 1), ("E3", 1), ("E2", 1), ("E3", 1),
             ("A2", 1), ("A3", 1), ("E2", 1), ("E3", 1), ("A2", 2), ("-", 2)],
            0.105),
        mix(melody([("A4", 1), ("C#5", 1), ("E5", 1), ("A5", 1), ("C#6", 1), ("E6", 1), ("A6", 8)], 0.07,
                   wave="pulse", duty=0.25, vol=0.5),
            melody([("A2", 3), ("E3", 3), ("A3", 8)], 0.07, wave="triangle", vol=0.45, legato=0.95))),
    # Halloween's: Korobeiniki in its own minor key, slow and stalking over
    # the octave bass, then a creepy chromatic slide down to a low A.
    "EndShowHalloween": lambda: seq(
        lead_bass(
            [("E5", 2), ("B4", 1), ("C5", 1), ("D5", 2), ("C5", 1), ("B4", 1),
             ("A4", 2), ("A4", 1), ("C5", 1), ("E5", 2), ("D5", 1), ("C5", 1),
             ("B4", 3), ("C5", 1), ("D5", 2), ("E5", 2), ("C5", 2), ("A4", 2), ("A4", 2), ("-", 2)],
            [("A2", 1), ("A3", 1), ("A2", 1), ("A3", 1), ("E2", 1), ("E3", 1), ("E2", 1), ("E3", 1),
             ("A2", 1), ("A3", 1), ("A2", 1), ("A3", 1), ("E2", 1), ("E3", 1), ("E2", 1), ("E3", 1),
             ("D2", 1), ("D3", 1), ("D2", 1), ("D3", 1), ("E2", 1), ("E3", 1), ("E2", 1), ("E3", 1),
             ("A2", 1), ("A3", 1), ("E2", 1), ("E3", 1), ("A2", 2), ("-", 2)],
            0.13),
        lead_bass([("E5", 1), ("D#5", 1), ("D5", 1), ("C#5", 1), ("C5", 1), ("B4", 1), ("Bb4", 1), ("A4", 6)],
                  [("A2", 6), ("A2", 7)], 0.09)),
    "GameOver": lambda: lead_bass(
        [("E5", 2), ("C5", 2), ("A4", 2), ("E4", 2), ("F4", 2), ("E4", 2), ("D#4", 2), ("E4", 6)],
        [("A2", 4), ("A2", 4), ("D2", 4), ("E2", 8)],
        0.085),
}
