"""Frogger: springy hops, a wet splash and a crunchy squash, and bouncy
square-wave jingles with a triangle bass, like an early-80s arcade board."""

from synth import *


def _tune(lead, bass, beat):
    """A lead line over a triangle bass line, both [(note, beats), ...]."""
    return mix(melody(lead, beat, wave="square", duty=0.25, vol=0.5),
               melody(bass, beat, wave="triangle", vol=0.6, legato=0.95))


def _hop():
    # A quick up-chirp: the frog leaving the ground.
    return tone("C5", 0.07, wave="square", duty=0.25, freq_end="G6", curve=0.7, release=0.015)


def _splash():
    # A plop down into the water, then the spray hissing away.
    plop = tone("A5", 0.12, wave="triangle", freq_end="C3", vol=0.7)
    spray = noise(0.35, vol=0.5, smooth=0.6, decay=0.12, seed=3)
    return mix(plop, seq(silence(0.04), spray))


def _squash():
    # A hard crunch over a low falling buzz.
    crunch = noise(0.18, vol=0.8, smooth=0.3, decay=0.06, seed=7)
    buzz = tone("E3", 0.3, wave="square", duty=0.5, freq_end="E1", vol=0.5, decay=0.15)
    return mix(crunch, buzz)


def _speed(up):
    a, b = ("C4", "C6") if up else ("C6", "C4")
    return seq(tone(a, 0.06, wave="pulse", duty=0.125, freq_end=b, vol=0.5),
               tone(b, 0.05, wave="pulse", duty=0.125, vol=0.4, decay=0.03))


SOUNDS = {
    "GameSelected": lambda: _tune(
        [("G4", 1), ("C5", 1), ("E5", 1), ("G5", 1), ("-", 0.5), ("E5", 0.5), ("G5", 2)],
        [("C3", 2), ("G2", 2), ("C3", 3)],
        0.12),
    "Start": lambda: _tune(
        [("E5", 1), ("E5", 1), ("G5", 1), ("E5", 1), ("C5", 1), ("D5", 1), ("E5", 2),
         ("D5", 1), ("D5", 1), ("F5", 1), ("D5", 1), ("B4", 1), ("G4", 1), ("C5", 2)],
        [("C3", 2), ("G2", 2), ("C3", 2), ("G2", 2), ("G2", 2), ("D3", 2), ("C3", 2), ("-", 2)],
        0.1),
    "Hop": _hop,
    "Bump": lambda: tone("G2", 0.06, wave="square", duty=0.5, vol=0.5, decay=0.03),
    "Home": lambda: melody([("C6", 1), ("E6", 1), ("G6", 1), ("C7", 2)], 0.055,
                           wave="square", duty=0.25, legato=0.9),
    "Splash": _splash,
    "Squash": _squash,
    "SpeedUp": lambda: _speed(True),
    "SpeedDown": lambda: _speed(False),
    "LevelClear": lambda: _tune(
        [("C5", 1), ("E5", 1), ("G5", 1), ("C6", 1), ("G5", 1), ("C6", 1), ("E6", 1), ("G6", 1), ("C7", 4)],
        [("C3", 2), ("E3", 2), ("G3", 2), ("C4", 2), ("C3", 4)],
        0.09),
    # The end show for a good game: a hop up the arpeggio, then the chorus of
    # Jingle Bells in the game's square-and-triangle style, over an oom-pah
    # bass, ending on a high C.
    "EndShow": lambda: _tune(
        [("C5", 0.5), ("E5", 0.5), ("G5", 0.5), ("C6", 0.5),
         ("E5", 1), ("E5", 1), ("E5", 2), ("E5", 1), ("E5", 1), ("E5", 2),
         ("E5", 1), ("G5", 1), ("C5", 1.5), ("D5", 0.5), ("E5", 4),
         ("F5", 1), ("F5", 1), ("F5", 1.5), ("F5", 0.5), ("F5", 1), ("E5", 1), ("E5", 1), ("E5", 0.5), ("E5", 0.5),
         ("G5", 1), ("G5", 1), ("F5", 1), ("D5", 1), ("C5", 1), ("G5", 1), ("C6", 2)],
        [("C3", 2),
         ("C3", 1), ("G2", 1), ("C3", 1), ("G2", 1), ("C3", 1), ("G2", 1), ("C3", 1), ("G2", 1),
         ("C3", 1), ("G2", 1), ("G2", 1), ("D3", 1), ("C3", 1), ("G2", 1), ("C3", 1), ("G2", 1),
         ("F2", 1), ("C3", 1), ("F2", 1), ("C3", 1), ("C3", 1), ("G2", 1), ("C3", 1), ("G2", 1),
         ("G2", 1), ("D3", 1), ("G2", 1), ("B2", 1), ("C3", 2), ("C2", 2)],
        0.13),
    # Halloween's: a hop up the minor arpeggio, then the subject of Bach's
    # "Little" Fugue in G minor over the oom-pah bass, and a run home to G.
    "EndShowHalloween": lambda: _tune(
        [("G4", 0.5), ("Bb4", 0.5), ("D5", 0.5), ("G5", 0.5),
         ("G5", 2), ("D6", 2), ("Bb5", 3), ("A5", 1), ("G5", 1), ("Bb5", 1), ("A5", 1), ("G5", 1),
         ("F#5", 1), ("A5", 1), ("D5", 2),
         ("G5", 1), ("F#5", 1), ("G5", 1), ("A5", 1), ("Bb5", 1), ("A5", 1), ("G5", 1), ("D5", 1), ("G5", 4)],
        [("G2", 2),
         ("G2", 1), ("D3", 1), ("G2", 1), ("D3", 1), ("G2", 1), ("D3", 1), ("G2", 1), ("D3", 1),
         ("C3", 1), ("G2", 1), ("D3", 1), ("A2", 1), ("D3", 1), ("A2", 1), ("D3", 1), ("A2", 1),
         ("G2", 1), ("D3", 1), ("Eb3", 1), ("Bb2", 1), ("D3", 1), ("A2", 1), ("G2", 2), ("G2", 2)],
        0.13),
    # Everyday's: a hop up the arpeggio and a jaunty tune home to a high C.
    "EndShowEveryday": lambda: _tune(
        [("C5", 0.5), ("E5", 0.5), ("G5", 0.5), ("C6", 0.5),
         ("E6", 1), ("D6", 1), ("C6", 1), ("G5", 1), ("A5", 1), ("G5", 1), ("E5", 2),
         ("F5", 1), ("A5", 1), ("G5", 1), ("E5", 1), ("D5", 1), ("E5", 1), ("C5", 2),
         ("G5", 1), ("A5", 1), ("B5", 1), ("C6", 4)],
        [("C3", 2),
         ("C3", 1), ("G2", 1), ("C3", 1), ("G2", 1), ("F2", 1), ("C3", 1), ("C3", 1), ("G2", 1),
         ("F2", 1), ("C3", 1), ("C3", 1), ("G2", 1), ("G2", 1), ("D3", 1), ("C3", 1), ("G2", 1),
         ("G2", 1), ("D3", 1), ("G2", 1), ("B2", 1), ("C3", 2), ("C2", 2)],
        0.14),
    "GameOver": lambda: _tune(
        [("G5", 1), ("F5", 1), ("E5", 1), ("D5", 1), ("C5", 2), ("G4", 2), ("C4", 4)],
        [("C3", 2), ("B2", 2), ("A2", 2), ("G2", 2), ("C2", 4)],
        0.14),
}
