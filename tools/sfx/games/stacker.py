"""Stacker: drop a sliding row onto the tower. Each drop is a plucked note
one step higher up a pentatonic scale as the tower nears the top."""

from synth import *

# The opening of Bach's Toccata and Fugue in D minor: the mordent, a rest,
# the run down and the held D (23 beats).
TOCCATA = [("A5", 1), ("G5", 1), ("A5", 6), ("-", 2), ("G5", 1), ("F5", 1), ("E5", 1), ("D5", 1), ("C#5", 3), ("D5", 6)]

# Two octaves and a bit of C major pentatonic, for Drop1..Drop16.
SCALE = ["C4", "D4", "E4", "G4", "A4", "C5", "D5", "E5", "G5", "A5", "C6", "D6", "E6", "G6", "A6", "C7"]


def drop(n):
    return mix(noise(0.02, vol=0.3, smooth=0.5),
               tone(n, 0.12, duty=0.25, vol=0.5, decay=0.05),
               tone(n, 0.12, wave="triangle", vol=0.4, decay=0.06) * 0.5)


SOUNDS = {
    "GameSelected": lambda: mix(
        melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 1), ("E6", 1), ("G6", 3)], 0.09, duty=0.25),
        melody([("C3", 3), ("G3", 3), ("C4", 2)], 0.09, wave="triangle", vol=0.4)),
    "LevelStart": lambda: melody([("G4", 1), ("C5", 1), ("G5", 2)], 0.09, duty=0.25),
    "Perfect": lambda: seq(silence(0.04), melody([("E6", 1), ("G6", 1), ("C7", 2)], 0.045, duty=0.125, vol=0.4)),
    "Chop": lambda: mix(noise(0.08, vol=0.35, smooth=0.8, decay=0.04),
                        tone("G4", 0.2, duty=0.125, freq_end="G3", vol=0.3, decay=0.08)),
    "Miss": lambda: seq(
        mix(noise(0.06, vol=0.4, smooth=0.7), tone("E4", 0.06)),
        tone("E4", 0.55, duty=0.25, freq_end="E2", curve=0.8, vibrato=0.6, vib_rate=10, release=0.08)),
    "LevelClear": lambda: mix(
        melody([("C5", 1), ("C5", 1), ("C5", 1), ("E5", 2), ("C5", 1), ("G5", 1), ("-", 1),
                ("A5", 1), ("G5", 1), ("C6", 4)], 0.1, duty=0.25),
        melody([("C3", 3), ("E3", 3), ("G3", 3), ("C3", 5)], 0.1, wave="triangle", vol=0.4, legato=0.7)),
    "GameOver": lambda: mix(
        melody([("C5", 1), ("G4", 1), ("E4", 1), ("A4", 1.5), ("B4", 1.5), ("A4", 1), ("G#4", 1), ("Bb4", 1), ("G#4", 1), ("G4", 3)],
               0.11, duty=0.25),
        melody([("C3", 3), ("F3", 4), ("E3", 3), ("C3", 3)], 0.11, wave="triangle", vol=0.4)),
    # The end show, as the tower becomes a Christmas tree: a quick run up all
    # the drop notes, then O Christmas Tree.
    "EndShow": lambda: seq(
        seq(*[tone(n, 0.045, duty=0.25, vol=0.5, decay=0.03, release=0.01) for n in SCALE]),
        silence(0.08),
        mix(melody([("D5", 1), ("G5", 1.5), ("G5", 0.5), ("G5", 3), ("A5", 1), ("B5", 1.5), ("B5", 0.5), ("B5", 3),
                    ("B5", 1), ("A5", 1), ("B5", 1), ("C6", 2), ("F#5", 1), ("A5", 2), ("G5", 4)], 0.17, duty=0.25),
            seq(silence(0.17), melody([("G3", 6), ("D3", 5), ("G3", 3), ("C3", 2), ("D3", 3), ("G2", 4)], 0.17,
                                      wave="triangle", vol=0.4, legato=0.7)))),
    # Halloween's, as the tower becomes a witch's hat: the run up the drop
    # notes, then the opening of Bach's Toccata and Fugue in D minor - the
    # mordent and the falling run, twice an octave apart - over a low D, and
    # the rolled diminished chord.
    "EndShowHalloween": lambda: seq(
        seq(*[tone(n, 0.045, duty=0.25, vol=0.5, decay=0.03, release=0.01) for n in SCALE]),
        silence(0.08),
        mix(seq(melody(TOCCATA, 0.11, duty=0.25),
                melody([(n.replace("5", "4").replace("6", "5"), d) for n, d in TOCCATA], 0.11, duty=0.25)),
            tone("D2", 0.11 * 46, wave="triangle", vol=0.4, release=0.3)),
        mix(seq(*[tone(n, 0.07, duty=0.25, vol=0.45, release=0.01) for n in ("C#4", "E4", "G4", "Bb4", "C#5", "E5")]),
            tone("C#3", 0.42, wave="triangle", vol=0.4)),
        mix(tone("D5", 0.7, duty=0.25, vol=0.45, release=0.3), tone("D3", 0.7, wave="triangle", vol=0.45, release=0.3))),
    # Everyday's: the run up the drop notes, then a bright fanfare.
    "EndShowEveryday": lambda: seq(
        seq(*[tone(n, 0.045, duty=0.25, vol=0.5, decay=0.03, release=0.01) for n in SCALE]),
        silence(0.08),
        mix(melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 2), ("A5", 1), ("C6", 1), ("E6", 3),
                    ("D6", 1), ("C6", 1), ("A5", 1), ("G5", 1), ("A5", 1), ("C6", 4)], 0.17, duty=0.25),
            melody([("C3", 2), ("G3", 2), ("C3", 2), ("E3", 2), ("F3", 2), ("G3", 2), ("C3", 4)], 0.17,
                   wave="triangle", vol=0.4, legato=0.7))),
}
SOUNDS.update({f"Drop{i + 1}": (lambda n=n: drop(n)) for i, n in enumerate(SCALE)})
