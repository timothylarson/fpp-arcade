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
| **Pong** | "Pressed" and "Released" events. Player one uses Up/Down; player two uses Left/Right, or B (up) and A (down) with the *Up/Down and B/A* controls setting. |
| **Snake** | The four directional "Pressed" events; A or B brings in a second snake for a duel (B turns it left, A right). |
| **Breakout** | Directional "Pressed"/"Released" events. |
| **Frogger** | The four directional "Pressed" events hop; A speeds the traffic up a step, B slows it back down to the level's own speed. |
| **Orb Pop** | People in front of a Kinect (needs the fpp-kinect plugin). A starts a round; Start pauses. |
| **Maze Chase** | The four directional "Pressed" events. |
| **Barrel Climb** | All four directions held and released (walk, climb), and A (jump). |
| **Star Defenders** | Left/Right held and released, A (fire, hold to repeat; Up also fires), B (laser). |
| **Shape Fit** | People in front of a Kinect (needs the fpp-kinect plugin). Any button starts; Start pauses. |
| **Stacker** | The directions, or A, B or Fire, drop the sliding row; the other control at the start of a level makes it a race. |
| **Flap** | Up flaps the yellow bird; A or B a second, blue one, for two players. |
| **Light Memory** | The four directional "Pressed" events. |
| **Breakout Versus** | Two players: Left/Right and Up for the bottom paddle; B and A (held) for the top one. |
| **Stick Bridge** | Hold any direction, or A/B, to grow the stick; let go to drop it. The other control before the first drop splits the wall for two. |
| **Sky Jump** | Left/Right held and released (steer), Up (launch); or B and A held (steer left/right), either launches. The other control before launch starts a race. |
| **Reindeer Run** | Up jumps (hold for higher), Down ducks; A and B do the same for a second reindeer in its own lane. |
| **Road Race** | Up/Down change lane (alone, Right faster and Left brake); B/A (up/down) drive a second car on its own road. |
| **Snowball Fight** | Left/Right aim and Up (held, then released) throws for the red elf; B swings the aim and A throws for the green one. Alone, the computer plays green. |
| **Asteroid Orbit** | All eight directions held and released (thrust), A (fire, hold to keep firing), B (shockwave). |

**Start pauses any game**, with a menu of RESUME, RESTART and EXIT: Up/Down (or Left/Right)
choose, and Start, A or B picks. EXIT ends the game as GAME OVER would, without its end show.

## New in the arcade: Stick Bridge, Sky Jump, Reindeer Run, Road Race, Snowball Fight

- **Stick Bridge:** hold a direction (or A/B) to grow a candy cane up from the edge of your gift
  box, and let go to drop it across the gap. If its end lands on the next box the elf walks over
  and the view slides along; too short or too long and the elf walks off the end and falls. Each
  bridge is a point, and landing right on a box's red bow is a PERFECT, worth one more. The boxes
  get narrower and the gaps wider as the score rises. Pressing the other control before the first
  stick drops (or in the first couple of seconds) splits the wall in two, directions on the left
  and A/B on the right; the higher score wins. End show (score 10): candy-cane bridges to the
  North Pole.
- **Sky Jump:** blast off from the snow with Up (or A) and keep climbing. Coins give a hop,
  blasts a big boost, a star five seconds of smashing through clouds, bats and meteors; without
  one, an enemy stops your climb. Steer with Left/Right (or B/A); the elf wraps round the sides.
  Fall off the bottom and it's over. Press the other control before launch (or just after) to
  race: D-pad on the left half, A/B on the right, the same course on both. Settings: *Lives* (1)
  and *Speed*. End show (score 600): past the moon and Santa's sleigh to the North Star.
- **Reindeer Run:** an endless runner on a snowy night. Up (or A) jumps, and holding it jumps
  higher; Down (or B) ducks under boughs, icicles and swooping owls. Jump the logs, presents,
  snowmen, ice and creeks, and grab candy canes for 10 points. It speeds up in six steps over
  *Seconds to top speed* (80) and one hit ends the run. The directions run Rudolph and A/B run
  Dasher: the other control joins at any time, splitting the wall into two lanes on the same
  course, and the longer run wins. End show (score 600): the reindeer joins Santa's team.
- **Road Race:** the road runs across the wall. Change lane with Up and Down past traffic that
  speeds up - cars and vans, then trucks, buses and a snowplough, and later oncoming cars in the
  top lane, each warned of by a blinking light at the right edge. Presents score, turbo stars make
  you fast and unstoppable for a few seconds, a crash costs one of three *Lives*. Alone on the
  D-pad, Right goes faster and Left brakes. A second player joins at any time with the other
  control (B up, A down) and the wall splits into two roads with the same traffic. End show
  (2000 m): a drive home through a snowy town, dropping presents on the doorsteps.
- **Snowball Fight:** two elves on snowy hills take turns throwing snowballs. The red elf uses the
  D-pad (Left/Right aim, hold Up to charge, let go to throw); the green elf A and B (hold B to swing
  the aim, each press the other way; hold A to charge, let go). The wind arrow at the top (and the
  snow) changes every turn; every snowball blows a crater out of the hill, and an elf whose snow is
  blown away drops into the hole. First to knock the other out (*Hits*, 3) wins; a *Turn time*
  (15 s) keeps it moving. Alone, the computer throws for green (*Computer skill*) and gets better
  as the fight goes on; pressing A or B takes over. End show: the winner builds a giant snowman
  (after beating the computer, or a two-player win with *Good game* health left).

### Two players

Pong, Flap, the Snake duel, the Stacker race and Breakout Versus take a second player on the same controller,
one on the directions and one on A and B, without choosing anything first: the second player
just presses their control. In the **Snake duel** a blue snake joins in a clear corner; B turns
it left and A right (two buttons can't give four directions, but they can steer, like light
cycles). Both eat and grow, and the first to hit a wall, itself or the other snake loses - head
on is a tie. Their end shows name the winner and show both scores; the better one counts as a
best.

**Breakout Versus** is its own game, as in one-player Breakout A and B already launch and fire,
so they can't also mean "join". A wall of bricks runs across the middle, with a paddle on the
bottom row (Left/Right, and Up to launch or fire) and one on the top row (B left, A right; its
ball launches by itself and its laser fires by itself, as it has no button left over). Each
player has a ball in their colour, yellow and blue. A ball that gets past a paddle scores for the
other player, whoever's ball it was; bricks are in the way and break when hit, and sometimes
drop a capsule toward whoever broke them: blue for a wider paddle, red for a laser - as in
Arkanoid one at a time for each player, never the power they already have, and catching one
ends the other. Balls leave a paddle at an angle, never straight; *Paddle speed* (1.5) sets how
fast both paddles move, and every tap moves one at least an LED. A new wall
goes up when one is gone, the balls quicken a little with every goal, and the first to *Goals
to win* (5) wins. Its end show (after a win by Good Game goals, 3) has the winner's paddle smash
a rainbow ball through the wall.

### Cheat codes

Entered during a game (not on the pause menu), each press within 2 seconds of the last:

- **Up Up Down Down Left Right Left Right B A Start** (the Konami code): the last level, below.
- **Up Up Down Down Left Right Left Right A B Start**: the next level, as if this one were
  cleared (in Breakout that includes its two extra lives, and after the last level, the
  credits). Handy for trying each level in turn.

The directions play as usual; the B, A and Start that finish a code are kept from the game, so
nothing fires, drops a row or pauses.

| Game | What it does |
|------|-----------|
| **Breakout** | The final level, with 9 lives. |
| **Tetris** | Full drop speed (reached at 14 lines, level 2). The board stays. |
| **Frogger** | Level 10, traffic about twice level 1's speed, and 9 lives. |
| **Maze Chase** | Level 10 (every speed and timer at its limit, the narrow maze), and 9 lives. |
| **Asteroid Orbit** | The sector where the rock count stops growing (at least 10), with full fuel and ammo, 9 shockwaves and 5 lives. |
| **Barrel Climb** | The level where the barrels are at their fastest (at least 9), and 9 lives. |
| **Star Defenders** | Wave 14, the fortress at full strength, with 9 lives, 5 lasers and the piercing shot. |
| **Stacker** | The first 60-row tower. |

Pong, Snake, Flap, Light Memory, Orb Pop and Shape Fit have no levels and ignore both codes.

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

## End shows

A good game ends with a show of about 20 seconds instead of a plain GAME OVER: a headline
(NEW BEST! or GREAT GAME!) bouncing in over fireworks, the score and the like counting up, a
little story of that game's own, rolling credits and GAME OVER. Any button skips it after two
seconds. Breakout has its longer win show instead (below).

Every game has two settings for it. *End show*: after a good game (the default), after every
game, or never. *Good game*: the level, wave, length or score that makes a game a good one; a
new best score always counts too. Best scores are kept per model and game in
`media/plugindata/fpp-arcade/best.json` (delete it to start afresh). Each show plays its game's
`EndShow` sound as it starts (see SOUNDS.md).

*End shows* on the Arcade Games page sets the season they're dressed for. **By date** (the
default) is Halloween through October, Christmas from 15 November to 6 January and everyday the
rest of the year; or pick Christmas, Halloween or Everyday to keep to one. The stories below are
the Christmas ones. At Halloween each has its spooky, kid-friendly side - an orange harvest moon
with bats and falling leaves instead of snow, jack-o'-lanterns, witches' hats, candy corn, orange
and purple fireworks - and plays `EndShowHalloween`; an everyday show has a clear night, party
hats, balloons and trophies, and plays `EndShowEveryday`. MERRY CHRISTMAS in the credits
(Breakout's own lines included) becomes HAPPY HALLOWEEN or SEE YOU NEXT TIME. Breakout keeps its
`WonGame` music in every season; a game without a sound for the season plays its `EndShow`.
Controller speakers are only offered the season's end-show music, so they hold one set: change
the setting and they swap it for the other within a minute.

What counts as good, and each game's story:

- **Tetris** (level 5): falling tetrominoes build a Christmas tree; a star lands on top and
  lights up the ornaments under fireworks.
- **Pong** (a win by 3 or more; Pong keeps no best score): one last rally that speeds up until
  the ball bursts into fireworks, then the paddles shake hands and the winner gets a Santa hat.
- **Snake** (a length of 20): the snake takes a victory lap, eating a line of apples in straight
  runs, then a golden apple that turns it rainbow, and slithers off the edge.
- **Frogger** (level 4): the frog, in a Santa hat, makes one last crossing over a snowy road and
  an icy river to its home bay, where its family pops up and they hop for joy.
- **Maze Chase** (level 3): the arcade intermission - the ghost, in a Santa hat, chases the
  hero; a giant hero chases the scared ghost back and takes its hat. The cast parades under the
  credits.
- **Asteroid Orbit** (sector 4): the ship flies a victory loop, blasts rocks, collects their
  crystals and warps through a black hole into a Christmas star.
- **Barrel Climb** (level 3): the rescue - the climber reaches the top, the rivets pop, the ape
  tumbles to the floor and a heart appears between the climber and the friend.
- **Star Defenders** (wave 5): the aliens wave a white flag and fly off, the saucer drops a
  present the cannon catches, and the cannon's fireworks light a Christmas star while the aliens
  dance.
- **Stacker** (level 3): the tower builds itself into a lit Christmas tree with a star on top.
- **Flap** (15 pipes): the bird, in a Santa hat, flies a victory lap, turning every pipe it gets
  through into a candy cane, then zooms off in a trail of sparkles.
  After two players: the winner's name, both scores, and both birds on the lap (the better
  score is the one that counts as good and as a best).
- **Light Memory** (10 rounds): the four triangles play Jingle Bells, spin through every colour,
  flash red and green and finish in a golden starburst.
- **Orb Pop** (a round of 50; the show replaces the score screen, and attract mode follows as
  before): the orbs gather into one giant orb that bursts and floats away as lanterns. No round
  starts while it plays.
- **Shape Fit** (a total of 300): the five poses just played dance across the wall in a conga
  line, then a big STAR outline fills up and turns gold.

`tools/shows/preview.sh` renders any game's show to a contact sheet, without the player.

## Breakout

Fifteen levels of bricks, with power-ups that fall from broken bricks: **laser** (red: the
paddle's ends turn red and A or B fires two beams), **expand** (blue), **triple ball** (cyan),
**extra life** (red and white checks, like a candy cane), **slow** (orange), **power ball**
(yellow, goes through bricks), **sticky** (green) and the **portal** (magenta, to the next level).
As in Arkanoid, one capsule falls at a time, none during triple ball, and never the power the
paddle already has; *Capsule chance* (20%) is how many broken bricks drop one when one can.
Catching a capsule ends the paddle's sticky, expand or laser, so it has one of those at a time
(slow and the power ball carry on). The ball leaves the paddle at a slight angle, never straight
up. **Silver** bricks take two or three hits and **gold** ones can't be broken; both ring and
flash when hit, and a shine crosses them now and then. Clearing a level gives two extra lives (up to 9, the
same limit as the extra-life capsule). Each level makes the ball and the paddle 10% faster,
up to *Fastest ball* (1.3 times level 1 by default, reached at level 4). *Ball speed* is 0.8 by
default - the wall is only 25 LEDs tall, so the ball is soon back at the paddle - and *Paddle
speed* 1.4 (how fast the paddle moves to start with; 1 is how it moved before the setting).
The brick rows are stacked edge to edge, Arkanoid style, with bricks twice as tall where the
level fits (it always ends above the lower third), and *Space above bricks* (3 LEDs by default)
open above them for the ball to get into. *Gap between rows* at Yes spaces the rows out instead,
as before.

After level 15 comes the **final boss**: an evil snowman at the top of the field, with no bricks.
Hit him *Boss hits* times (16, as many as Arkanoid's DOH; 0 leaves him out) while he throws
icy-blue snowballs at the paddle - each one costs a life, so dodge them. He holds the next snowball up in
a stick hand before he throws it, throws more often and faster as he weakens, and his health runs
along the bottom row. Beaten, he melts down to his hat, and the credits play. The last-level cheat
goes straight to him.

Clearing the last level plays the credits, for as long as the WonGame music (30 seconds with
sound off): YOU WON! over fireworks, the bricks broken and time played, a short story (Santa's
sleigh comes for the ball and drops presents down the chimneys), rolling credits and THE END.
*Special thanks* sets the names under SPECIAL THANKS (blank leaves it out) and *Credits lines*
adds your own lines at the end of the roll, both separated by `|` (letters, numbers and
`: ! . , - '`); *Credits when won* turns the show off. Any button skips it after two seconds.

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
  and the score is in the corner. The round ends when time is up, or 15 seconds after the Kinect
  sees no one at all, and shows the score before going back to attract mode.
- **Start Automatically** (off by default) switches the model to Orb Pop whenever it is showing
  the plain Kinect picture. Another game selected and started on the model takes over as usual,
  and Orb Pop comes back about a second after it ends. If the Kinect stops sending frames for
  10 seconds, Orb Pop hands the model back.
- Options: *Brightness*, *Palette* (Frost, or Christmas), *Round Length*, *Start After*,
  *Sensitivity* (the share of the LEDs a person must cover to count as someone in view), and
  *Start Automatically*.
- **Overlay Transparent** (the default for Orb Pop) plays it over the running sequence: the dim
  field and the attract-mode orbs are left out, so the sequence is the backdrop, and the
  fpp-kinect plugin's *Sequence Brightness* dims it under the players as it does for the plain
  Kinect picture. The players themselves are drawn steady and at full brightness, as the Kinect
  picture draws them, whatever *Brightness* is set to. Overwrite keeps Orb Pop's own field over the whole matrix. Every other game is
  meant to be played with Overwrite.

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
- The first levels have paths two LEDs wide, with 2x2 characters; from the *Narrow maze from
  level* setting on (default 4) the maze is one LED per path, wall and character, which fits
  about twice as many turns. 0 keeps the wide paths.
- Options: *Lives* and *Narrow maze from level*.

## Barrel Climb

Climb to the top while an ape rolls barrels down at you, after the classic girder games.

- Left/Right walk along the sloping girders, Up/Down climb a ladder (two LEDs wide) you are at,
  and A (or B) jumps. Jumping clean over a barrel scores 100.
- Barrels roll downhill, drop off the low end of each girder to the one below, and
  sometimes take a ladder down. Touching one costs a life, and so does walking off the end
  of a girder: it's too far to fall.
- Reach the top girder to rescue the person there; the bonus that counts down while you
  climb is added to the score. Lives left show as little people in the top right corner.
- Each level gets harder: new ladder positions, barrels thrown sooner (down to the fastest
  rate) and rolling faster, more of them taking ladders down. From level 2 some barrels are
  blue: quicker, and they nearly always take a ladder down toward you. From level 3 the ape
  sometimes throws two in a row.
- On a model 40 or more wide there are two ladders between girders, otherwise one.
- Options: *Lives*, *Barrels Every* (seconds between barrels on level 1, default 6),
  *Barrels Sooner* (how much sooner each level, default 0.6) and *Fastest Barrels* (the
  quickest it gets, default 3.2).

## Star Defenders

Hold off a marching alien formation from behind four shields, after the classic invaders.

- Left/Right move the cannon, A fires (hold it to keep firing; Up fires too, for controllers
  without buttons), and B fires a laser straight up that clears a whole column. You get one
  laser charge a wave, and can catch more. Magenta dots top left are laser charges, white dots
  top right spare lives.
- Each wave lays the formation out differently: a block, a V, a diamond, a checkerboard,
  two squads, a pyramid, then the fortress, and round again. The formation steps faster as it
  thins out, drops a row at each edge, and marches and bombs faster each wave. If the aliens
  reach the bottom the game is over.
- From wave 3 aliens peel off and swoop at the cannon, dropping a bomb on the way, then fly
  back to their place; they're worth double while diving. From wave 4 the top row is armoured
  (silver; two hits), from wave 8 the top two rows, and in the fortress every row but the
  bottom.
- The saucer that crosses the top row (50 to 300) always drops a capsule, and so do a quarter
  of the divers and a few other aliens. Catch it with the cannon. Your shots and the cannon
  take the weapon's colour, and a bar on the top row shows the time left (12 seconds):
  **Double** (cyan, two shots side by side), **Rapid** (yellow), **Pierce** (white, shots carry on
  through aliens), plus **Shields** (rebuilds them), **Laser** (another charge) and, rarely,
  **Life**.
- Shots from either side wear holes in the shields, and the aliens flatten them as they march
  through.
- Options: *Lives* and *Start at wave*.

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
  each row. A press drops it on the stack (whichever control starts the game plays it), and whatever hangs over the edge falls off and
  the next row is that much narrower. Reach the dotted goal line to clear the level; each
  level is a new tower four rows taller (11, 15, 19... on a 25-row model) that starts faster
  and tops out faster. Once the tower passes the lower third the view follows it up.
  Scoring, all times the level: 10 a row, 10 more for a perfect drop, and on reaching the
  top 25 for every block still in the last row. Rows left and the score show in the top
  corners.
- **Stacker race:** pressing the other control at the start of a level (on its LEVEL title, or
  before the first drop) splits the wall into two towers: the directions drop on the left, A or
  B on the right. The first to the top takes the level. A missed row stops that tower, and
  its height is the one to beat: the other side takes the level by building past it (the
  counter and goal line show how far), and if they miss first the taller tower takes it. The
  first to two levels wins the match.
- **Flap:** fly through the gaps in the pipes, which narrow and speed up as they go by. Up
  flaps the yellow bird and A or B a blue one: whoever flaps first starts, and the other bird
  joins whenever someone presses its control, arriving in the middle and blinking while it
  can't be hit. Each keeps its own score (yellow top right, blue top left), and the game is
  over when every bird in play has crashed; with two, GAME OVER shows both scores.
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
- Clear the sector and a black hole opens and starts pulling you in, harder every second;
  through it is the next sector, with more and faster asteroids and stronger gravity.
- Options: *Asteroids* at the start (0 fits the model: 3 on 60x25, 2 on a 30x25 half),
  *Gravity* (%), and *Lives*.

It needs the Released events as well as the Pressed ones, which joysticks and
`tools/arcade-keyboard.html` both send.

## Sound

Games can play sound effects: short WAV clips, mixed together and played on a USB sound card
plugged into the player, and/or streamed over the network to a computer.

- Turn it on under **Sound** on the Arcade Games page. *Speaker* picks the first USB sound card
  (it can be plugged in later), or none to stream only; *Volume* sets the level.
- **Stream to** sends the same sound to a computer, for testing without a speaker on the
  player: the Matrix Viewer for macOS plays it (Settings › Inputs › Game Sound, same port,
  5570 by default). The stream is 10 ms blocks of 44.1 kHz mono 16-bit audio over UDP, sent only
  while something is playing; the format is described in `src/ArcadeSound.h`.
- Every game except Breakout comes with its own retro sound effects, made with the synthesizer
  in `tools/sfx/`; [SOUNDS.md](SOUNDS.md) lists each game's sounds and when they play. To use your
  own, put files on the player in `media/plugindata/fpp-arcade/sounds/<game>/`, where `<game>` is
  the game's name in lower case without spaces (`breakout`, `mazechase`): a file there replaces
  the bundled sound of the same name. Use 16-bit PCM WAV, ideally mono at 44.1 kHz with no
  silence at the end. A sound with no file is simply silent. Saving the Arcade Games page
  reloads them.
- Every game plays `GameSelected` when its title is shown. Pausing with Start stops the game's
  sounds and plays `Pause`; leaving the menu plays `Unpause` (both in `sounds/arcade/`, shared by
  every game). **Breakout**, whose sounds you supply, also plays:

  | File | When |
  |---|---|
  | `StartLevel`, `LastLevel` | a level starts, and again for the new ball after each lost life (`LastLevel` on the final level: the snowman, or level 15 without him) |
  | `BounceFromPaddle`, `StickToPaddle` | the ball hits the paddle or is launched from it; it sticks, with Sticky |
  | `HitMetalBrick` | a silver or gold brick is hit but holds (it was `BreakMetalicBrick`, which still plays if that's the file there) |
  | `BreakRegularBrick` | a brick breaks, silver ones included |
  | `HitEnemy` | the snowman is hit (it was `HitNoBreak`, likewise) |
  | `BossThrow`, `BossDefeated` | the snowman throws a snowball, is beaten (optional: without them, `FireLaser` and `BreakRegularBrick` play) |
  | `FireLaser` | a laser shot |
  | `GrowPaddle`, `ExtraLife`, `Shrill`, `Coin` | catching Expand, Extra Life, the power ball (Break), any other power-up |
  | `LevelPortal` | the paddle goes through the portal |
  | `LoseLife`, `LoseGame`, `WonGame` | a life lost, game over, all levels cleared |

The sound card should be one FPP itself isn't using for show audio (leave FPP's own audio
output on the player set to none or the Dummy device).

**Controller speakers:** an ESP32-S3 with an amp and speaker inside each controller, on Wi-Fi,
playing just its own model's sounds. It keeps its own copy of the sound files (fetched from
here) and is sent an event per sound rather than audio, so sounds land instantly and a busy
network costs at most a sound. Parts, wiring, firmware and set-up are in
[speaker/README.md](speaker/README.md); connected speakers are listed under Sound on the Arcade
Games page.

## Commands

The plugin registers three FPP Commands:

- **FPP Arcade Select Game** — start a game on a given overlay model.
- **FPP Arcade Button** — deliver a button press/release (Up, Down, Left, Right, Fire, Select, Start).
- **FPP Arcade Axis** — deliver an analog axis position.

Wire these to GPIO inputs, the joystick page, or any other FPP event source to provide controls.

## License

GPLv2 — see [LICENSE](LICENSE).
