<p>
The FPP Arcade plugin provides a few interactive games that can be played on a Pixel Overlay Model.  Each of the games does require some input "FPP Arcade Button" commands configured as described below.

<div class='fppTableWrapper fppTableWrapperAsTable'>
<div class='fppTableContents'>
<table class="fppTable"><thead><th>Game</th><th>Configuration</th></tr>
<tbody>
<tr><td>Tetris</td><td>Tetris only responds the FPP Arcade Button commands for the 4 directional "Pressed" events.   The "Up" event will rotate the block.
</td></tr>
<tr><td>Pong</td><td>Pong requires both the "Pressed" and "Released" events for all 4 directions.   Player one is controlled by the Up/Down events and Player two is controlled by the Left/Right events.</td></tr>
<tr><td>Snake</td>Snake only responds the FPP Arcade Button commands for the 4 directional "Pressed" events.</td></tr>
<tr><td>Maze Chase</td><td>Responds to the 4 directional "Pressed" events; the last one pressed is where you turn next.</td></tr>
<tr><td>Barrel Climb</td><td>Needs "Pressed" and "Released" for the 4 directions (walk and climb), and the A Button to jump.</td></tr>
<tr><td>Star Defenders</td><td>Needs "Pressed" and "Released" for Left/Right, and the A Button (fire, hold to repeat) and B Button (laser, once a wave). Up also fires.</td></tr>
<tr><td>Asteroid Orbit</td><td>Needs both "Pressed" and "Released" for the directions (diagonals work too) and the A Button: directions thrust while held, A fires (hold to keep firing), B sends a shockwave, Start pauses. Land gently on an asteroid to reload and refuel.</td></tr>
<tr><td>Orb Pop</td><td>Played by people in front of a Kinect; needs the FPP Kinect plugin running on the same model. Standing in view for "Start After" seconds, or the A button, starts a round: touch the orbs to pop them. Start pauses. With "Start Automatically" it takes over the model whenever the plain Kinect picture is showing.</td></tr>
</table>
</div>
</div>
