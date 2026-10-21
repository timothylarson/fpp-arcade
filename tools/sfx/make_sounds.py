#!/usr/bin/env python3
"""Render the bundled game sounds: every SOUNDS entry in games/<game>.py to
sounds/<game>/<name>.wav in the plugin (16-bit mono 44.1 kHz).

    python3 tools/sfx/make_sounds.py              all games
    python3 tools/sfx/make_sounds.py tetris pong  just these

Each sound is normalised to the same peak, so games sound equally loud, and
gets a 5 ms fade at each end so nothing clicks.
"""

import importlib.util
import pathlib
import sys
import wave

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from synth import RATE  # noqa: E402

PLUGIN = HERE.parent.parent
PEAK = 0.7   # about -3 dBFS, leaving room when several sounds mix


def render(game):
    spec = importlib.util.spec_from_file_location(f"games.{game}", HERE / "games" / f"{game}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    out = PLUGIN / "sounds" / game
    out.mkdir(parents=True, exist_ok=True)
    wanted = set(mod.SOUNDS)
    for old in out.glob("*.wav"):
        if old.stem not in wanted:
            old.unlink()   # a sound dropped from the recipe
    for name, make in mod.SOUNDS.items():
        x = np.asarray(make(), dtype=float)
        peak = np.max(np.abs(x)) or 1.0
        x = x / peak * PEAK
        f = min(len(x) // 2, int(RATE * 0.005))
        if f:
            x[:f] *= np.linspace(0, 1, f)
            x[-f:] *= np.linspace(1, 0, f)
        pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
        with wave.open(str(out / f"{name}.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(pcm.tobytes())
        print(f"{game}/{name}.wav  {len(x) / RATE:.2f}s")


def main():
    games = sys.argv[1:] or sorted(p.stem for p in (HERE / "games").glob("*.py") if p.stem != "__init__")
    for g in games:
        render(g)


if __name__ == "__main__":
    main()
