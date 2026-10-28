"""Reindeer Run: a reindeer galloping through the snow. Sleigh bells on the
big moments, soft snowy thuds and swishes for the moves (they repeat a lot),
and Jingle Bells for the end show's sleigh ride."""

import numpy as np

from synth import *


def bell(freq, dur=0.3, vol=0.4):
    # A little bell: a sine with the inharmonic partials a bell has, dying away.
    f = note(freq)
    return mix(tone(f, dur, wave="sine", vol=vol, decay=dur / 3),
               tone(f * 2.76, dur, wave="sine", vol=vol * 0.4, decay=dur / 5),
               tone(f * 5.4, dur * 0.6, wave="sine", vol=vol * 0.2, decay=dur / 8))


def sleigh_bells(dur=0.5, rate=24, seed=3):
    # A shake of sleigh bells: lots of tiny high bells, a little out of step,
    # over a breath of hiss.
    rng = np.random.default_rng(seed)
    out = noise(dur, vol=0.06, decay=dur / 2, seed=seed)
    t = 0.0
    while t < dur - 0.05:
        b = bell(2600 + rng.uniform(-300, 500), 0.12, vol=0.25 * (1 - t / dur) + 0.05)
        i = int(t * RATE)
        n = min(len(b), len(out) - i)
        out[i:i + n] += b[:n]
        t += (1 + rng.uniform(-0.4, 0.4)) / rate
    return out


def hoof(vol=0.4):
    # A hoof in snow: a muffled thud.
    return mix(noise(0.06, vol=vol, smooth=0.85, decay=0.02), tone("A2", 0.06, wave="triangle", vol=vol, decay=0.02))


JINGLE = [("E5", 1), ("E5", 1), ("E5", 2), ("E5", 1), ("E5", 1), ("E5", 2),
          ("E5", 1), ("G5", 1), ("C5", 1.5), ("D5", 0.5), ("E5", 4),
          ("F5", 1), ("F5", 1), ("F5", 1.5), ("F5", 0.5), ("F5", 1), ("E5", 1), ("E5", 1), ("E5", 0.5), ("E5", 0.5),
          ("E5", 1), ("D5", 1), ("D5", 1), ("E5", 1), ("D5", 2), ("G5", 2)]
JINGLE_BASS = [("C3", 4), ("C3", 4), ("C3", 4), ("C3", 4), ("F2", 4), ("C3", 4), ("G2", 4), ("G2", 4)]


SOUNDS = {
    # The title: a shake of bells, and the first line of Jingle Bells.
    "GameSelected": lambda: mix(
        sleigh_bells(1.4, seed=1),
        seq(silence(0.1), melody(JINGLE[:11], 0.1, duty=0.25, vol=0.35)),
        seq(silence(0.1), melody([("C3", 4), ("C3", 4), ("G2", 4)], 0.1, wave="triangle", vol=0.45))),
    # Off we go (and a second reindeer joining): bells and three notes up.
    "Start": lambda: mix(sleigh_bells(0.45, seed=2),
                         melody([("C5", 1), ("E5", 1), ("G5", 2)], 0.06, duty=0.25, vol=0.35)),
    # A spring up off the snow.
    "Jump": lambda: mix(hoof(0.3), tone("C5", 0.12, duty=0.25, freq_end="G5", vol=0.3, decay=0.08)),
    # Down low: a soft swish.
    "Duck": lambda: mix(noise(0.12, vol=0.35, smooth=0.5, decay=0.05),
                        tone("E4", 0.1, wave="triangle", freq_end="B3", vol=0.25, decay=0.05)),
    # Landing in the snow (quiet; it's every jump).
    "Land": lambda: hoof(0.5),
    # A candy cane: two little bells (quiet).
    "Candy": lambda: seq(bell("E6", 0.08, 0.4), bell("B6", 0.22, 0.4)),
    # A step faster: a run up with the bells.
    "Faster": lambda: mix(sleigh_bells(0.5, seed=4),
                          melody([("G4", 1), ("C5", 1), ("E5", 1), ("G5", 1), ("C6", 2)], 0.05, duty=0.125, vol=0.35)),
    # An owl coming on: hoo-hoo.
    "Owl": lambda: seq(tone("F#4", 0.16, wave="sine", freq_end="E4", vol=0.5, attack=0.03, release=0.05),
                       silence(0.08),
                       tone("F#4", 0.3, wave="sine", freq_end="D4", vol=0.5, attack=0.04, release=0.1, vibrato=0.3)),
    # A crash into something: a thump, a jangle and a slide down.
    "Crash": lambda: mix(
        seq(mix(noise(0.14, vol=0.6, smooth=0.6, decay=0.06), tone("C3", 0.14, wave="square", freq_end="C2")),
            tone("G4", 0.35, duty=0.25, freq_end="C3", curve=0.7, vol=0.35, release=0.06)),
        sleigh_bells(0.3, rate=30, seed=5) * 0.6),
    # Into a creek: a splash.
    "Splash": lambda: seq(
        mix(noise(0.3, vol=0.6, smooth=0.3, decay=0.1), tone("A4", 0.25, wave="sine", freq_end="A3", vol=0.3, decay=0.1)),
        tone("G4", 0.3, duty=0.25, freq_end="C3", curve=0.7, vol=0.3, release=0.06)),
    "GameOver": lambda: mix(
        melody([("G5", 1), ("E5", 1), ("C5", 1), ("-", 1), ("D5", 1), ("B4", 1), ("G4", 1), ("C5", 3)], 0.13, duty=0.25, vol=0.35),
        melody([("C3", 4), ("G2", 3), ("C3", 3)], 0.13, wave="triangle", vol=0.45)),
    # Halloween's pumpkin-coach ride: a galloping minor tune over hoofbeats
    # and a bass, ending on a long low A.
    "EndShowHalloween": lambda: mix(
        melody([("A4", 1), ("C5", 1), ("E5", 1), ("A5", 1), ("G#5", 1), ("E5", 1), ("-", 2),
                ("F5", 1), ("E5", 1), ("D5", 1), ("C5", 1), ("B4", 1), ("G#4", 1), ("-", 2),
                ("A4", 1), ("C5", 1), ("E5", 1), ("A5", 1), ("C6", 1), ("B5", 1), ("G#5", 1), ("E5", 1),
                ("A5", 4), ("A4", 4)], 0.16, duty=0.25, vol=0.35),
        melody([("A2", 2), ("E3", 2), ("A2", 2), ("E3", 2), ("D3", 2), ("A2", 2), ("E3", 2), ("E2", 2),
                ("A2", 2), ("E3", 2), ("A2", 2), ("E3", 2), ("A2", 8)], 0.16, wave="triangle", vol=0.45, legato=0.6),
        seq(*[seq(hoof(0.3), silence(0.13), hoof(0.25), silence(0.17)) for _ in range(8)])),
    # Everyday's ride: a bright galloping tune over hoofbeats and a bass.
    "EndShowEveryday": lambda: mix(
        melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 1), ("B5", 1), ("G5", 1), ("-", 2),
                ("A5", 1), ("G5", 1), ("F5", 1), ("E5", 1), ("D5", 1), ("G5", 1), ("-", 2),
                ("C5", 1), ("E5", 1), ("G5", 1), ("C6", 1), ("E6", 1), ("D6", 1), ("B5", 1), ("G5", 1),
                ("C6", 4)], 0.16, duty=0.25, vol=0.35),
        melody([("C3", 2), ("G3", 2), ("C3", 2), ("G3", 2), ("F3", 2), ("C3", 2), ("G3", 2), ("G2", 2),
                ("C3", 2), ("G3", 2), ("C3", 2), ("G3", 2), ("C3", 4)], 0.16, wave="triangle", vol=0.45, legato=0.6),
        seq(*[seq(hoof(0.3), silence(0.13), hoof(0.25), silence(0.17)) for _ in range(7)])),
    # The end show's sleigh ride: the Jingle Bells chorus over sleigh bells
    # and a bass, about five seconds.
    "EndShow": lambda: mix(
        melody(JINGLE, 0.16, duty=0.25, vol=0.35),
        melody(JINGLE_BASS, 0.16, wave="triangle", vol=0.45, legato=0.6),
        sleigh_bells(5.2, rate=8, seed=6) * 0.7),
}
