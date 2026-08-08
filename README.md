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

## Installation

Install from **Content Setup → Plugins** in the FPP web UI, then restart FPPD.

## Configuration

Two pages are added to the FPP UI:

- **Content Setup → Arcade Games** — pick the game and the Pixel Overlay Model to play it on.
- **Input/Output Setup → FPP Arcade Joysticks** — map a connected USB joystick or gamepad to
  the arcade button/axis commands. Detected controllers are listed automatically; "Set Defaults"
  fills in a standard mapping for a game pad or a dance mat.

## Commands

The plugin registers three FPP Commands:

- **FPP Arcade Select Game** — start a game on a given overlay model.
- **FPP Arcade Button** — deliver a button press/release (Up, Down, Left, Right, Fire, Select, Start).
- **FPP Arcade Axis** — deliver an analog axis position.

Wire these to GPIO inputs, the joystick page, or any other FPP event source to provide controls.

## License

GPLv2 — see [LICENSE](LICENSE).
