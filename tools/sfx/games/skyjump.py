"""Sky Jump: an elf with a rocket pack climbing the night sky. Whooshes and
rising zips for the boosts, a coin scale that climbs as the coins chain,
and bright bell-like square waves for the jingles."""

from synth import *

# The coin scale: C major from C6 up, one step per coin in a quick run.
_COIN_NOTES = ["C6", "D6", "E6", "F6", "G6", "A6", "B6", "C7"]


def coin(name):
    """A short plink, kept tiny - the game plays one for every coin."""
    return seq(tone(name, 0.025, duty=0.25, vol=0.4), tone(note(name) * 1.5, 0.06, duty=0.25, vol=0.35, decay=0.03))


def whoosh(dur, low, high, seed=1):
    """Rocket air rising in pitch: smoothed noise over a sliding square."""
    return mix(noise(dur, vol=0.45, smooth=0.75, decay=dur / 1.5, seed=seed),
               tone(low, dur, wave="square", duty=0.25, vol=0.25, freq_end=high, curve=0.6, decay=dur / 2))


SOUNDS = {
    "GameSelected": lambda: mix(
        melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 1), ("-", 1), ("G5", 1), ("C6", 1), ("E6", 1), ("G6", 3)], 0.09,
               duty=0.25),
        melody([("C3", 2), ("G3", 2), ("-", 1), ("C3", 2), ("G3", 2), ("C4", 3)], 0.09, wave="triangle", vol=0.45)),
    # Blast-off: a rumble that takes off.
    "Launch": lambda: seq(noise(0.12, vol=0.35, smooth=0.9, attack=0.05, seed=2), whoosh(0.55, "C3", "C6", seed=3)),
    "Blast": lambda: mix(whoosh(0.3, "G3", "G6", seed=4),
                         seq(silence(0.12), tone("G6", 0.12, duty=0.125, vol=0.3, decay=0.06))),
    **{f"Coin{i + 1}": (lambda n=n: coin(n)) for i, n in enumerate(_COIN_NOTES)},
    # The star: a sparkly run up and a shimmer.
    "Star": lambda: seq(
        melody([("C6", 1), ("E6", 1), ("G6", 1), ("C7", 1), ("E7", 1), ("G7", 1)], 0.035, duty=0.25, vol=0.4),
        tone("C7", 0.3, duty=0.125, vol=0.35, vibrato=0.5, vib_rate=14, decay=0.15)),
    "StarOut": lambda: melody([("G6", 1), ("E6", 1), ("C6", 2)], 0.05, duty=0.25, vol=0.35),
    # Through an enemy with a star: a pop and a bright ping.
    "Smash": lambda: seq(
        mix(noise(0.1, vol=0.55, smooth=0.3, decay=0.04, seed=5), tone("C5", 0.1, wave="square", freq_end="C3", decay=0.04)),
        tone("E7", 0.08, duty=0.125, vol=0.3, decay=0.04)),
    # Knocked by an enemy: a bonk.
    "Hit": lambda: mix(tone("A3", 0.22, wave="square", duty=0.5, vol=0.45, freq_end="D3", decay=0.08),
                       noise(0.08, vol=0.35, smooth=0.6, decay=0.03, seed=6)),
    # Off the bottom: a long falling whistle.
    "Fall": lambda: seq(tone("C6", 0.7, wave="triangle", vol=0.5, freq_end="C3", curve=1.4, vibrato=0.3, vib_rate=8),
                        noise(0.15, vol=0.3, smooth=0.85, decay=0.06, seed=7)),
    # A spare life catches the elf: up it goes again, with a chime.
    "Rescue": lambda: mix(whoosh(0.5, "C3", "C6", seed=8),
                          seq(silence(0.2), melody([("E6", 1), ("G6", 1), ("C7", 2)], 0.06, duty=0.25, vol=0.35))),
    # Every 250 rows climbed: a little cheer.
    "Higher": lambda: melody([("G5", 1), ("C6", 1), ("E6", 1), ("G6", 2)], 0.06, duty=0.25, vol=0.4),
    # A second player joins: two notes up, then the race's countdown beeps.
    "Join": lambda: melody([("C5", 1), ("G5", 2)], 0.07, duty=0.25),
    "Count": lambda: tone("A5", 0.12, duty=0.25, vol=0.4, decay=0.08),
    "GameOver": lambda: mix(
        melody([("G5", 1), ("E5", 1), ("C5", 1), ("-", 1), ("D5", 1), ("B4", 1), ("G4", 1), ("C5", 3)], 0.12, duty=0.25),
        melody([("C3", 4), ("G2", 3), ("C3", 3)], 0.12, wave="triangle", vol=0.45)),
    # Halloween's: blast-off, then a spooky broomstick waltz in E minor,
    # chimed high over a swaying bass, ending on a long E.
    "EndShowHalloween": lambda: seq(
        whoosh(0.5, "C3", "C6", seed=9),
        mix(melody([("E6", 1), ("B5", 1), ("G5", 1), ("E5", 3), ("F#5", 1), ("G5", 1), ("A5", 1), ("B5", 3),
                    ("C6", 1), ("B5", 1), ("A5", 1), ("G5", 1), ("F#5", 1), ("D#5", 1), ("E5", 1), ("B5", 1), ("E6", 4)],
                   0.16, duty=0.125, vol=0.45),
            melody([("E3", 3), ("B2", 3), ("E3", 3), ("D#3", 3), ("C3", 3), ("B2", 3), ("E3", 4)], 0.16,
                   wave="triangle", vol=0.45, legato=0.7))),
    # Everyday's: blast-off, then a bright fanfare climbing to a high C.
    "EndShowEveryday": lambda: seq(
        whoosh(0.5, "C3", "C6", seed=9),
        mix(melody([("C6", 1), ("E6", 1), ("G6", 2), ("E6", 1), ("G6", 1), ("C7", 2), ("A6", 1), ("G6", 1), ("E6", 2),
                    ("F6", 1), ("A6", 1), ("G6", 1), ("E6", 1), ("D6", 1), ("G6", 1), ("C7", 4)], 0.15, duty=0.125, vol=0.45),
            melody([("C3", 4), ("C3", 4), ("F2", 4), ("F2", 2), ("G2", 2), ("C3", 4)], 0.15,
                   wave="triangle", vol=0.45, legato=0.7))),
    # The end show: blast-off, then Jolly Old Saint Nicholas, chimed high, over a bass.
    "EndShow": lambda: seq(
        whoosh(0.5, "C3", "C6", seed=9),
        mix(melody([("E6", 1), ("E6", 1), ("E6", 1), ("E6", 1), ("D6", 1), ("D6", 1), ("D6", 2),
                    ("C6", 1), ("C6", 1), ("C6", 1), ("C6", 1), ("E6", 4),
                    ("A5", 1), ("A5", 1), ("A5", 1), ("A5", 1), ("G5", 1), ("G5", 1), ("G5", 2),
                    ("D6", 1), ("E6", 1), ("D6", 1), ("C6", 1), ("G6", 4)], 0.16, duty=0.125, vol=0.45),
            melody([("C3", 4), ("G2", 4), ("C3", 4), ("C3", 4), ("F2", 4), ("C3", 4), ("G2", 4), ("G2", 4)], 0.16,
                   wave="triangle", vol=0.45, legato=0.7))),
}
