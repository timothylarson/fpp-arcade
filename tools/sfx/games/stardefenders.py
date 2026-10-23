"""Star Defenders: classic invaders - a square-wave bass march, noisy pops
for hits, a warbling saucer, and short square-wave fanfares."""

from synth import *


def thump(name):
    """One step of the march: a short, punchy bass note. Kept brief so the
    steps stay separate when the last few aliens race."""
    return mix(
        tone(name, 0.09, wave="square", vol=0.5, decay=0.05, release=0.02),
        tone(name, 0.09, wave="triangle", vol=0.5, decay=0.06, release=0.02),
    )


def pop(dur, high, low, smooth=0.3, seed=1):
    return mix(
        noise(dur, vol=0.5, smooth=smooth, decay=dur / 3, seed=seed),
        tone(high, dur, wave="square", duty=0.25, vol=0.3, freq_end=low, decay=dur / 3),
    )


def power(notes, wave="square", duty=0.25):
    return melody(notes, 0.04, wave=wave, duty=duty, vol=0.5)


SOUNDS = {
    "GameSelected": lambda: mix(
        melody([("E5", 1), ("-", 1), ("E5", 1), ("G5", 1), ("B5", 2), ("A5", 1), ("G5", 1), ("E6", 4)], 0.1,
               wave="square", duty=0.25, vol=0.4),
        melody([("C3", 2), ("Bb2", 2), ("Ab2", 2), ("G2", 2), ("C3", 4)], 0.1, wave="square", vol=0.3),
    ),
    # Four notes going down, as in the original.
    "March1": lambda: thump("C3"),
    "March2": lambda: thump("Bb2"),
    "March3": lambda: thump("Ab2"),
    "March4": lambda: thump("G2"),
    "Shoot": lambda: mix(
        tone("A6", 0.1, wave="pulse", duty=0.25, vol=0.4, freq_end="A4", decay=0.05),
        noise(0.04, vol=0.15, seed=2),
    ),
    "AlienKill": lambda: pop(0.15, "C5", "C3", smooth=0.4, seed=3),
    "DiverKill": lambda: seq(pop(0.1, "G5", "G3", seed=4), tone("G6", 0.06, wave="pulse", duty=0.125, vol=0.3)),
    "ArmourHit": lambda: mix(
        tone("E6", 0.07, wave="square", duty=0.5, vol=0.3, decay=0.03),
        tone(note("E6") * 1.41, 0.07, wave="square", duty=0.5, vol=0.25, decay=0.025),
    ),
    # 0.4 s, played back to back while it flies: a steady wobble that loops.
    "Saucer": lambda: tone("E5", 0.4, wave="square", duty=0.25, vol=0.4, vibrato=3, vib_rate=10,
                            attack=0.005, release=0.005),
    "SaucerHit": lambda: seq(
        mix(noise(0.25, vol=0.6, smooth=0.5, decay=0.1, seed=5),
            tone("E5", 0.25, wave="square", vol=0.3, freq_end="E3")),
        melody([("C6", 1), ("E6", 1), ("G6", 1), ("C7", 2)], 0.04, wave="pulse", duty=0.25, vol=0.4),
    ),
    "Laser": lambda: mix(
        tone("C7", 0.35, wave="saw", vol=0.35, freq_end="C4", vibrato=1.0, vib_rate=40, decay=0.18),
        tone("G6", 0.35, wave="square", duty=0.25, vol=0.2, freq_end="G3", decay=0.15),
        noise(0.35, vol=0.2, decay=0.1, seed=6),
    ),
    "ShieldHit": lambda: noise(0.06, vol=0.5, smooth=0.6, decay=0.02, seed=7),
    "BombShot": lambda: mix(noise(0.05, vol=0.4, decay=0.015, seed=8), tone("A5", 0.05, wave="pulse", duty=0.125, vol=0.2)),
    "PowerDouble": lambda: power([("C6", 1), ("C6", 1), ("G6", 2)]),
    "PowerRapid": lambda: power([("E6", 1), ("G6", 1), ("E6", 1), ("G6", 1), ("C7", 2)], duty=0.125),
    "PowerPierce": lambda: power([("G5", 1), ("D6", 1), ("G6", 1), ("D7", 2)], wave="triangle"),
    "PowerShields": lambda: power([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 2)], wave="triangle"),
    "PowerLaser": lambda: seq(
        power([("C6", 1), ("G6", 1)]),
        tone("C7", 0.12, wave="saw", vol=0.3, freq_end="C6", vibrato=0.8, vib_rate=40),
    ),
    "ExtraLife": lambda: melody([("E6", 1), ("G6", 1), ("E7", 1), ("C7", 1), ("D7", 1), ("G7", 2)], 0.07,
                                wave="square", duty=0.25, vol=0.4),
    "Death": lambda: mix(
        noise(0.9, vol=0.6, smooth=0.5, decay=0.3, seed=9),
        tone("C5", 0.8, wave="square", vol=0.3, freq_end="C2", vibrato=2, vib_rate=14, decay=0.35),
    ),
    "WaveStart": lambda: mix(
        melody([("G4", 1), ("C5", 1), ("E5", 1), ("G5", 2), ("E5", 1), ("G5", 4)], 0.1, wave="square", duty=0.25, vol=0.4),
        melody([("C3", 3), ("G2", 3), ("C3", 4)], 0.1, wave="triangle", vol=0.5),
    ),
    "WaveClear": lambda: mix(
        melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 2), ("G5", 1), ("C6", 4)], 0.09, wave="square", duty=0.25, vol=0.4),
        melody([("C4", 1), ("G4", 1), ("E4", 1), ("A4", 2), ("F4", 1), ("G4", 4)], 0.09, wave="triangle", vol=0.4),
    ),
    # The end show (about 5 s): the march's four thumps turned round to climb
    # instead of fall - the invaders giving up - then a square-wave fanfare
    # over a bouncing bass, and a run up to a long high C as the star lights.
    "EndShow": lambda: mix(
        seq(thump("G2"), silence(0.13), thump("A2"), silence(0.13), thump("B2"), silence(0.13), thump("C3"),
            silence(0.13),
            melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 3), ("A5", 1), ("C6", 1), ("F6", 3),
                    ("E6", 1), ("D6", 1), ("C6", 1), ("D6", 1), ("E6", 1), ("G6", 3)], 0.13,
                   wave="square", duty=0.25, vol=0.4),
            melody([("G6", 1), ("A6", 1), ("B6", 1), ("C7", 10)], 0.09, wave="square", duty=0.25, vol=0.35,
                   vibrato=0.15, vib_rate=7, release=0.4)),
        seq(silence(0.88),
            melody([("C3", 2), ("G2", 2), ("C3", 2), ("A2", 2), ("F2", 2), ("A2", 2), ("G2", 2), ("B2", 2),
                    ("G2", 2), ("C3", 2)], 0.13, wave="triangle", vol=0.5),
            tone("C3", 1.2, wave="triangle", vol=0.5, release=0.5)),
        seq(silence(3.5), *[tone(f, 0.05, wave="pulse", duty=0.125, vol=0.15, decay=0.03)
                             for f in ["C7", "G7", "E7", "C8", "G7", "E7", "C8", "G7"]]),
    ),
    "GameOver": lambda: mix(
        melody([("G4", 2), ("F#4", 2), ("F4", 2), ("E4", 6)], 0.13, wave="square", duty=0.5, vol=0.4, vibrato=0.3),
        melody([("C3", 2), ("Bb2", 2), ("Ab2", 2), ("G2", 6)], 0.13, wave="triangle", vol=0.5),
    ),
}
