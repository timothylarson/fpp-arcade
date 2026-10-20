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
| **Maze Chase** | The four directional "Pressed" events. |
| **Asteroid Orbit** | All eight directions held and released (thrust), A (fire, hold to keep firing), B (shockwave). |

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

## Maze Chase

Eat every dot while four ghosts hunt you, after the arcade maze games. Everything is one LED:
the corridors, the walls, you (yellow) and the ghosts.

- The last direction pressed is where you turn next, so it doesn't need holding.
- The blinking corner dots turn the ghosts blue for a few seconds; catch them then for 200,
  400, 800, 1600. Their eyes go back to the house in the middle and they come out again.
- The ghosts take turns at scattering to their corners and chasing, and each chases its own
  way: red heads straight for you, pink aims ahead of you, cyan works with red, and orange
  loses interest up close.
- The tunnel on each side wraps around. Each level is a new maze, generated to fit the model
  (mirrored, no dead ends), and a bit faster.
- Option: *Lives*.

## Asteroid Orbit

A thruster ship in a field of asteroids that each pull on it, after the Asteroid Orbit iOS game.

- **Directions** thrust that way for as long as they are held (diagonals too), and the ship
  points the way it is flying. Thrust uses fuel (the blue bar, top left).
- **A** fires; hold it to keep firing. Ammo is the yellow bar, top right.
- **B** sends out a shockwave that pushes asteroids away and breaks the smallest. Charges are
  the magenta dots in the middle of the top row, next to the white dots for lives.
- **Start** pauses, as in the other games.
- Every asteroid pulls on the ship, so you can swing around them. Touch one slowly and you
  land on it and ride along: that reloads ammo and refuels. Hit one fast and you crash. To
  take off, push any direction except into the rock.
- Shot asteroids split into smaller ones and sometimes leave a crystal: blue refuels, yellow
  adds ammo, magenta adds a shockwave.
- Clear the sector and a black hole opens; fly into it for the next sector, with more and
  faster asteroids and stronger gravity.
- Options: *Asteroids* at the start (0 fits the model: 3 on 60x25, 2 on a 30x25 half),
  *Gravity* (%), and *Lives*.

It needs the Released events as well as the Pressed ones, which joysticks and
`tools/arcade-keyboard.html` both send.

## Commands

The plugin registers three FPP Commands:

- **FPP Arcade Select Game** — start a game on a given overlay model.
- **FPP Arcade Button** — deliver a button press/release (Up, Down, Left, Right, Fire, Select, Start).
- **FPP Arcade Axis** — deliver an analog axis position.

Wire these to GPIO inputs, the joystick page, or any other FPP event source to provide controls.

## License

GPLv2 — see [LICENSE](LICENSE).
