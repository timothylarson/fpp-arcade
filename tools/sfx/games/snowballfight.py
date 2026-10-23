"""Snowball Fight: soft whooshes and crunchy snow thuds, sleigh-bell jingles,
and two public-domain carols - Deck the Halls when the game is picked,
Jingle Bells for a win. The sounds heard many times a game (the aim tick,
the charge) are kept short and quiet."""

from synth import *


def bell(freq, dur=0.18, vol=0.4):
    # A sleigh bell: a bright metallic ping over a shake of noise.
    return mix(tone(freq, dur, wave="square", duty=0.25, vol=vol, decay=dur / 4),
               tone(note(freq) * 2.76, dur * 0.6, wave="sine", vol=vol * 0.4, decay=dur / 6),
               noise(dur * 0.4, vol=vol * 0.35, decay=0.02, seed=3))


def shake(n, gap, vol=0.3):
    # Sleigh bells jingling: quick noise shakes.
    return seq(*[seq(noise(0.04, vol=vol, decay=0.012, seed=5 + i), silence(gap)) for i in range(n)])


def thud(vol=0.6, low="C3"):
    # Snow: a dull crunch - smoothed noise with a dropping low note under it.
    return mix(noise(0.16, vol=vol, smooth=0.85, decay=0.05, seed=11),
               noise(0.06, vol=vol * 0.35, smooth=0.3, decay=0.02, seed=12),
               tone(low, 0.14, wave="triangle", freq_end=note(low) / 2, vol=vol * 0.8, decay=0.05))


SOUNDS = {
    # Deck the Halls' first line, on bells over a bass, with a jingle.
    "GameSelected": lambda: mix(
        melody([("G5", 1.5), ("F5", 0.5), ("E5", 1), ("D5", 1), ("C5", 1), ("D5", 1), ("E5", 1), ("C5", 1),
                ("D5", 0.5), ("E5", 0.5), ("F5", 0.5), ("D5", 0.5), ("E5", 1.5), ("D5", 0.5), ("C5", 1), ("B4", 1),
                ("C5", 2)], 0.15, duty=0.25, vol=0.45, legato=0.8),
        melody([("C3", 4), ("G2", 4), ("F2", 2), ("G2", 2), ("C3", 4)], 0.15, wave="triangle", vol=0.45),
        seq(silence(0.15 * 14), shake(6, 0.05, 0.25))),
    # The elves drop in: a quick rising run and a jingle.
    "Start": lambda: mix(melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 2)], 0.07, duty=0.25),
                         seq(silence(0.28), shake(3, 0.04, 0.25))),
    # Your turn: two bells.
    "Turn": lambda: seq(bell("E6", 0.12), bell("A6", 0.22)),
    # A soft tick as the aim turns; very short, played a lot.
    "Aim": lambda: tone("A5", 0.018, wave="triangle", vol=0.3, release=0.008),
    # Winding up: a note rising the whole 1.6 s to full power, a little wobble
    # on it so it sounds like effort.
    "Charge": lambda: tone("C4", 1.6, wave="pulse", duty=0.25, vol=0.3, freq_end="C6", curve=0.9,
                           vibrato=0.25, vib_rate=9.0, release=0.05),
    # The snowball leaves the hand: a whoosh.
    "Throw": lambda: mix(noise(0.22, vol=0.4, smooth=0.6, attack=0.03, release=0.1, seed=21),
                         tone("A5", 0.2, wave="sine", freq_end="E4", vol=0.3, decay=0.08)),
    # It hits the snow and blows a crater out of it.
    "Splat": lambda: seq(thud(0.7, "D3"), noise(0.18, vol=0.15, smooth=0.5, decay=0.06, seed=31)),
    # It gets the other elf: a crunch, a thwack and a squeak.
    "Hit": lambda: mix(thud(0.6, "E3"),
                       noise(0.05, vol=0.5, decay=0.015, seed=41),
                       seq(silence(0.07), tone("C6", 0.16, wave="square", duty=0.25, freq_end="F5", vol=0.35,
                                               decay=0.08))),
    # Off the side of the wall: a whistle falling away.
    "Miss": lambda: tone("E6", 0.45, wave="sine", freq_end="A4", curve=1.4, vol=0.4, release=0.12),
    # A strong wind for this turn: a gust swelling and dying away.
    "Wind": lambda: mix(noise(0.8, vol=0.5, smooth=0.93, attack=0.3, release=0.4, seed=51),
                        tone("E5", 0.8, wave="sine", freq_end="B5", vol=0.06, attack=0.3, release=0.4, vibrato=0.5,
                             vib_rate=3.0)),
    # The last five seconds of a turn.
    "Tick": lambda: tone("E6", 0.03, wave="square", duty=0.125, vol=0.3, decay=0.01),
    # Out of time: the turn passes.
    "TimeUp": lambda: seq(tone("E4", 0.12, wave="square", duty=0.25, vol=0.4),
                          tone("C4", 0.25, wave="square", duty=0.25, vol=0.4, release=0.08)),
    # A second player takes the green elf.
    "Join": lambda: seq(bell("C6", 0.1), bell("E6", 0.1), bell("G6", 0.1), bell("C7", 0.25)),
    # An elf drops into a crater (or lands at the start).
    "Land": lambda: mix(noise(0.08, vol=0.5, smooth=0.9, decay=0.03, seed=61),
                        tone("G2", 0.08, wave="triangle", vol=0.5, decay=0.03)),
    # Knocked out: a wobbling slide down.
    "KO": lambda: seq(tone("G5", 0.5, wave="square", duty=0.25, freq_end="C4", curve=0.8, vol=0.35,
                           vibrato=0.8, vib_rate=10.0, release=0.1),
                      thud(0.5, "C3")),
    # The giant snowball cheat.
    "Giant": lambda: mix(tone("C3", 0.6, wave="saw", freq_end="C5", vol=0.3, release=0.1),
                         melody([("C5", 1), ("G5", 1), ("C6", 1), ("G6", 2)], 0.1, duty=0.25, vol=0.35)),
    # A person wins: "Jingle bells, jingle bells, jingle all the way!"
    "Win": lambda: mix(
        melody([("E5", 1), ("E5", 1), ("E5", 2), ("E5", 1), ("E5", 1), ("E5", 2), ("E5", 1), ("G5", 1), ("C5", 1.5),
                ("D5", 0.5), ("E5", 4)], 0.12, duty=0.25, vol=0.45),
        melody([("C3", 2), ("G2", 2), ("C3", 2), ("G2", 2), ("C3", 2), ("G2", 2), ("C3", 4)], 0.12,
               wave="triangle", vol=0.4),
        shake(14, 0.08, 0.2)),
    # The computer wins (or everyone's gone): a sorry little fall.
    "GameOver": lambda: mix(
        melody([("G5", 1), ("E5", 1), ("C5", 1), ("-", 1), ("D5", 1), ("B4", 1), ("G4", 1), ("C5", 3)], 0.13,
               duty=0.25, vol=0.45),
        melody([("C3", 4), ("G2", 3), ("C3", 3)], 0.13, wave="triangle", vol=0.4)),
    # The end show: the Jingle Bells chorus right through, with sleigh bells,
    # then a last bright chord.
    "EndShow": lambda: seq(
        mix(melody([("E5", 1), ("E5", 1), ("E5", 2), ("E5", 1), ("E5", 1), ("E5", 2), ("E5", 1), ("G5", 1),
                    ("C5", 1.5), ("D5", 0.5), ("E5", 4),
                    ("F5", 1), ("F5", 1), ("F5", 1.5), ("F5", 0.5), ("F5", 1), ("E5", 1), ("E5", 1), ("E5", 0.5),
                    ("E5", 0.5), ("G5", 1), ("G5", 1), ("F5", 1), ("D5", 1), ("C5", 4)], 0.16, duty=0.25, vol=0.45),
            melody([("C3", 2), ("G2", 2), ("C3", 2), ("G2", 2), ("C3", 2), ("G2", 2), ("C3", 2), ("E3", 2),
                    ("F3", 2), ("C3", 2), ("C3", 2), ("A2", 2), ("G2", 2), ("G2", 2), ("C3", 4)], 0.16,
                   wave="triangle", vol=0.4),
            shake(32, 0.12, 0.18)),
        mix(bell("C6", 0.6, 0.35), bell("E6", 0.6, 0.3), bell("G6", 0.6, 0.3), bell("C7", 0.8, 0.3))),
}
