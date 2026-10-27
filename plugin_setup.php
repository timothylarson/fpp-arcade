<?
// The game list lives in config/plugin.fpp-arcade.json as {"games": [...]},
// each {enabled, game, model, overlay, options: [{name, value}]}. The plugin
// watches that file, so saving here applies without restarting FPP. Games
// are numbered per model in file order, which is what "FPP Arcade Select
// Game" counts, so the page groups them by model and keeps that order.
$cfgFile = $settings['configDirectory'] . "/plugin.fpp-arcade.json";
$arcadeConfig = ["games" => []];
if (file_exists($cfgFile)) {
    $saved = json_decode(file_get_contents($cfgFile), true);
    if (is_array($saved) && isset($saved["games"]) && is_array($saved["games"])) {
        $arcadeConfig = $saved;
    }
}
$pluginName = basename(dirname(__FILE__));
?>

<style>
#arcadePage { max-width: 980px; }
#arcadePage .arcadeIntro { margin: 0 0 12px 0; }
#arcadePage .arcadeStatus { padding: 8px 12px; margin: 8px 0 12px 0; border-left: 5px solid #888; background: rgba(128,128,128,0.12); }
#arcadePage .arcadeStatus div { margin: 2px 0; }
#arcadePage .arcadeStatus b { display: inline-block; min-width: 190px; }
#arcadePage .arcadeError { color: #d9534f; margin-left: 10px; }
#arcadePage .arcadeDirty { color: #d98e04; margin-left: 10px; display: none; }
#arcadePage h3 { margin: 26px 0 8px 0; font-size: 1.15em; }
#arcadePage h3 .count { opacity: 0.6; font-weight: normal; font-size: 0.85em; margin-left: 8px; }
#arcadePage .modelHint { opacity: 0.7; font-size: 0.88em; margin: -4px 0 8px 0; }

/* One card per game. */
#arcadePage .game { border: 1px solid rgba(128,128,128,0.45); border-radius: 6px; margin: 0 0 10px 0; }
#arcadePage .game.off { opacity: 0.55; }
#arcadePage .gameHead { display: flex; align-items: center; gap: 10px; padding: 8px 10px; flex-wrap: wrap;
    background: rgba(128,128,128,0.12); border-radius: 6px 6px 0 0; }
#arcadePage .gameNum { font-weight: bold; font-size: 1.1em; min-width: 26px; text-align: center;
    border-radius: 4px; background: rgba(128,128,128,0.3); padding: 1px 4px; }
#arcadePage .gameName { font-weight: bold; font-size: 1.05em; }
#arcadePage .gameHead .spacer { flex: 1 1 auto; }
#arcadePage .gameHead label { margin: 0; font-weight: normal; white-space: nowrap; }
#arcadePage .gameHead .buttons { min-width: 34px; margin: 0; }
#arcadePage .gameBody { padding: 4px 12px 10px 12px; }
#arcadePage .gameAbout { opacity: 0.8; font-size: 0.92em; margin: 6px 0 4px 0; }
#arcadePage .gameControls { font-size: 0.88em; margin: 0 0 6px 0; }
#arcadePage .gameControls span { display: inline-block; border: 1px solid rgba(128,128,128,0.6); border-radius: 3px;
    padding: 0 5px; margin: 1px 2px; font-family: monospace; }
#arcadePage table.opts { border-collapse: collapse; width: 100%; table-layout: fixed; }
#arcadePage table.opts td { padding: 5px 0; vertical-align: top; }
#arcadePage table.opts td:first-child { width: 210px; padding-right: 16px; padding-top: 8px; }
#arcadePage table.opts input[type=number] { width: 90px; }
#arcadePage table.opts input[type=text] { width: 100%; max-width: 420px; box-sizing: border-box; }
#arcadePage .hint { opacity: 0.7; font-size: 0.88em; margin-top: 3px; max-width: 560px; }
#arcadePage .noOpts { opacity: 0.6; font-size: 0.9em; margin: 6px 0; }
#arcadePage .addRow { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; margin-top: 8px; }
#arcadePage details { margin-top: 26px; }
#arcadePage summary { cursor: pointer; font-weight: bold; font-size: 1.15em; }
/* Phones: labels above controls, and the card header wraps. */
@media (max-width: 640px) {
    #arcadePage table.opts, #arcadePage table.opts tbody, #arcadePage table.opts tr, #arcadePage table.opts td { display: block; width: auto; }
    #arcadePage table.opts td:first-child { width: auto; padding: 8px 0 2px 0; font-weight: 600; }
    #arcadePage table.opts td { padding: 2px 0 6px 0; }
    #arcadePage .arcadeStatus b { display: block; }
}
</style>

<div id="arcadePage" class="settings">
<p class="arcadeIntro">Games played on Pixel Overlay Models. Each model has its own numbered list of games: the
Select button steps through them, and the <i>FPP Arcade Select Game</i> command picks one by number.
Buttons and joysticks are set up on <a href="plugin.php?plugin=<?= $pluginName ?>&page=joysticks.php">FPP Arcade Joysticks</a>.
Saving applies straight away, without restarting FPP, and stops any game being played.</p>

<div id="arcadeStatus" class="arcadeStatus">Checking...</div>

<div>
    <input type="button" value="Save" class="buttons genericButton" onclick="arcadeSave();">
    <span id="arcadeDirty" class="arcadeDirty">Unsaved changes</span>
    <span id="arcadeError" class="arcadeError"></span>
</div>

<h3>Sound</h3>
<table class="opts" id="arcadeSound">
<tr><td>Sound effects</td><td><input type="checkbox" id="sndEnabled">
    <div class="hint">Plays each game's sound files from <code>media/plugindata/fpp-arcade/sounds/&lt;game&gt;/</code>
    (the game's name in lower case, no spaces: <code>breakout</code>, <code>mazechase</code>). A game with no files there is silent.</div></td></tr>
<tr><td>Speaker</td><td><select id="sndDevice">
        <option value="auto">USB sound card (the first one plugged in)</option>
        <option value="none">None - stream only</option>
    </select>
    <div class="hint">A USB sound card can be plugged in at any time; it's picked up within a few seconds.</div></td></tr>
<tr><td>Volume (%)</td><td><input type="number" id="sndVolume" min="0" max="100" step="5"></td></tr>
<tr><td>Stream to</td><td><input type="text" id="sndHost" placeholder="computer's IP address" style="width: 200px">
    port <input type="number" id="sndPort" min="1" max="65535" style="width: 90px">
    <div class="hint">Sends the game sounds over the network too, for the Matrix Viewer on a Mac to play (turn on Game sound in its
    settings, same port). Leave blank when the speaker is on the player.</div></td></tr>
<tr><td>Controller speakers</td><td><div id="sndSpeakers">Looking...</div>
    <div class="hint">ESP32 speakers built into the controllers (see <code>speaker/README.md</code> in the plugin). Each plays the sounds of
    the model it's set to, from its own copy of the files, which it fetches from here.</div></td></tr>
</table>

<div id="arcadeModels"></div>

<h3>Add a game</h3>
<div class="addRow">
    <select id="arcadeAddGame"></select>
    on
    <select id="arcadeAddModel"></select>
    <input type="button" value="Add" class="buttons genericButton" onclick="arcadeAdd();">
</div>
<div class="hint">It goes at the end of that model's list. Click Save to keep it.</div>
</div>

<script>
var arcadeConfig = <?= json_encode($arcadeConfig) ?>;
var ARCADE_SOUND_DEFAULTS = { enabled: false, device: 'auto', volume: 80, streamHost: '', streamPort: 5570 };
arcadeConfig.sound = $.extend({}, ARCADE_SOUND_DEFAULTS, arcadeConfig.sound || {});
var arcadeModels = [];

// Every game, what it is, how it's played, and its options. Option names are
// what the game reads from the config, so they can't change (Tetris's
// "Colums" included).
var PS = { name: 'Pixel Scaling', label: 'Pixel scaling', type: 'number', def: 1, min: 1, max: 20,
           hint: 'Each game cell is this many LEDs square.' };
var PW = { name: 'Playfield Width', label: 'Playfield width', type: 'number', def: 0, min: 0, max: 500, hint: 'In game cells. 0 fills the model.' };
var PH = { name: 'Playfield Height', label: 'Playfield height', type: 'number', def: 0, min: 0, max: 500, hint: 'In game cells. 0 fills the model.' };
var LIVES = { name: 'Lives', label: 'Lives', type: 'number', def: 3, min: 1, max: 9 };
var YESNO = [['1', 'Yes'], ['0', 'No']];

var ARCADE_GAMES = {
    'Tetris': { about: 'Falling blocks.', controls: ['Left/Right move', 'Up or A rotate', 'B rotate back', 'Down faster'],
        options: [{ name: 'Rows', label: 'Rows', type: 'number', def: 20, min: 0, max: 50, hint: '0 fills the model.' },
                  { name: 'Colums', label: 'Columns', type: 'number', def: 11, min: 0, max: 30, hint: '0 fills the model.' }, PS] },
    'Pong': { about: 'Two paddles, one ball.', controls: ['Up/Down: player 1', 'Left/Right or B/A: player 2 (see Controls)'],
        options: [PS, { name: 'Controls', label: 'Controls', type: 'select', def: '1',
                        choices: [['1', 'Up/Down and Left/Right'], ['2', 'Up-Left/Down-Left and Up-Right/Down-Right'], ['3', 'Up/Left and Right/Down'],
                                  ['4', 'Up/Down and B/A (B up, A down)']],
                        hint: 'Which buttons move each paddle. Needs the Released events too.' }, PW, PH] },
    'Snake': { about: 'Eat, grow, and don\'t hit yourself. A second player joins with A or B for a duel.',
        controls: ['Directions steer', 'A/B: the second snake (B turns left, A right)'], options: [PS, PW, PH] },
    'Breakout': { about: 'Bat the ball into the bricks, with power-ups.', controls: ['Left/Right move', 'A/B release or fire'],
        options: [{ name: 'Brick Width', label: 'Brick width', type: 'number', def: 0, min: 0, max: 10, hint: 'LEDs. 0 picks one.' },
                  { name: 'Brick Spacing', label: 'Gap between bricks', type: 'select', def: '1', choices: YESNO, hint: 'Side by side.' },
                  { name: 'Row Gap', label: 'Gap between rows', type: 'select', def: '0', choices: YESNO,
                    hint: 'No stacks the rows edge to edge, Arkanoid style, with bricks twice as tall where they fit, under an open strip the ball can get into.' },
                  { name: 'Space Above', label: 'Space above bricks', type: 'number', def: 3, min: 0, max: 50,
                    hint: 'LEDs open above the top row when rows are stacked.' },
                  { name: 'Max Bricks', label: 'Most bricks per row', type: 'number', def: 0, min: 0, max: 100, hint: '0 uses each level\'s own.' },
                  { name: 'Paddle Width', label: 'Paddle width', type: 'number', def: 0, min: 0, max: 100, hint: 'LEDs. 0 picks one.' },
                  { name: 'Ball Speed', label: 'Ball speed', type: 'number', def: 0.8, min: 0.1, max: 5, step: 0.1, hint: '0.8 is normal (it was 1).' },
                  { name: 'Top Speed', label: 'Fastest ball', type: 'number', def: 1.3, min: 1, max: 5, step: 0.1,
                    hint: 'Each level is 10% faster (the paddle too), up to this many times level 1. 1.3 is reached at level 4.' },
                  { name: 'Paddle Speed', label: 'Paddle speed', type: 'number', def: 1.4, min: 0.3, max: 3, step: 0.05,
                    hint: 'How fast the paddle moves at level 1; it speeds up with the ball from there. 1 is the original speed.' },
                  { name: 'Capsule Chance', label: 'Capsule chance', type: 'number', def: 20, min: 0, max: 100,
                    hint: 'Percent of broken bricks that drop a power-up capsule, as in Arkanoid one at a time, none during triple ball.' },
                  { name: 'Boss Hits', label: 'Final boss hits', type: 'number', def: 16, min: 0, max: 99,
                    hint: 'After level 15, an evil snowman throwing snowballs: the hits it takes to beat him. 0 leaves him out.' },
                  { name: 'Playfield Width', label: 'Playfield width', type: 'number', def: 0, min: 0, max: 500, hint: 'In cells. 0 fits the bricks.' },
                  { name: 'Win Credits', label: 'Credits when won', type: 'select', def: '1', choices: YESNO,
                    hint: 'After the last level: YOU WON!, fireworks, a little story and rolling credits, as long as the WonGame music.' },
                  { name: 'Special Thanks', label: 'Special thanks', type: 'text', def: 'SANTA|RUDOLPH',
                    hint: 'Names under SPECIAL THANKS in the credits, separated by |. Blank leaves it out.' },
                  { name: 'Credits', label: 'Credits lines', type: 'text', def: 'THANKS FOR PLAYING|MERRY CHRISTMAS',
                    hint: 'Your lines at the end of the rolling credits, separated by |. Letters, numbers and : ! . , - \' only.' }] },
    'Stick Bridge': { about: 'Hold to grow a candy cane up from your gift box, let go to drop it across the gap, and walk over; land it on the red bow for a PERFECT. Two players split the wall.',
        controls: ['Hold any direction (or A/B) to grow, let go to drop', 'The other control before the first drop: two players'], options: [] },
    'Sky Jump': { about: 'Blast off and keep climbing on coins and blasts; a star smashes through clouds, bats and meteors. Fall off the bottom and it\'s over. Two players race side by side on the same course.',
        controls: ['Up launch, Left/Right steer', 'A/B: launch, B left, A right', 'The other control before launch (or just after): a race'],
        options: [{ name: 'Lives', label: 'Lives', type: 'number', def: 1, min: 1, max: 9, hint: 'Falls each climber can take; all but the last blast it back up from the bottom.' },
                  { name: 'Speed', label: 'Speed (%)', type: 'number', def: 100, min: 50, max: 200, hint: 'The pace of the whole game. Lower is easier to follow on the wall.' }] },
    'Reindeer Run': { about: 'An endless runner on a snowy night: jump the logs, presents, snowmen and creeks, duck the boughs, icicles and owls, and grab candy canes. A second reindeer joins in a lane of its own.',
        controls: ['Up jump (hold: higher), Down duck', 'A/B: a second reindeer (A jump, B duck)'],
        options: [{ name: 'Speed', label: 'Speed (%)', type: 'number', def: 100, min: 25, max: 300, hint: 'Slows or speeds up the whole game, jumps included, so it plays the same with more or less time to react.' },
                  { name: 'Speed Up', label: 'Seconds to top speed', type: 'number', def: 80, min: 10, max: 900, hint: 'How long the run takes to reach top speed, in six steps.' }] },
    'Road Race': { about: 'Drive down a snowy road past the traffic as it speeds up: presents for points, turbo stars, a snowplough and oncoming cars. A second player joins with the other control and the wall splits into two roads.',
        controls: ['Up/Down: change lane (alone, Right faster, Left brake)', 'B/A: the blue car (B up, A down)', 'The other control any time: a second car joins'],
        options: [LIVES] },
    'Snowball Fight': { about: 'Two elves on snowy hills take turns throwing snowballs against the wind; every snowball blows a crater in the snow. First to knock the other out wins. Alone, the computer plays the green elf.',
        controls: ['Red (D-pad): Left/Right aim, hold Up to charge, let go to throw', 'Green (A/B): hold B to swing the aim, hold A to charge, let go', 'A or B any time: take over the green elf'],
        options: [{ name: 'Hits', label: 'Hits to knock out', type: 'number', def: 3, min: 1, max: 9, hint: 'Health pips each elf starts with.' },
                  { name: 'Turn Time', label: 'Turn time (seconds)', type: 'number', def: 15, min: 5, max: 60, hint: 'Seconds to aim and throw before the turn passes.' },
                  { name: 'Wind', label: 'Strongest wind', type: 'number', def: 5, min: 0, max: 10, hint: '0 for no wind; 5 drifts a throw across the wall about 9 LEDs.' },
                  { name: 'Computer Skill', label: 'Computer skill', type: 'number', def: 3, min: 1, max: 5, hint: 'How well the computer aims when it plays the green elf.' }] },
    'Breakout Versus': { about: 'Two players, one controller: a paddle at the bottom and one at the top, a wall of bricks between, and a ball each. Get yours past the other paddle; first to Goals wins.',
        controls: ['Left/Right: bottom paddle', 'Up: launch, or fire its laser', 'B/A: top paddle (its ball and laser go by themselves)'],
        options: [{ name: 'Goals', label: 'Goals to win', type: 'number', def: 5, min: 1, max: 15 },
                  { name: 'Ball Speed', label: 'Ball speed', type: 'number', def: 1.0, min: 0.3, max: 3, step: 0.1, hint: '1 is normal; it quickens a little with every goal.' },
                  { name: 'Paddle Speed', label: 'Paddle speed', type: 'number', def: 1.5, min: 0.3, max: 3, step: 0.05,
                    hint: 'Both paddles, in the same units as Breakout\'s: 1 is 20 LEDs a second, 1.5 (30) as it has been.' }] },
    'Frogger': { about: 'Hop across the road and the river.', controls: ['Directions hop', 'A traffic faster', 'B traffic slower'],
        options: [PS, { name: 'Lanes', label: 'Lanes', type: 'number', def: 5, min: 1, max: 20 },
                  { name: 'River Speed', label: 'River speed', type: 'number', def: 1, min: 1, max: 10 },
                  { name: 'Road Speed', label: 'Road speed', type: 'number', def: 1, min: 1, max: 10 },
                  { name: 'Speed Variability', label: 'Speed variety (%)', type: 'number', def: 20, min: 0, max: 100, hint: 'How much lanes differ from each other.' },
                  PW, PH] },
    'Orb Pop': { about: 'Kinect: pop drifting orbs by touching them. Shows people as flickering light between rounds. Needs the FPP Kinect plugin on the same model.',
        controls: ['Stand in view to start', 'A start now', 'Start pause'],
        options: [{ name: 'Brightness', label: 'Brightness (%)', type: 'number', def: 50, min: 1, max: 100 },
                  { name: 'Palette', label: 'Palette', type: 'select', def: 'Frost', choices: [['Frost', 'Frost'], ['Christmas', 'Christmas']] },
                  { name: 'Round Length', label: 'Round length (seconds)', type: 'number', def: 45, min: 10, max: 600 },
                  { name: 'Start After', label: 'Start after (seconds in view)', type: 'number', def: 2, min: 0, max: 60 },
                  { name: 'Sensitivity', label: 'Sensitivity (% of LEDs)', type: 'number', def: 3, min: 1, max: 100, hint: 'How much of the matrix someone must cover to count.' },
                  { name: 'Start Automatically', label: 'Start automatically', type: 'select', def: '0', choices: YESNO,
                    hint: 'Yes: takes over the model whenever it shows the plain Kinect picture. No: the Kinect picture stays until a button starts Orb Pop.' }] },
    'Shape Fit': { about: 'Kinect: fill a pose outline with your body before the countdown. Five rounds. Needs the FPP Kinect plugin on the same model.',
        controls: ['Any button start', 'Start pause'],
        options: [{ name: 'Shape Size', label: 'Shape size (% of height)', type: 'number', def: 85, min: 30, max: 100 },
                  { name: 'Seconds', label: 'Seconds per pose', type: 'number', def: 6, min: 2, max: 20 }] },
    'Asteroid Orbit': { about: 'Fly through asteroids that pull on you. Land gently to reload; a black hole leads to the next sector.',
        controls: ['Directions thrust (hold)', 'A fire', 'B shockwave'],
        options: [{ name: 'Asteroids', label: 'Asteroids at the start', type: 'number', def: 0, min: 0, max: 12, hint: '0 fits the model.' },
                  { name: 'Gravity', label: 'Gravity (%)', type: 'number', def: 100, min: 0, max: 300 }, LIVES] },
    'Maze Chase': { about: 'Eat the dots, dodge the ghosts. A new maze every level.', controls: ['Directions turn'],
        options: [LIVES, { name: 'Narrow From', label: 'Narrow maze from level', type: 'number', def: 4, min: 0, max: 99,
                           hint: 'Levels before this have paths two LEDs wide; from this level on, one LED. 0 keeps them wide.' }] },
    'Barrel Climb': { about: 'Climb the girders to the top while barrels roll down.',
        controls: ['Left/Right walk (hold)', 'Up/Down climb', 'A jump'],
        options: [LIVES,
                  { name: 'Barrels Every', label: 'Barrels every (seconds)', type: 'number', def: 6, min: 1, max: 30, step: 0.1,
                    hint: 'How often the ape throws on level 1.' },
                  { name: 'Barrels Sooner', label: 'Sooner each level (seconds)', type: 'number', def: 0.6, min: 0, max: 10, step: 0.1,
                    hint: 'How much more often it throws on each new level. 0 keeps it the same.' },
                  { name: 'Fastest Barrels', label: 'Fastest (seconds)', type: 'number', def: 3.2, min: 0.8, max: 20, step: 0.1,
                    hint: 'The quickest it ever gets.' }] },
    'Star Defenders': { about: 'Hold off the marching aliens from behind the shields. A new formation each wave, armoured aliens from wave 4, divers from wave 3, and capsules to catch for new weapons.',
        controls: ['Left/Right move (hold)', 'A fire', 'B laser'],
        options: [LIVES, { name: 'Start Wave', label: 'Start at wave', type: 'number', def: 1, min: 1, max: 99,
                           hint: 'Higher starts harder: later formations, armour and divers.' }] },
    'Stacker': { about: 'Drop the sliding row onto the stack; the overhang falls off. Two players race side by side.',
        controls: ['Directions or A/B drop', 'The other control at a level start: a race'], options: [] },
    'Flap': { about: 'Flap through the gaps in the pipes, one bird or two.', controls: ['Up: the yellow bird', 'A/B: the blue bird'], options: [] },
    'Light Memory': { about: 'Repeat the growing sequence of lit triangles.', controls: ['Directions, one per triangle'], options: [] }
};

// The end show (fireworks, the score, a little story and credits) every game
// but Breakout has; Breakout has its credits. Each game's Good Game mark: its
// default and what it measures.
var ENDSHOW_GOOD = {
    'Tetris': [5, 'Level reached'], 'Pong': [3, 'Points the winner wins by'], 'Snake': [20, 'Length reached'],
    'Frogger': [4, 'Level reached'], 'Maze Chase': [3, 'Level reached'], 'Asteroid Orbit': [4, 'Sector reached'],
    'Barrel Climb': [3, 'Level reached'], 'Star Defenders': [5, 'Wave reached'], 'Stacker': [3, 'Level reached'],
    'Flap': [15, 'Pipes passed'], 'Light Memory': [10, 'Rounds repeated'], 'Orb Pop': [50, 'Score in a round'],
    'Shape Fit': [300, 'Total score'], 'Breakout Versus': [3, 'Goals the winner wins by'],
    'Stick Bridge': [10, 'Score (bridges, plus a point per perfect landing)'], 'Sky Jump': [600, 'Score'],
    'Reindeer Run': [600, 'Score'], 'Road Race': [2000, 'Metres driven'],
    'Snowball Fight': [2, 'Health left', 'Two players: the winner needs this much health left. Beating the computer always counts.']
};
Object.keys(ENDSHOW_GOOD).forEach(function (g) {
    var d = ENDSHOW_GOOD[g];
    ARCADE_GAMES[g].options = ARCADE_GAMES[g].options.concat([
        { name: 'End Show', label: 'End show', type: 'select', def: '1',
          choices: [['1', 'After a good game'], ['2', 'After every game'], ['0', 'Never']],
          hint: 'About 20 seconds of fireworks, the score, a little story and credits. A new best score always counts as good. Any button skips it.' },
        { name: 'Good Game', label: 'Good game', type: 'number', def: d[0], min: 0, max: 99999,
          hint: d[2] || (d[1] + ' that counts as a good game.') }]);
});

// The running effect's name, as FPP reports it, in words.
var ARCADE_EFFECTS = {
    KinectBlit: 'showing the Kinect picture', ArcadeTitle: 'showing a game title', Clear: 'clearing',
    OrbPop: 'playing Orb Pop', ShapeFit: 'playing Shape Fit', AsteroidOrbit: 'playing Asteroid Orbit',
    MazeChase: 'playing Maze Chase', BarrelClimb: 'playing Barrel Climb', StarDefenders: 'playing Star Defenders',
    LightMemory: 'playing Light Memory', BreakoutVersus: 'playing Breakout Versus', StickBridge: 'playing Stick Bridge',
    SkyJump: 'playing Sky Jump', ReindeerRun: 'playing Reindeer Run', RoadRace: 'playing Road Race',
    SnowballFight: 'playing Snowball Fight'
};

function arcadeEsc(s) { return $('<div>').text(String(s)).html(); }
function arcadeOpt(g, name) {
    var o = (g.options || []).filter(function (x) { return x.name === name; });
    return o.length ? o[0].value : undefined;
}

// Every game gets every option its form shows, filled from the saved value or
// the default. Options the form doesn't know are kept as they were.
function arcadeNormalise(g) {
    var def = ARCADE_GAMES[g.game];
    g.options = g.options || [];
    if (!def) {
        return g;
    }
    def.options.forEach(function (o) {
        if (arcadeOpt(g, o.name) === undefined) {
            g.options.push({ name: o.name, value: String(o.def) });
        }
    });
    if (!g.overlay) g.overlay = 'Overwrite';
    return g;
}

function arcadeSetDirty(d) { $('#arcadeDirty').toggle(d); }

function arcadeShowSound() {
    var s = arcadeConfig.sound;
    $('#sndEnabled').prop('checked', !!s.enabled);
    $('#sndDevice').val(s.device);
    $('#sndVolume').val(s.volume);
    $('#sndHost').val(s.streamHost);
    $('#sndPort').val(s.streamPort);
    $('#arcadeSound tr').not(':first').toggle(!!s.enabled);
}
$('#arcadeSound').on('change input', 'input, select', function () {
    var s = arcadeConfig.sound;
    s.enabled = $('#sndEnabled').is(':checked');
    s.device = $('#sndDevice').val();
    s.volume = parseInt($('#sndVolume').val(), 10) || 0;
    s.streamHost = $.trim($('#sndHost').val());
    s.streamPort = parseInt($('#sndPort').val(), 10) || 5570;
    $('#arcadeSound tr').not(':first').toggle(s.enabled);
    arcadeSetDirty(true);
});

function arcadeModelNames() {
    var names = arcadeModels.slice();
    arcadeConfig.games.forEach(function (g) {
        if (names.indexOf(g.model) < 0) names.push(g.model);   // a model that no longer exists still shows
    });
    return names;
}

function arcadeRender() {
    var html = '';
    arcadeModelNames().forEach(function (model) {
        var list = [];
        arcadeConfig.games.forEach(function (g, i) { if (g.model === model) list.push(i); });
        if (!list.length) return;
        var missing = arcadeModels.indexOf(model) < 0;
        html += '<h3>' + arcadeEsc(model) + '<span class="count">' + list.length + (list.length === 1 ? ' game' : ' games') + '</span></h3>';
        if (missing) html += '<div class="modelHint" style="color:#d9534f">This model isn\'t in FPP\'s overlay models any more.</div>';
        var n = 0;
        list.forEach(function (i, k) {
            var g = arcadeConfig.games[i];
            var def = ARCADE_GAMES[g.game];
            if (g.enabled) n++;
            html += '<div class="game' + (g.enabled ? '' : ' off') + '" data-i="' + i + '">';
            html += '<div class="gameHead">';
            html += '<span class="gameNum" title="Its number for FPP Arcade Select Game">' + (g.enabled ? n : '–') + '</span>';
            html += '<span class="gameName">' + arcadeEsc(g.game) + '</span>';
            html += '<label><input type="checkbox" class="gEnabled"' + (g.enabled ? ' checked' : '') + '> Enabled</label>';
            html += '<label>Overlay <select class="gOverlay">' +
                    ['Overwrite', 'Transparent'].map(function (o) {
                        return '<option' + (g.overlay === o ? ' selected' : '') + '>' + o + '</option>';
                    }).join('') + '</select></label>';
            html += '<span class="spacer"></span>';
            html += '<button type="button" class="buttons gPlay" title="Save first, then start it on the wall"><i class="fas fa-play"></i> Play</button>';
            html += '<button type="button" class="buttons gUp" title="Move up"' + (k === 0 ? ' disabled' : '') + '><i class="fas fa-chevron-up"></i></button>';
            html += '<button type="button" class="buttons gDown" title="Move down"' + (k === list.length - 1 ? ' disabled' : '') + '><i class="fas fa-chevron-down"></i></button>';
            html += '<button type="button" class="buttons gDelete" title="Remove"><i class="fas fa-trash"></i></button>';
            html += '</div><div class="gameBody">';
            if (def) {
                html += '<div class="gameAbout">' + arcadeEsc(def.about) + '</div>';
                html += '<div class="gameControls">' + def.controls.map(function (c) { return '<span>' + arcadeEsc(c) + '</span>'; }).join('') + '</div>';
                if (def.options.length) {
                    html += '<table class="opts">';
                    def.options.forEach(function (o) {
                        var v = arcadeOpt(g, o.name);
                        html += '<tr><td>' + arcadeEsc(o.label) + '</td><td>';
                        if (o.type === 'select') {
                            html += '<select class="gOpt" data-name="' + arcadeEsc(o.name) + '">' + o.choices.map(function (c) {
                                return '<option value="' + arcadeEsc(c[0]) + '"' + (String(v) === String(c[0]) ? ' selected' : '') + '>' + arcadeEsc(c[1]) + '</option>';
                            }).join('') + '</select>';
                        } else if (o.type === 'text') {
                            html += '<input type="text" class="gOpt" data-name="' + arcadeEsc(o.name) + '" value="' +
                                    arcadeEsc(v).replace(/"/g, '&quot;') + '">';
                        } else {
                            html += '<input type="number" class="gOpt" data-name="' + arcadeEsc(o.name) + '" value="' + arcadeEsc(v) + '"' +
                                    ' min="' + o.min + '" max="' + o.max + '" step="' + (o.step || 1) + '">';
                        }
                        if (o.hint) html += '<div class="hint">' + arcadeEsc(o.hint) + '</div>';
                        html += '</td></tr>';
                    });
                    html += '</table>';
                } else {
                    html += '<div class="noOpts">No settings.</div>';
                }
            } else {
                html += '<div class="noOpts">This version of the plugin doesn\'t know this game; it is kept as it is.</div>';
            }
            html += '</div></div>';
        });
    });
    if (!arcadeConfig.games.length) {
        html = '<h3>No games yet</h3><div class="hint">Add one below.</div>';
    }
    $('#arcadeModels').html(html);
}

// The cards' row number in arcadeConfig.games.
function arcadeIndex(el) { return parseInt($(el).closest('.game').data('i'), 10); }

// Move a game past the next one on the same model.
function arcadeMove(i, dir) {
    var games = arcadeConfig.games, model = games[i].model, j = i + dir;
    while (j >= 0 && j < games.length && games[j].model !== model) j += dir;
    if (j < 0 || j >= games.length) return;
    var t = games[i]; games[i] = games[j]; games[j] = t;
    arcadeSetDirty(true);
    arcadeRender();
}

$('#arcadeModels').on('change', '.gEnabled', function () {
    arcadeConfig.games[arcadeIndex(this)].enabled = this.checked;
    arcadeSetDirty(true);
    arcadeRender();
}).on('change', '.gOverlay', function () {
    arcadeConfig.games[arcadeIndex(this)].overlay = $(this).val();
    arcadeSetDirty(true);
}).on('change input', '.gOpt', function () {
    var g = arcadeConfig.games[arcadeIndex(this)], name = $(this).data('name'), v = String($(this).val());
    var o = g.options.filter(function (x) { return x.name === name; });
    if (o.length) o[0].value = v; else g.options.push({ name: name, value: v });
    arcadeSetDirty(true);
}).on('click', '.gUp', function () { arcadeMove(arcadeIndex(this), -1); })
  .on('click', '.gDown', function () { arcadeMove(arcadeIndex(this), 1); })
  .on('click', '.gDelete', function () {
    var i = arcadeIndex(this);
    if (!confirm('Remove ' + arcadeConfig.games[i].game + ' from ' + arcadeConfig.games[i].model + '?')) return;
    arcadeConfig.games.splice(i, 1);
    arcadeSetDirty(true);
    arcadeRender();
}).on('click', '.gPlay', function () {
    if ($('#arcadeDirty').is(':visible')) {
        $('#arcadeError').text('Save first, so the wall has your changes.');
        return;
    }
    var g = arcadeConfig.games[arcadeIndex(this)];
    var n = parseInt($(this).closest('.gameHead').find('.gameNum').text(), 10);
    if (!g.enabled || !n) return;
    // Pick it, then press Start - the same as a player would.
    arcadeCommand('FPP Arcade Select Game', [String(n), g.model]).then(function () {
        return arcadeCommand('FPP Arcade Button', ['Start - Pressed', g.model]);
    }).always(function () {
        setTimeout(arcadeRefresh, 500);
        setTimeout(arcadeRefresh, 1500);
    });
});

function arcadeCommand(command, args) {
    return $.get('api/command/' + [command].concat(args).map(encodeURIComponent).join('/'))
        .fail(function () { $('#arcadeError').text('FPP did not respond to ' + command + '.'); });
}

function arcadeValidate() {
    var bad = '';
    arcadeConfig.games.forEach(function (g) {
        var def = ARCADE_GAMES[g.game];
        if (!def) return;
        def.options.forEach(function (o) {
            if (o.type !== 'number' || bad) return;
            var v = parseFloat(arcadeOpt(g, o.name));
            if (isNaN(v) || v < o.min || v > o.max) {
                bad = g.game + ' on ' + g.model + ': ' + o.label + ' must be between ' + o.min + ' and ' + o.max + '.';
            }
        });
    });
    return bad;
}

function arcadeSave() {
    var bad = arcadeValidate();
    $('#arcadeError').text(bad);
    if (bad) return;
    $.ajax({
        type: 'POST', url: 'api/configfile/plugin.fpp-arcade.json', contentType: 'application/json',
        data: JSON.stringify(arcadeConfig, null, 2), processData: false
    }).done(function () {
        arcadeSetDirty(false);
        $.jGrowl('Arcade games saved and loaded', { themeState: 'success' });
        setTimeout(arcadeRefresh, 600);
    }).fail(function () {
        $('#arcadeError').text('Could not save the settings.');
    });
}

function arcadeAdd() {
    var game = $('#arcadeAddGame').val(), model = $('#arcadeAddModel').val();
    if (!game || !model) return;
    arcadeConfig.games.push(arcadeNormalise({ enabled: true, game: game, model: model,
        overlay: game === 'Orb Pop' ? 'Transparent' : 'Overwrite', options: [] }));
    arcadeSetDirty(true);
    arcadeRender();
    $('html, body').animate({ scrollTop: $('#arcadeModels .game').last().offset().top - 80 }, 300);
}

function arcadeRefreshSpeakers() {
    $.get('api/plugin-apis/arcade/speakers').done(function (list) {
        if (!list || !list.length) {
            $('#sndSpeakers').text('None heard from.');
            return;
        }
        $('#sndSpeakers').html(list.map(function (s) {
            return '<div><a href="http://' + arcadeEsc(s.ip) + '/" target="_blank">' + arcadeEsc(s.name || s.ip) + '</a> - ' +
                arcadeEsc(s.model) + ', volume ' + s.volume + '%, ' + arcadeEsc(s.ip) + ', firmware ' + arcadeEsc(s.firmware) + '</div>';
        }).join(''));
    }).fail(function () { $('#sndSpeakers').text('Not available.'); });
}

// What each model is doing right now.
function arcadeRefresh() {
    arcadeRefreshSpeakers();
    var names = arcadeModelNames().filter(function (m) {
        return arcadeConfig.games.some(function (g) { return g.model === m; });
    });
    if (!names.length) {
        $('#arcadeStatus').text('No games are set up yet.');
        return;
    }
    $.when.apply($, names.map(function (m) {
        return $.get('api/overlays/model/' + encodeURIComponent(m)).then(function (d) { return d; }, function () { return $.Deferred().resolve(null); });
    })).done(function () {
        var results = names.length === 1 ? [arguments[0]] : Array.prototype.slice.call(arguments);
        var html = '';
        names.forEach(function (m, k) {
            var d = results[k], what;
            if (!d || typeof d !== 'object') what = 'not found';
            else if (d.effectRunning && d.effectName) what = ARCADE_EFFECTS[d.effectName] || ('playing ' + d.effectName);
            else what = d.isActive ? 'on, nothing running' : 'off';
            html += '<div><b>' + arcadeEsc(m) + '</b>' + arcadeEsc(what) + '</div>';
        });
        $('#arcadeStatus').html(html);
    });
}

$.get('api/models').done(function (data) {
    arcadeModels = (data || []).map(function (m) { return m.Name; });
}).always(function () {
    arcadeConfig.games.forEach(arcadeNormalise);
    $('#arcadeAddGame').html(Object.keys(ARCADE_GAMES).map(function (g) { return '<option>' + arcadeEsc(g) + '</option>'; }).join(''));
    $('#arcadeAddModel').html(arcadeModels.map(function (m) { return '<option>' + arcadeEsc(m) + '</option>'; }).join(''));
    arcadeRender();
    arcadeShowSound();
    arcadeRefresh();
    setInterval(arcadeRefresh, 3000);
});

window.addEventListener('beforeunload', function (e) {
    if ($('#arcadeDirty').is(':visible')) { e.preventDefault(); e.returnValue = ''; }
});
</script>
