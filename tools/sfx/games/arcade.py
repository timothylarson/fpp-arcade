"""Sounds every game shares, in sounds/arcade/: the pause menu opening and
closing. NES style: a plain square wave flicking quickly between two notes a
fourth apart, the last one held. Like most NES games, pausing and unpausing
sound the same."""

from synth import *


def pause_jingle():
    blip = lambda note, dur: tone(note, dur, wave="square", duty=0.5, vol=0.5, attack=0.001, release=0.008)
    gap = silence(0.012)
    return seq(blip("E6", 0.05), gap, blip("B5", 0.05), gap, blip("E6", 0.05), gap, blip("B5", 0.11))


SOUNDS = {
    "Pause": pause_jingle,
    "Unpause": pause_jingle,
}
