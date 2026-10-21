# Game sounds

Every game plays sound effects by name: `sounds/<game>/<Name>.wav`, where `<game>` is the game's
name in lower case without spaces. The plugin comes with a set for every game except Breakout,
made with the little synthesizer in `tools/sfx/` (the recipes are `tools/sfx/games/<game>.py`;
`python3 tools/sfx/make_sounds.py [game ...]` renders them). A file with the same name in
`media/plugindata/fpp-arcade/sounds/<game>/` on the player is played instead, so any sound can
be replaced, and Breakout's come from there. A name with no file is silent.

All of them play `GameSelected` when the game's title is shown. Sounds marked *(quiet)* are
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

## Pong
| Sound | When |
|---|---|
| StartGame | A new game or Restart |
| Paddle, Wall | The ball bounces off a paddle, or the top or bottom wall |
| Point | Either player scores |
| GameOver | Someone reaches 5 |

## Snake
| Sound | When |
|---|---|
| StartGame | A new game or Restart |
| Turn *(quiet)* | The direction changes |
| Eat, Milestone | Food eaten; every 10th bite plays Milestone instead |
| GameOver | Hitting a wall or itself |

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

## Maze Chase
| Sound | When |
|---|---|
| Start | A new game or Restart (through READY) |
| WakaA, WakaB | Dots eaten, alternating |
| PowerPellet, Scared | A power pellet; the ghosts turn blue |
| EatGhost | A scared ghost caught |
| Death, LevelClear, GameOver | Caught; the last dot; the last life |

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

## Barrel Climb
| Sound | When |
|---|---|
| LevelStart | Each level's intro |
| Step *(quiet)*, Climb / Climb2 *(quiet)* | Walking (every other LED); each rung, alternating |
| Jump, Score | A jump; clearing a barrel (+100) |
| Throw, BarrelLand | The ape throws; a barrel lands on the girder below |
| Fall, Die | Walking or jumping off an edge; death |
| Rescue, GameOver | Reaching the top; the last life lost |

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

## Orb Pop
| Sound | When |
|---|---|
| Count, Go | 3, 2, 1 and GO (the first is the only sound in attract mode) |
| Pop1-Pop8 | An orb popped: quick pops climb a pentatonic scale |
| GoldPop | The gold orb |
| Tick *(quiet)* | Each of a round's last 3 seconds |
| RoundEnd, Score | The round ends; the score appears (not for 0) |

## Shape Fit
| Sound | When |
|---|---|
| Pose | Each pose name |
| Tick *(quiet)*, TickUrgent | Each second; the last three |
| Snap | Time's up |
| Perfect, Great, Good, Ok, Miss | The verdict, with the same cut-offs as on screen |
| Total | The final total |

## Stacker
| Sound | When |
|---|---|
| LevelStart | Each tower's intro |
| Drop1-Drop16 | Each drop, higher up a pentatonic scale as the tower nears its goal |
| Perfect, Chop, Miss | An exact drop; an overhang falls off; a row misses |
| LevelClear, GameOver | The tower complete; GAME OVER |

## Flap
| Sound | When |
|---|---|
| Start, Flap *(quiet)* | The first flap; each later one |
| Score, Crash, GameOver | Passing a pipe; hitting a pipe or the ground; GAME OVER |

## Light Memory
| Sound | When |
|---|---|
| Up, Right, Down, Left | Each triangle's note (A4, C#5, E5, A5), shown and pressed |
| Wrong, RoundClear, GameOver | A wrong press or timeout; a round repeated; GAME OVER |
