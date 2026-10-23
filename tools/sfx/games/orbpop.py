"""Orb Pop: soft and magical, to suit the flickering glyph field. Sine bells
and glassy sparkles rather than square-wave chiptune, since this one plays to
groups of kids out in the cold and should sound like the lights look."""

import numpy as np

from synth import *


def bell(n, dur, vol=0.5, decay=None):
    """A small glass bell: a sine with a few slightly stretched partials, the
    upper ones dying away first."""
    f = note(n)
    d = decay or dur / 4
    return mix(
        tone(f, dur, "sine", vol=vol, decay=d, release=0.03),
        tone(f * 2.0, dur, "sine", vol=vol * 0.35, decay=d * 0.6, release=0.03),
        tone(f * 3.01, dur, "sine", vol=vol * 0.15, decay=d * 0.35, release=0.03),
        tone(f * 4.17, dur, "sine", vol=vol * 0.08, decay=d * 0.25, release=0.03),
    )


def sparkle(dur, lo=2000, hi=5000, count=10, vol=0.25, seed=3):
    """Glittery pips of random high pitch scattered through `dur`."""
    rng = np.random.default_rng(seed)
    out = silence(dur + 0.08)
    for _ in range(count):
        at = int(rng.uniform(0, dur) * RATE)
        pip = tone(rng.uniform(lo, hi), 0.07, "sine", vol=vol * rng.uniform(0.5, 1.0), decay=0.015, release=0.01)
        out[at:at + len(pip)] += pip[: len(out) - at]
    return out


def arp(notes, step, dur, vol=0.5):
    """Bells struck one after another, each left ringing under the next."""
    total = step * (len(notes) - 1) + dur
    out = silence(total)
    for k, n in enumerate(notes):
        b = bell(n, dur, vol)
        at = int(k * step * RATE)
        out[at:at + len(b)] += b
    return out


def pop(n):
    """A bubbly upward blip into a little bell, pitched to one step of the scale."""
    f = note(n)
    return mix(
        tone(f * 0.5, 0.06, "sine", vol=0.6, freq_end=f * 1.5, curve=0.6, release=0.02),
        seq(silence(0.025), bell(f, 0.22, 0.45, decay=0.05)),
    )


# C major pentatonic, two octaves up from C5: pops in a quick run climb it.
SCALE = ["C5", "D5", "E5", "G5", "A5", "C6", "D6", "E6"]

SOUNDS = {
    "GameSelected": lambda: mix(
        arp(["C5", "E5", "G5", "C6", "E6", "G6"], 0.11, 0.7, 0.5),
        seq(silence(0.3), sparkle(0.9, count=14, vol=0.2)),
    ),
    # 3, 2, 1: a soft bell; GO: the same bell with its fifth and octave above.
    "Count": lambda: bell("A5", 0.35, 0.5, decay=0.1),
    "Go": lambda: mix(
        arp(["A5", "E6", "A6"], 0.05, 0.5, 0.45),
        seq(silence(0.08), sparkle(0.3, count=6, vol=0.2, seed=5)),
    ),
    **{f"Pop{k + 1}": (lambda n=n: pop(n)) for k, n in enumerate(SCALE)},
    # Gold: a quick shower of bells up an octave, wrapped in glitter.
    "GoldPop": lambda: mix(
        arp(["G6", "C7", "E7", "G7"], 0.035, 0.25, 0.45),
        sparkle(0.3, lo=3000, hi=7000, count=12, vol=0.3, seed=9),
        pop("C6") * 0.8,
    ),
    "Tick": lambda: tone("E7", 0.06, "sine", vol=0.4, decay=0.012, release=0.01),
    # Time's up: a gentle falling chime, not a buzzer.
    "RoundEnd": lambda: arp(["G6", "E6", "C6", "G5"], 0.09, 0.45, 0.45),
    "Score": lambda: mix(
        arp(["C5", "G5", "C6", "E6", "G6", "C7"], 0.08, 0.9, 0.4),
        seq(silence(0.45), bell("C6", 1.1, 0.35, decay=0.35)),
        seq(silence(0.45), bell("E6", 1.1, 0.25, decay=0.35)),
        seq(silence(0.4), sparkle(1.1, count=18, vol=0.18, seed=11)),
    ),
    # The end show after a good round: a music-box tune of bells climbing the
    # pentatonic scale the pops use, a swell of glitter as the giant orb
    # bursts, and a long shimmering chord as the lanterns float away.
    "EndShow": lambda: mix(
        arp(["C5", "E5", "G5", "C6", "D6", "E6", "G6", "E6", "D6", "C6", "D6", "E6", "G6", "A6"], 0.16, 0.6, 0.45),
        seq(silence(2.3), arp(["C6", "G6", "C7", "E7", "G7"], 0.04, 0.4, 0.4)),
        seq(silence(2.3), sparkle(0.9, lo=3000, hi=7000, count=22, vol=0.3, seed=13)),
        seq(silence(2.6), bell("C6", 2.6, 0.4, decay=0.9)),
        seq(silence(2.6), bell("E6", 2.6, 0.3, decay=0.9)),
        seq(silence(2.6), bell("G6", 2.6, 0.25, decay=0.9)),
        seq(silence(2.8), sparkle(2.2, count=26, vol=0.16, seed=17)),
    ),
}
