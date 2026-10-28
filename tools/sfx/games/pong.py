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
    # The end show for a clear win: a rally of paddle and wall blips getting
    # faster and faster, then the game-over fanfare climbing higher, over a
    # walking bass, to a long top note.
    "EndShow": lambda: seq(
        seq(*[seq(blip(PADDLE if i % 2 else WALL, 0.035), silence(max(0.04, 0.3 * 0.82 ** i)))
              for i in range(16)]),
        silence(0.12),
        mix(
            melody([(PADDLE, 1), (PADDLE * 1.25, 1), (PADDLE * 1.5, 1), (PADDLE * 2, 2),
                    (PADDLE * 1.5, 1), (PADDLE * 2, 2), ("-", 1),
                    (PADDLE * 1.25, 1), (PADDLE * 1.5, 1), (PADDLE * 2, 1), (PADDLE * 2.5, 2),
                    (PADDLE * 2, 1), (PADDLE * 3, 8)], 0.12, wave="square", vol=0.45),
            melody([(WALL / 2, 4), (WALL * 0.75, 4), (WALL * 0.625, 4), (WALL, 12)], 0.12,
                   wave="triangle", vol=0.5, legato=0.95))),
    # Halloween's: the same rally speeding up, then Chopin's funeral march
    # (Piano Sonata No. 2) in blips over a rocking bass - played straight,
    # with a wink - landing on a low Bb.
    "EndShowHalloween": lambda: seq(
        seq(*[seq(blip(PADDLE if i % 2 else WALL, 0.035), silence(max(0.04, 0.3 * 0.82 ** i)))
              for i in range(16)]),
        silence(0.12),
        mix(melody([("Bb4", 1.5), ("Bb4", 1.125), ("Bb4", 0.375), ("Bb4", 1.5), ("Db5", 1.125), ("C5", 0.375),
                    ("C5", 1.125), ("Bb4", 0.375), ("Bb4", 1.125), ("A4", 0.375), ("Bb4", 3)], 0.22,
                   wave="square", vol=0.45),
            melody([("Bb2", 1.5), ("F2", 1.5)] * 4, 0.22, wave="triangle", vol=0.5, legato=0.6)),
        mix(blip("Bb5", 0.06), tone("Bb2", 0.4, wave="triangle", vol=0.5, release=0.2))),
    "GameOver": lambda: mix(
        melody([(PADDLE, 1), (PADDLE * 1.25, 1), (PADDLE * 1.5, 1), (PADDLE * 2, 2),
                (PADDLE * 1.5, 1), (PADDLE * 2, 4)], 0.1, wave="square", vol=0.45),
        melody([(WALL / 2, 3), (WALL * 0.75, 2), (WALL, 5)], 0.1, wave="triangle", vol=0.5,
               legato=0.95)),
}
