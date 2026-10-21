"""Maze Chase: the two-part waka, warbling sweeps for the fright and the
ghost gobble, a spiralling-down death, and pulse-wave jingles over a
triangle bass, in the style of the early-80s maze games."""

from synth import *


def _tune(lead, bass, beat):
    """A lead line over a triangle bass line, both [(note, beats), ...]."""
    return mix(melody(lead, beat, wave="pulse", duty=0.25, vol=0.5),
               melody(bass, beat, wave="triangle", vol=0.6, legato=0.95))


def _waka(down):
    # Each half is a fast pitch sweep on a triangle wave; alternating a falling
    # one with a rising one is what makes the "wa-ka".
    a, b = ("B4", "D4") if down else ("D4", "B4")
    return tone(a, 0.07, wave="triangle", freq_end=b, vol=0.7, attack=0.002, release=0.01)


def _scared():
    # Quick up-sweeps that slow down and sink: the ghosts turning and running.
    parts = []
    for i in range(10):
        lo = note("G4") * 0.97 ** i
        parts.append(tone(lo, 0.11, wave="square", duty=0.5, freq_end=lo * 2.2, vol=0.4, release=0.01))
    return seq(*parts)


def _eat_ghost():
    # A swooping gulp up the scale.
    return seq(tone("C4", 0.06, wave="square", duty=0.25, vol=0.5),
               tone("G3", 0.24, wave="square", duty=0.25, freq_end="C7", curve=1.6, vol=0.5))


def _death():
    # Spirals down: falling sweeps, each starting lower, then two blips.
    parts = []
    for i in range(7):
        hi = note("E6") * 0.86 ** i
        parts.append(tone(hi, 0.14, wave="square", duty=0.5, freq_end=hi * 0.55, vol=0.45))
    parts += [silence(0.08), tone("C4", 0.06, wave="square", vol=0.5), silence(0.05),
              tone("C4", 0.08, wave="square", vol=0.5, decay=0.05)]
    return seq(*parts)


SOUNDS = {
    "GameSelected": lambda: _tune(
        [("E5", 1), ("G5", 1), ("B5", 1), ("E6", 1), ("-", 1), ("D6", 1), ("E6", 2)],
        [("E3", 2), ("B2", 2), ("E3", 4)],
        0.11),
    # Two seconds, matching READY.
    "Start": lambda: _tune(
        [("A4", 1), ("A5", 1), ("E5", 1), ("C#5", 1), ("A5", 0.5), ("E5", 1.5), ("C#5", 2),
         ("Bb4", 1), ("Bb5", 1), ("F5", 1), ("D5", 1), ("Bb5", 0.5), ("F5", 1.5), ("D5", 2),
         ("A4", 1), ("C#5", 1), ("E5", 1), ("A5", 1), ("A5", 2)],
        [("A2", 3), ("A3", 1), ("A2", 3), ("A3", 1), ("Bb2", 3), ("Bb3", 1), ("Bb2", 3), ("Bb3", 1),
         ("A2", 2), ("E3", 2), ("A2", 2)],
        0.075),
    "WakaA": lambda: _waka(True),
    "WakaB": lambda: _waka(False),
    "PowerPellet": lambda: tone("C4", 0.14, wave="square", duty=0.5, freq_end="C2", vol=0.6, decay=0.08),
    "Scared": _scared,
    "EatGhost": _eat_ghost,
    "Death": _death,
    "LevelClear": lambda: _tune(
        [("A5", 1), ("E5", 1), ("A5", 1), ("C#6", 1), ("E6", 1), ("C#6", 1), ("E6", 1), ("A6", 3), ("-", 1),
         ("A6", 2)],
        [("A2", 2), ("E3", 2), ("A2", 2), ("E3", 2), ("A2", 4), ("A3", 2)],
        0.1),
    "GameOver": lambda: _tune(
        [("E5", 1), ("D5", 1), ("C5", 1), ("B4", 1), ("A4", 2), ("E4", 2), ("A3", 4)],
        [("A2", 2), ("G2", 2), ("F2", 2), ("E2", 2), ("A1", 4)],
        0.14),
}
