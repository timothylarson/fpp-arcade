# fpp-arcade

Interactive games playable on Pixel Overlay Models for [Falcon Player (FPP)](https://github.com/FalconChristmas/fpp).

The plugin renders each game onto an FPP Pixel Overlay Model (typically an LED matrix), so a
prop becomes a playable screen. Input comes from FPP Commands, which means anything that can
trigger a command — physical buttons via GPIO, a USB gamepad, MQTT, a web request — can drive
the games.

## Games

| Game | Input used |
|------|-----------|
| **Tetris** | The four directional "Pressed" events. "Up" rotates the block. |
| **Pong** | Both "Pressed" and "Released" for all four directions. Player one uses Up/Down, player two uses Left/Right. |
| **Snake** | The four directional "Pressed" events. |
| **Breakout** | Directional "Pressed"/"Released" events. |
| **Frogger** | The four directional "Pressed" events. |
| **Orb Pop** | People in front of a Kinect (needs the fpp-kinect plugin). A starts a round; Start pauses. |

## Installation

Install from **Content Setup → Plugins** in the FPP web UI, then restart FPPD.

## Configuration

Two pages are added to the FPP UI:

- **Content Setup → Arcade Games** — pick the game and the Pixel Overlay Model to play it on.
- **Input/Output Setup → FPP Arcade Joysticks** — map a connected USB joystick or gamepad to
  the arcade button/axis commands. Detected controllers are listed automatically; "Set Defaults"
  fills in a standard mapping for a game pad or a dance mat.

## Orb Pop (Kinect)

Orb Pop is played with your body. It needs the
[fpp-kinect](https://github.com/timothylarson/fpp-kinect) plugin running on the same player
and the same overlay model, and reads who is standing where from it every frame.

- **Attract mode:** the matrix becomes a field of dim, flickering lights with two slow shaded
  orbs drifting through it, after the animated background in the Claude app. People show up as
  brighter, rounded figures, nearer is brighter, and anything that moves leaves a short glow.
- **A round:** someone standing in view for *Start After* seconds (or the A button) starts a
  3-2-1 countdown. Glowing orbs drift across the wall and burst into sparks when anyone touches
  one. Gold orbs are smaller and quicker and score 5. A bar along the top shows the time left
  and the score is in the corner. The round ends when time is up, or 8 seconds after everyone
  leaves, and shows the score before going back to attract mode.
- **Start Automatically** (on by default) switches the model to Orb Pop whenever it is showing
  the plain Kinect picture. Another game selected and started on the model takes over as usual,
  and Orb Pop comes back about a second after it ends. If the Kinect stops sending frames for
  10 seconds, Orb Pop hands the model back.
- Options: *Brightness*, *Palette* (Frost, or Christmas), *Round Length*, *Start After*,
  *Sensitivity* (the share of the LEDs a person must cover to count as someone in view), and
  *Start Automatically*.

Only something nearer than the Kinect's learned empty scene starts rounds and pops orbs, so a
wall or fence inside the distance range doesn't. Someone standing perfectly still for the
Kinect's *Relearn background after* time fades into the scene and stops counting until they
move.

`tools/orbpop-preview.html` in the MatrixPi folder runs the same drawing in a browser with a
mouse-driven stand-in for the Kinect, for trying out the look without the wall.

## Commands

The plugin registers three FPP Commands:

- **FPP Arcade Select Game** — start a game on a given overlay model.
- **FPP Arcade Button** — deliver a button press/release (Up, Down, Left, Right, Fire, Select, Start).
- **FPP Arcade Axis** — deliver an analog axis position.

Wire these to GPIO inputs, the joystick page, or any other FPP event source to provide controls.

## License

GPLv2 — see [LICENSE](LICENSE).
