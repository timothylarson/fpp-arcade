# FPP Arcade Changelog

## Unreleased
- Added dedicated `A Button` and `B Button` controller bindings in the command list and web UI.
- Updated joystick defaults to map common gamepad buttons 0/1 to the new A/B bindings.
- In Tetris, `A Button` (or `Up`) now rotates clockwise while `B Button` rotates counter-clockwise.
- Breakout now uses gap-aware brick collisions that prevent the ball from slipping between bricks.
- Refactored Breakout to support multiple simultaneous balls and added new Sticky, Laser, Break, Expand, Slow, and Triple power-ups that randomly drop from destroyed bricks, fall toward the paddle, and apply temporary effects (press A/B to release stuck balls or fire lasers).
- Dim and center the Game Over / You Win overlays in Breakout for improved readability.
- Tetris lateral movement now repeats while holding left/right, and the Game Over screen (and Snake’s) is centered along with the score.
- Selecting games now shows a centered title card on the matrix so the active choice is visible before it starts.
