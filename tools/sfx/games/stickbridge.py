"""Stick Bridge: grow a candy cane, drop it across the gap, walk over. The
stick ticks up a semitone for every LED it grows, so its pitch is its length;
landings are wooden knocks, and the jingles are sleigh-bell bright."""

from synth import *

# Grow1..Grow20: one short tick per LED of stick, a semitone higher each,
# from C5.
GROW = ["C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5",
        "C6", "C#6", "D6", "D#6", "E6", "F6", "F#6", "G6"]


def grow(n):
    return mix(tone(n, 0.035, duty=0.125, vol=0.4, decay=0.015, release=0.01),
               tone(n, 0.035, wave="triangle", vol=0.3, decay=0.02, release=0.01))


def knock(n, vol=1.0):
    """A wooden tok: a click of noise over a quick falling note."""
    return mix(noise(0.03, vol=0.35 * vol, smooth=0.6, decay=0.01),
               tone(n, 0.09, wave="triangle", freq_end=note(n) * 0.7, vol=0.6 * vol, decay=0.03))


def bell(n, dur=0.12, vol=0.4):
    """A sleigh-bell-ish ping: a bright pulse with a fifth over it."""
    return mix(tone(n, dur, duty=0.125, vol=vol, decay=dur / 3),
               tone(note(n) * 1.5, dur, wave="triangle", vol=vol * 0.5, decay=dur / 4))


SOUNDS = {
    "GameSelected": lambda: mix(
        melody([("G5", 1), ("C6", 1), ("E6", 1), ("G6", 1), ("-", 1), ("E6", 1), ("G6", 3)], 0.09, duty=0.125),
        melody([("C3", 3), ("G3", 2), ("C4", 4)], 0.09, wave="triangle", vol=0.4)),
    "Start": lambda: melody([("C5", 1), ("G5", 1), ("C6", 2)], 0.05, duty=0.125),
    # The other control joined: the start, answered a fifth higher.
    "Join": lambda: seq(melody([("C5", 1), ("G5", 1), ("C6", 2)], 0.05, duty=0.125),
                        melody([("G5", 1), ("D6", 1), ("G6", 2)], 0.05, duty=0.25)),
    # Let go: the stick tipping over, a swish that drops.
    "Drop": lambda: mix(noise(0.22, vol=0.3, smooth=0.85, attack=0.05, decay=0.12),
                        tone("E5", 0.22, wave="triangle", freq_end="E4", curve=1.6, vol=0.25)),
    # Its end hits the next box.
    "Land": lambda: knock("G4"),
    # A footstep (every other LED).
    "Step": lambda: noise(0.03, vol=0.5, smooth=0.7, decay=0.008),
    # Over: a little two-note rise.
    "Cross": lambda: seq(bell("E6", 0.05), bell("A6", 0.12)),
    # Dead on the bow: a sparkly run up.
    "Perfect": lambda: seq(silence(0.03), mix(
        melody([("C6", 1), ("E6", 1), ("G6", 1), ("C7", 3)], 0.045, duty=0.125, vol=0.4),
        seq(silence(0.09), noise(0.18, vol=0.12, decay=0.06)))),
    # Off the end: a long slide down, and a soft thump far below.
    "Fall": lambda: seq(
        tone("C6", 0.6, duty=0.25, freq_end="C3", curve=0.8, vol=0.4, release=0.05),
        noise(0.12, vol=0.5, smooth=0.9, decay=0.04)),
    "GameOver": lambda: mix(
        melody([("G5", 1), ("E5", 1), ("C5", 1), ("-", 1), ("D5", 1), ("B4", 1), ("G4", 1), ("C5", 3)], 0.12, duty=0.125),
        melody([("C3", 4), ("G2", 3), ("C3", 3)], 0.12, wave="triangle", vol=0.45)),
    # The end show: the stick growing all the way up, a knock as it lands,
    # then the chorus of Jingle Bells with a bell on every beat.
    "EndShow": lambda: seq(
        seq(*[grow(n) for n in GROW[::2]]),
        knock("G4"),
        silence(0.06),
        mix(melody([("E5", 1), ("E5", 1), ("E5", 2), ("E5", 1), ("E5", 1), ("E5", 2),
                    ("E5", 1), ("G5", 1), ("C5", 1.5), ("D5", 0.5), ("E5", 4),
                    ("F5", 1), ("F5", 1), ("F5", 1.5), ("F5", 0.5), ("F5", 1), ("E5", 1), ("E5", 1), ("E5", 0.5), ("E5", 0.5),
                    ("G5", 1), ("G5", 1), ("F5", 1), ("D5", 1), ("C5", 4)], 0.15, duty=0.25),
            melody([("C3", 2), ("G3", 2), ("C3", 2), ("G3", 2), ("C3", 2), ("G3", 2), ("C3", 2), ("G3", 2),
                    ("F3", 2), ("C4", 2), ("C3", 2), ("G3", 2), ("G3", 2), ("G2", 2), ("C3", 4)], 0.15,
                   wave="triangle", vol=0.4, legato=0.7),
            seq(*[seq(noise(0.04, vol=0.08, decay=0.015), silence(0.26)) for _ in range(16)]))),
}
SOUNDS.update({f"Grow{i + 1}": (lambda n=n: grow(n)) for i, n in enumerate(GROW)})
