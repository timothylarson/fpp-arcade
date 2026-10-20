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
| **Barrel Climb** | All four directions held and released (walk, climb), and A (jump). |
| **Star Defenders** | Left/Right held and released, A (fire, hold to repeat; Up also fires), B (laser). |
| **Shape Fit** | People in front of a Kinect (needs the fpp-kinect plugin). Any button starts; Start pauses. |
| **Stacker** | Any button drops the sliding row. |
| **Flap** | A, B or Up flaps. |
| **Light Memory** | The four directional "Pressed" events. |
| **Asteroid Orbit** | All eight directions held and released (thrust), A (fire, hold to keep firing), B (shockwave). |

## Installation

Install from **Content Setup → Plugins** in the FPP web UI, then restart FPPD.

## Configuration

Two pages are added to the FPP UI:

- **Content Setup → Arcade Games** — the games on each Pixel Overlay Model, numbered as *Select
  Game* counts them, with what each one is, its controls and its settings. A status line shows
  what every model is doing, and Play starts a game on the wall.
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

## Barrel Climb

Climb to the top while an ape rolls barrels down at you, after the classic girder games.

- Left/Right walk along the sloping girders, Up/Down climb a ladder (two LEDs wide) you are at,
  and A (or B) jumps. Jumping clean over a barrel scores 100.
- Barrels roll downhill, drop off the low end of each girder to the one below, and
  sometimes take a ladder down. Touching one costs a life, and so does walking off the end
  of a girder: it's too far to fall.
- Reach the top girder to rescue the person there; the bonus that counts down while you
  climb is added to the score. Each level moves the ladders and throws barrels 0.6 seconds
  sooner, until it reaches the fastest rate.
- On a model 40 or more wide there are two ladders between girders, otherwise one.
- Options: *Lives*, *Barrels Every* (seconds between barrels on level 1, default 6) and
  *Fastest Barrels* (the quickest it gets, default 3.2).

## Star Defenders

Hold off a marching alien formation from behind four shields, after the classic invaders.

- Left/Right move the cannon, A fires (hold it to keep firing; Up fires too, for controllers
  without buttons), and B fires a laser straight up that clears a whole column - once a wave.
  The magenta dot top left shows it's ready; white dots top right are spare lives.
- The formation steps faster as it thins out and drops a row at each edge. Shots from either
  side wear holes in the shields, and the aliens flatten them as they march through.
- A saucer crosses the top row now and then for 50 to 300. If the aliens reach the bottom
  the game is over. Each wave starts a little lower and bombs more.
- Option: *Lives*.

## Shape Fit (Kinect)

A "hole in the wall" game for whoever is in front of the Kinect. It needs the fpp-kinect plugin
running on the same model.

- Any button starts five rounds. Each shows the name of a pose (STAR, ARMS UP, T, CROUCH,
  LEAN, FLAMINGO, or PAIR for two people) and then its outline. Everyone in front shows up
  green where they're inside the outline and red where they're outside it.
- When the countdown runs out the fit is scored out of 100: how much of the shape is filled,
  less half of what spills outside it. PERFECT, GREAT, GOOD, OK or MISS, and a total at the end.
- People change size with distance, so stepping nearer or farther is part of fitting the shape.
  The shapes are stretched sideways the same way the Kinect's picture is stretched onto the
  matrix (about 1.8 times on a 60x25 model).
- Options: *Shape Size* (% of the model's height) and *Seconds per Pose*.
- With no Kinect frames it says NO KINECT and hands the model back after 5 seconds.

## Stacker, Flap and Light Memory

Three quick games for people walking up, each playable with one or four buttons:

- **Stacker:** a row of blocks a quarter of the width slides back and forth, a little faster
  each row. Any button drops it on the stack, and whatever hangs over the edge falls off and
  the next row is that much narrower. The tower is taller than the screen: past the lower
  third the view follows it up. Reach the top to win.
- **Flap:** A, B or Up flaps; fly the bird through the gaps in the pipes. The gaps narrow and
  the pipes speed up as the score climbs.
- **Light Memory:** watch the four triangles light up in sequence, then repeat it with the
  directions (Up is the top triangle, Right the right one, and so on). The sequence grows by
  one each round and plays faster; a white dot in the middle means it's your turn. A wrong
  press, or six seconds without one, ends it and flashes the one you needed.

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
