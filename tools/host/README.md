# FPP Arcade host

Plays the plugin's games on a Mac, without FPP or the wall, for working on
them: a change builds in seconds instead of a full build on the player, and
the code that runs here is the code that runs there.

It builds the plugin's own sources (`src/`, all but `FPPArcade.cpp`, which is
FPP's plugin machinery: commands, the web API, the controllers) against small
stand-ins for the FPP headers they use (`stub/`), runs the games on an
overlay model the way FPP runs them, and shows the model in a window.

## Build

Needs Xcode's command line tools and SDL3 (`brew install sdl3`).

```bash
cd tools/host
make                 # build/arcade-host
make SANITIZE=1      # build-asan/arcade-host, with the address and undefined-behaviour checkers
```

`make` rebuilds only what changed. Run the `SANITIZE=1` build to chase a
crash: it stops at the first bad memory access or overflow and says where.

## Play

```bash
build/arcade-host                       # every game, Tetris first
build/arcade-host --game "Sky Jump" --theme halloween --opt "End Show=2"
```

| Key | Button |
|---|---|
| Arrows | Up, Down, Left, Right |
| Z / X | A / B |
| Return or Space | Start (start, pause) |
| Tab | Select (the next game, as on the wall) |
| Esc | Quit |

A game controller works too: its D-pad, A, B, Start and Back (Select).

As on the wall, the game's title shows until Start, Select moves on to the
next game, and the pause menu's EXIT ends a game.

- `--config plugin.fpp-arcade.json` takes the games and their settings for
  `--model` from a copy of the player's config; without it every game runs
  with its defaults. `--opt "Name=Value"` sets an option on top (the names
  are those on the Arcade Games page's options, such as `End Show`,
  `Lives`, `Pixel Scaling`).
- `--theme` picks the end shows' season: date (the default), christmas,
  halloween or everyday.
- `--size 30x25` and `--model Matrix-Left` stand in for the halves.

## In the Matrix Viewer, with sound

The host sends each frame as sACN to this Mac (`--sacn HOST` elsewhere,
`--no-sacn` for none), universe 1 on, in the show matrix's channel order (a
vertical matrix from the bottom right, zig-zag, 10 strands a string;
`--strands N` otherwise). Point a view of the Matrix Viewer at universe 1
and the game plays there on whatever wall layout that view has.

The sounds stream to the viewer as the plugin's *Stream to* setting sends
them from FPP (UDP 5570): turn on *Game sound* in the viewer's settings.
`--no-sound` turns them off.

## Where things go

The plugin's own folder is this checkout, so the bundled sounds are used.
Its data folder, `~/Library/Application Support/FPP Arcade Host/`, holds the
best scores (`best.json`) and sounds of your own (`sounds/<game>/`, as in
`media/plugindata/fpp-arcade/sounds/` on the player - Breakout's, say).

## Checks

`--headless` runs without a window: `--run-ms N` runs that long, `--press
MS:BUTTON` presses a button at a time (`--press "500:Start - Pressed"`), and
the last frame is printed as text. With the `SANITIZE=1` build that puts a
game through its paces looking for crashes:

```bash
build-asan/arcade-host --headless --no-sacn --no-sound --game Snake --run-ms 8000 \
    --press "300:Start - Pressed" --press "2000:Up - Pressed" --press "2100:Up - Released"
```

Orb Pop and Shape Fit need the Kinect, so they only show their titles here.
