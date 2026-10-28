# Game sounds

Every game plays sound effects by name: `sounds/<game>/<Name>.wav`, where `<game>` is the game's
name in lower case without spaces. The plugin comes with a set for every game except Breakout,
made with the little synthesizer in `tools/sfx/` (the recipes are `tools/sfx/games/<game>.py`;
`python3 tools/sfx/make_sounds.py [game ...]` renders them). A file with the same name in
`media/plugindata/fpp-arcade/sounds/<game>/` on the player is played instead, so any sound can
be replaced, and Breakout's come from there. A name with no file is silent.

All of them play `GameSelected` when the game's title is shown, and `EndShow` when an end show starts (README.md, End shows) - or, at Halloween or on an everyday show, `EndShowHalloween` or `EndShowEveryday` when the game has one (every game but Breakout does; the tables list only `EndShow`). Pausing (Start) stops the game's sounds and plays `arcade/Pause`; leaving the menu, by Resume or Restart, plays `arcade/Unpause`. Those two are shared by every game, in `sounds/arcade/` (recipe `tools/sfx/games/arcade.py`). Sounds marked *(quiet)* are
played below full level by the game.

## Tetris
| Sound | When |
|---|---|
| StartGame | A new game or Restart |
| Move *(quiet)* | A sideways move that worked (at most every 70 ms) |
| Rotate | A rotation that worked |
| Lock | A piece lands without clearing lines |
| Single, Double, Triple, Tetris | 1-4 lines cleared |
| LevelUp | A clear that raises the level (instead of the line sound) |
| GameOver | No room for the next piece |
| EndShow | The end show after a good game |

## Pong
| Sound | When |
|---|---|
| StartGame | A new game or Restart |
| Paddle, Wall | The ball bounces off a paddle, or the top or bottom wall |
| Point | Either player scores |
| GameOver | Someone reaches 5 |
| EndShow | The end show after a clear win |

## Snake
| Sound | When |
|---|---|
| StartGame | A new game or Restart |
| Turn *(quiet)* | The direction changes |
| Eat, Milestone | Food eaten; every 10th bite plays Milestone instead |
| GameOver | Hitting a wall or itself |
| EndShow | The end show after a good game |

## Breakout
See the table under Sound in [README.md](README.md).

## Frogger
| Sound | When |
|---|---|
| Start | A new game or Restart |
| Hop, Bump | A hop; a hop into the edge, or A/B at the speed limit |
| SpeedUp, SpeedDown | A / B changes the traffic speed |
| Home, LevelClear | Reaching a home; the fifth home |
| Splash, Squash | A life lost in the water; under a car |
| GameOver | The last life lost |
| EndShow | The end show after a good game |

## Maze Chase
| Sound | When |
|---|---|
| Start | A new game or Restart (through READY) |
| WakaA, WakaB | Dots eaten, alternating |
| PowerPellet, Scared | A power pellet; the ghosts turn blue |
| EatGhost | A scared ghost caught |
| Death, LevelClear, GameOver | Caught; the last dot; the last life |
| EndShow | The end show after a good game |

## Asteroid Orbit
| Sound | When |
|---|---|
| SectorStart | Each sector intro |
| Fire, Empty | A shot; A with no ammo |
| Thrust *(quiet)* | A soft puff every 0.22 s while thrusting |
| BreakLarge, BreakMedium, BreakSmall | A rock breaks, by size |
| Land, TakeOff, Reload *(quiet)* | Landing on a rock; leaving it; each ammo reloaded on it |
| Bounce | Bumping a rock while flashing after a respawn |
| CrystalFuel, CrystalAmmo, CrystalPulse | Each kind of crystal |
| Shockwave | B |
| Crash | The ship crashes |
| HoleOpen, Warp | The black hole opens; going through it |
| GameOver | The last life lost |
| EndShow | The end show after a good game |

## Barrel Climb
| Sound | When |
|---|---|
| LevelStart | Each level's intro |
| Step *(quiet)*, Climb / Climb2 *(quiet)* | Walking (every other LED); each rung, alternating |
| Jump, Score | A jump; clearing a barrel (+100) |
| Throw, BarrelLand | The ape throws; a barrel lands on the girder below |
| Fall, Die | Walking or jumping off an edge; death |
| Rescue, GameOver | Reaching the top; the last life lost |
| EndShow | The end show after a good game |

## Star Defenders
| Sound | When |
|---|---|
| WaveStart | Each wave intro |
| March1-4 | One per formation step, cycling, so it speeds up with the formation |
| Shoot, Laser | A shot; B |
| ArmourHit, AlienKill, DiverKill | Armour knocked off; an alien killed; a diver killed |
| Saucer *(quiet)*, SaucerHit | While the saucer is on screen; shooting it |
| PowerDouble, PowerRapid, PowerPierce, PowerShields, PowerLaser, ExtraLife | Catching each capsule |
| ShieldHit *(quiet)*, BombShot | A bomb hits a shield; a shot hits a bomb |
| Death, WaveClear, GameOver | The cannon destroyed; the last alien; the last life |
| EndShow | The end show after a good game |

## Orb Pop
| Sound | When |
|---|---|
| Count, Go | 3, 2, 1 and GO (the first is the only sound in attract mode) |
| Pop1-Pop8 | An orb popped: quick pops climb a pentatonic scale |
| GoldPop | The gold orb |
| Tick *(quiet)* | Each of a round's last 3 seconds |
| RoundEnd, Score | The round ends; the score appears (not for 0) |
| EndShow | The end show after a good round |

## Shape Fit
| Sound | When |
|---|---|
| Pose | Each pose name |
| Tick *(quiet)*, TickUrgent | Each second; the last three |
| Snap | Time's up |
| Perfect, Great, Good, Ok, Miss | The verdict, with the same cut-offs as on screen |
| Total | The final total |
| EndShow | The end show after a good game |

## Stacker
| Sound | When |
|---|---|
| LevelStart | Each tower's intro |
| Drop1-Drop16 | Each drop, higher up a pentatonic scale as the tower nears its goal |
| Perfect, Chop, Miss | An exact drop; an overhang falls off; a row misses |
| LevelClear, GameOver | The tower complete; GAME OVER |
| EndShow | The end show after a good game |

## Flap
| Sound | When |
|---|---|
| Start, Flap *(quiet)* | The first flap; each later one |
| Score, Crash, GameOver | Passing a pipe; hitting a pipe or the ground; GAME OVER |
| EndShow | The end show after a good game |

## Light Memory
| Sound | When |
|---|---|
| Up, Right, Down, Left | Each triangle's note (A4, C#5, E5, A5), shown and pressed |
| Wrong, RoundClear, GameOver | A wrong press or timeout; a round repeated; GAME OVER |

| EndShow | The end show after a good game |

## Breakout Versus
| Sound | When |
|---|---|
| Start | A new game |
| Launch *(quiet)* | A ball leaves its paddle |
| Paddle *(quiet)*, Wall *(quiet)*, Brick *(quiet)* | A ball hits a paddle, a side, a brick |
| Laser *(quiet)*, PowerUp | A laser shot; a capsule caught |
| Goal, NewWall | A ball past a paddle; a new wall going up |
| GameOver, EndShow | The match won; the end show after a clear win |

## Stick Bridge
| Sound | When |
|---|---|
| Start, Join | The first press starts the game; the other control joins for two players |
| Grow1-Grow20 *(quiet)* | The stick grows one LED, a semitone higher each |
| Drop, Land, Perfect | Let go; its end lands on the next box; right on the bow |
| Step *(quiet)*, Cross, Fall | The elf walking; reaching the next box; stepping off a stick that missed |
| GameOver, EndShow | GAME OVER; the end show after a good game |

## Sky Jump
| Sound | When |
|---|---|
| Launch, Blast, Star | Blast-off (and a bounce off the snow); touching a blast; catching a star |
| Coin1-Coin8 *(quiet)* | Each coin; a quick run climbs the scale |
| StarOut, Smash, Hit | The star wearing off; smashing an enemy; knocked by one |
| Fall, Rescue, Higher | Off the bottom; a spare life blasting you back up; every 250 rows |
| Join, Count | A second player starts a race; its 3, 2, 1 |
| GameOver, EndShow | GAME OVER; the end show after a good game |

## Reindeer Run
| Sound | When |
|---|---|
| Start | The first press, and a second reindeer joining |
| Jump, Land *(quiet)*, Duck *(quiet)* | A jump; landing; starting to duck |
| Candy *(quiet)*, Owl *(quiet)*, Faster | A candy cane; an owl coming on; each step of speed |
| Crash, Splash, GameOver | Hitting something; falling in a creek; GAME OVER |
| EndShow | The end show after a good game |

## Road Race
| Sound | When |
|---|---|
| Start, Join | The 3-2-1-GO countdown (a car joining, or Restart); joining during the other's countdown |
| Engine1-Engine6 *(quiet)* | The engine hum, every 0.3 s, pitched by speed |
| Lane *(quiet)*, Bump *(quiet)*, Pass *(quiet)* | A lane change; trying to go past the kerb; overtaking |
| Horn *(quiet)*, Faster *(quiet)* | An oncoming car on its way; into a faster stretch |
| Pickup, Boost, Smash | A present; a turbo star; knocking a car away on turbo |
| Crash, Out | A life lost; one of two players out of lives |
| GameOver, EndShow | Everyone out; the end show after a good game |

## Snowball Fight
| Sound | When |
|---|---|
| Start, Turn, Join | A game starts; each turn; a second player takes over green |
| Aim *(quiet)*, Charge | Turning the aim; charging a throw |
| Throw, Splat, Hit, Miss | The throw; it blows a crater; it hits an elf; it flies off the side |
| Wind, Tick, TimeUp | A strong wind this turn; the last five seconds; time up |
| Land, KO, Giant | An elf dropping into a crater; knocked out; the giant-snowball cheat |
| Win, GameOver, EndShow | A person wins; the computer wins (or the game is abandoned); the end show |
