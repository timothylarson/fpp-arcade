"""Breakout Versus: bright arcade blips - a paddle ping, a softer wall tick, a
crunchy brick - with a rising two-tone horn for a goal and the jingles built
from the same pitches. Two players in one game, so everything is short."""

from synth import *

PING = "E6"
TICK = "E5"


def blip(freq, dur, **kw):
    return tone(freq, dur, wave="square", attack=0.001, release=0.006, **kw)


SOUNDS = {
    "GameSelected": lambda: melody(
        [("E5", 1), ("G5", 1), ("B5", 1), ("E6", 2), ("-", 1), ("B5", 1), ("E6", 3)], 0.1, legato=0.6),
    # Face-off: two serves, then a high ping.
    "Start": lambda: melody([("E5", 1), ("-", 1), ("E5", 1), ("-", 1), ("E6", 3)], 0.11, legato=0.5),
    "Launch": lambda: blip("B5", 0.05, freq_end="E6"),
    "Paddle": lambda: blip(PING, 0.035),
    "Wall": lambda: blip(TICK, 0.025),
    "Brick": lambda: mix(blip("A5", 0.05, freq_end="A4", decay=0.04), noise(0.04, vol=0.25, decay=0.02)),
    "Laser": lambda: tone("C7", 0.09, wave="saw", freq_end="C6", decay=0.05),
    "PowerUp": lambda: melody([("C6", 1), ("E6", 1), ("G6", 1), ("C7", 2)], 0.045),
    "Goal": lambda: seq(tone("E5", 0.12, wave="square", duty=0.25), tone("B5", 0.22, wave="square", duty=0.25,
                                                                         release=0.08)),
    "NewWall": lambda: seq(*[blip(n, 0.04) for n in ["E5", "G5", "B5", "E6", "G6"]]),
    "GameOver": lambda: melody([("E6", 1), ("B5", 1), ("G5", 1), ("E5", 3)], 0.13, legato=0.85),
    # Halloween's: the rally speeding up, then a creeping minor run, a
    # spooky trill and a slide down to a low E.
    "EndShowHalloween": lambda: mix(
        seq(seq(*[seq(blip(PING if i % 2 else TICK, 0.03), silence(max(0.04, 0.22 * 0.85 ** i))) for i in range(14)]),
            silence(0.1),
            melody([("E5", 1), ("G5", 1), ("B5", 1), ("Bb5", 1), ("A5", 1), ("C6", 1), ("B5", 1), ("C6", 1),
                    ("B5", 1), ("C6", 1), ("B5", 2), ("E5", 6)], 0.12, wave="square", duty=0.25)),
        seq(silence(1.3), melody([("E3", 1), ("B2", 1), ("E3", 1), ("Bb2", 1), ("A2", 1), ("B2", 1), ("E2", 6)],
                                 0.24, wave="triangle", vol=0.45))),
    # The end show for a clear win: a rally that speeds up, then a fanfare
    # climbing to a long top E over a bouncing bass.
    "EndShow": lambda: mix(
        seq(seq(*[seq(blip(PING if i % 2 else TICK, 0.03), silence(max(0.04, 0.22 * 0.85 ** i))) for i in range(14)]),
            silence(0.1),
            melody([("E5", 1), ("G5", 1), ("B5", 1), ("E6", 2), ("D6", 1), ("E6", 1), ("G6", 1), ("B6", 1),
                    ("E7", 6)], 0.12, wave="square", duty=0.25)),
        seq(silence(1.3), melody([("E3", 1), ("B3", 1), ("E3", 1), ("B3", 1), ("E3", 1), ("B3", 1), ("E3", 6)],
                                 0.24, wave="triangle", vol=0.45))),
}
