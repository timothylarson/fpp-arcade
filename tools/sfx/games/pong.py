"""Pong: bare square-wave blips in the spirit of the 1972 cabinet - a high
blip off a paddle, an octave lower off a wall, a long tone for a point - with
the jingles built from the same blip pitches."""

from synth import *

PADDLE = 490   # roughly the original's pitches: paddle, then wall an octave down
WALL = 245


def blip(freq, dur):
    return tone(freq, dur, wave="square", attack=0.001, release=0.005)


SOUNDS = {
    "GameSelected": lambda: melody(
        [(PADDLE, 1), (WALL, 1), (PADDLE, 1), (WALL, 1), ("-", 1),
         (PADDLE, 1), (PADDLE * 1.5, 1), (PADDLE * 2, 3)], 0.11, legato=0.4),
    # A three-count serve.
    "StartGame": lambda: melody([(WALL, 1), (WALL, 1), (PADDLE * 2, 2)], 0.14, legato=0.35),
    "Paddle": lambda: blip(PADDLE, 0.035),
    "Wall": lambda: blip(WALL, 0.03),
    "Point": lambda: tone(PADDLE, 0.26, wave="square", freq_end=PADDLE * 0.9, release=0.04),
    "GameOver": lambda: mix(
        melody([(PADDLE, 1), (PADDLE * 1.25, 1), (PADDLE * 1.5, 1), (PADDLE * 2, 2),
                (PADDLE * 1.5, 1), (PADDLE * 2, 4)], 0.1, wave="square", vol=0.45),
        melody([(WALL / 2, 3), (WALL * 0.75, 2), (WALL, 5)], 0.1, wave="triangle", vol=0.5,
               legato=0.95)),
}
