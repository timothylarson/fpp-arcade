"""Road Race: a little car on a snowy road. A soft engine hum that climbs with
the speed, toots and whooshes for the traffic, a crunch for a crash, and a
3-2-1-GO on the start line; Deck the Halls for the end show."""

from synth import *


def _engine(freq):
    # A short burst of hum, played back to back by the game every 0.3 s and
    # pitched by its speed: a buzzy saw with a fast wobble for the pistons and
    # a quieter octave above so it carries on small speakers. Soft at both
    # ends so the repeats run into each other without clicking.
    f = note(freq)
    return mix(tone(f, 0.34, wave="saw", vol=0.5, vibrato=0.4, vib_rate=22, attack=0.05, release=0.08),
               tone(f * 2, 0.34, wave="square", duty=0.3, vol=0.15, vibrato=0.4, vib_rate=22, attack=0.05, release=0.08),
               noise(0.34, vol=0.08, smooth=0.85, attack=0.05, release=0.08, seed=4))


def _horn(dur=0.22):
    # A two-note car horn.
    return mix(tone("F4", dur, wave="square", duty=0.5, vol=0.35, release=0.03),
               tone("A4", dur, wave="square", duty=0.4, vol=0.3, release=0.03))


def _crunch(dur, seed):
    return mix(noise(dur, vol=0.8, smooth=0.35, decay=dur / 3, seed=seed),
               tone("D3", dur, wave="square", freq_end="D1", vol=0.5, decay=dur / 2))


SOUNDS = {
    # The title: two toots of the horn and a quick run up to the top.
    "GameSelected": lambda: seq(
        _horn(0.12), silence(0.06), _horn(0.2), silence(0.08),
        mix(melody([("C5", 1), ("E5", 1), ("G5", 1), ("C6", 1), ("E6", 1), ("G6", 3)], 0.07, wave="square", duty=0.25),
            melody([("C3", 3), ("G3", 3), ("C4", 2)], 0.07, wave="triangle", vol=0.5))),
    # On the line: 3, 2 and 1 low, 0.6 s apart as the game counts, then GO high.
    "Start": lambda: seq(
        tone("A4", 0.18, wave="square", duty=0.25, release=0.03), silence(0.42),
        tone("A4", 0.18, wave="square", duty=0.25, release=0.03), silence(0.42),
        tone("A4", 0.18, wave="square", duty=0.25, release=0.03), silence(0.42),
        mix(tone("A5", 0.45, wave="square", duty=0.25, release=0.1), tone("A4", 0.45, wave="triangle", vol=0.4, release=0.1))),
    # The second car joins during the countdown.
    "Join": lambda: seq(tone("E5", 0.06, wave="square", duty=0.25), tone("A5", 0.1, wave="square", duty=0.25, decay=0.06)),
    # The engine, one clip per step of speed.
    "Engine1": lambda: _engine("A1"),
    "Engine2": lambda: _engine("C2"),
    "Engine3": lambda: _engine("E2"),
    "Engine4": lambda: _engine("G2"),
    "Engine5": lambda: _engine("B2"),
    "Engine6": lambda: _engine("D3"),
    # A lane change: a soft swish of tyres.
    "Lane": lambda: noise(0.07, vol=0.4, smooth=0.6, attack=0.01, decay=0.03, seed=2),
    # Against the kerb: a dull thump.
    "Bump": lambda: mix(noise(0.08, vol=0.6, smooth=0.9, decay=0.03, seed=5), tone("E2", 0.08, wave="triangle", decay=0.04)),
    # Overtaking: a quick whoosh going by.
    "Pass": lambda: mix(noise(0.16, vol=0.5, smooth=0.75, attack=0.05, release=0.08, seed=3),
                        tone("C4", 0.16, wave="triangle", freq_end="G3", vol=0.15, attack=0.05, release=0.08)),
    # An oncoming car on its way: its horn.
    "Horn": lambda: seq(_horn(0.14), silence(0.05), _horn(0.14)),
    # A present picked up: sleigh-bell sparkle up an arpeggio.
    "Pickup": lambda: mix(melody([("C6", 1), ("E6", 1), ("G6", 1), ("C7", 2)], 0.045, wave="square", duty=0.125),
                          noise(0.25, vol=0.12, smooth=0.0, decay=0.08, seed=8)),
    # The turbo star: a rising roar.
    "Boost": lambda: mix(tone("C3", 0.55, wave="saw", freq_end="C5", curve=0.6, vol=0.45, release=0.1),
                         noise(0.55, vol=0.2, smooth=0.5, attack=0.1, release=0.15, seed=6),
                         seq(silence(0.4), tone("C6", 0.15, wave="square", duty=0.25, vol=0.3, decay=0.08))),
    # Ploughing through a car on turbo: a short crunch and a bright ding.
    "Smash": lambda: mix(_crunch(0.18, 9), seq(silence(0.04), tone("E6", 0.18, wave="triangle", vol=0.4, decay=0.08))),
    # A crash: a big crunch, the tone falling away, and a tinkle of glass.
    "Crash": lambda: mix(_crunch(0.45, 7),
                         seq(silence(0.12), tone("G5", 0.35, wave="square", duty=0.25, freq_end="C4", curve=0.7, vol=0.25)),
                         seq(silence(0.2), noise(0.25, vol=0.15, decay=0.07, seed=11))),
    # Into a faster, busier stretch.
    "Faster": lambda: seq(tone("G5", 0.06, wave="square", duty=0.25), tone("D6", 0.12, wave="square", duty=0.25, decay=0.06)),
    # One player out of the race (the other's still going).
    "Out": lambda: melody([("G4", 1), ("E4", 1), ("C4", 2)], 0.1, wave="square", duty=0.25),
    # The engine sputtering out and a sad little tune going down.
    "GameOver": lambda: seq(
        mix(tone("C2", 0.12, wave="saw", vol=0.4, release=0.04), noise(0.12, vol=0.2, smooth=0.8, seed=4)), silence(0.1),
        mix(tone("A1", 0.12, wave="saw", vol=0.35, release=0.04), noise(0.12, vol=0.18, smooth=0.8, seed=5)), silence(0.15),
        mix(melody([("G5", 1), ("E5", 1), ("C5", 1), ("-", 1), ("D5", 1), ("B4", 1), ("G4", 1), ("C5", 3)], 0.12, wave="square", duty=0.25),
            melody([("C3", 4), ("G2", 3), ("C3", 3)], 0.12, wave="triangle", vol=0.45))),
    # Halloween's trick-or-treat drive: the engine revving, then a sneaky
    # minor-key tiptoe over a walking bass, and the horn.
    "EndShowHalloween": lambda: seq(
        tone("C2", 0.5, wave="saw", freq_end="C4", curve=0.8, vol=0.4, vibrato=0.3, vib_rate=22, release=0.05),
        silence(0.05),
        mix(melody([("E4", 1), ("G4", 1), ("B4", 1), ("C5", 1), ("B4", 1), ("A#4", 1), ("B4", 2),
                    ("E4", 1), ("G4", 1), ("B4", 1), ("D5", 1), ("C5", 1), ("B4", 1), ("E4", 2)],
                   0.2, wave="square", duty=0.25, vol=0.5, legato=0.5),
            melody([("E2", 1), ("B2", 1), ("E2", 1), ("B2", 1), ("A2", 1), ("B2", 1), ("E2", 2),
                    ("E2", 1), ("B2", 1), ("G2", 1), ("B2", 1), ("A2", 1), ("B2", 1), ("E2", 2)],
                   0.2, wave="triangle", vol=0.55, legato=0.6)),
        silence(0.05), _horn(0.12), silence(0.05), _horn(0.3)),
    # Everyday's drive home: the engine revving, then a bouncy tune and the horn.
    "EndShowEveryday": lambda: seq(
        tone("C2", 0.5, wave="saw", freq_end="C4", curve=0.8, vol=0.4, vibrato=0.3, vib_rate=22, release=0.05),
        silence(0.05),
        mix(melody([("G4", 1), ("C5", 1), ("E5", 1), ("G5", 1), ("E5", 1), ("C5", 1), ("D5", 2),
                    ("F5", 1), ("E5", 1), ("D5", 1), ("B4", 1), ("C5", 4)],
                   0.2, wave="square", duty=0.25, vol=0.5),
            melody([("C3", 1), ("G2", 1), ("C3", 1), ("G2", 1), ("C3", 1), ("G2", 1), ("G2", 2),
                    ("D3", 1), ("G2", 1), ("G2", 1), ("G2", 1), ("C3", 4)],
                   0.2, wave="triangle", vol=0.55, legato=0.7)),
        silence(0.05), _horn(0.12), silence(0.05), _horn(0.3)),
    # The end show: the engine revving up, then Deck the Halls over a bouncy
    # bass, ending on a toot of the horn.
    "EndShow": lambda: seq(
        tone("C2", 0.5, wave="saw", freq_end="C4", curve=0.8, vol=0.4, vibrato=0.3, vib_rate=22, release=0.05),
        silence(0.05),
        mix(melody([("D5", 1.5), ("C5", 0.5), ("B4", 1), ("A4", 1), ("G4", 1), ("A4", 1), ("B4", 1), ("G4", 1),
                    ("A4", 0.5), ("B4", 0.5), ("C5", 0.5), ("A4", 0.5), ("B4", 1.5), ("A4", 0.5), ("G4", 1), ("F#4", 1), ("G4", 2)],
                   0.2, wave="square", duty=0.25, vol=0.5),
            melody([("G2", 1), ("D3", 1), ("G2", 1), ("D3", 1), ("G2", 1), ("D3", 1), ("G2", 1), ("D3", 1),
                    ("D3", 1), ("A2", 1), ("G2", 1), ("D3", 1), ("D3", 1), ("D2", 1), ("G2", 2)],
                   0.2, wave="triangle", vol=0.55, legato=0.7)),
        silence(0.05), _horn(0.12), silence(0.05), _horn(0.3)),
}
