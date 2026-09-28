# How this Pong works

This note is for an automation tester, or for someone learning 2D graphics, who wants to see how a small C + Raylib program is structured: where it starts, who owns state, who paints pixels, and how a paddle hit becomes a new ball velocity.

You do not need to be an OpenGL expert. Raylib hides the context setup. The ideas are the usual ones: an entry point, one struct that is the whole match, a simulation step, a draw, and a timed loop that does those two in that order.

There is **no scene graph**, **no editor**, and **no sprite sheet**. A paddle is a `Rectangle`. The ball is a `Vector2` plus a radius. The picture on screen exists only because `draw_game` filled those pixels this frame.

Unlike the larger tributes, this one is a **single translation unit** (`src/main.c`) on purpose. The loop, the physics, and the drawing can be read from top to bottom. Splitting it can wait until a second screen exists.

## Mental model

```
main
  → InitWindow + InitAudioDevice
  → reset_match                     # scores 0, ball parked, serve clock armed
  → loop while the window is open
       update_game(dt)              # keys, paddles, serve clock, ball
       draw_game                    # clear, then primitives, then captions
  → unload tones, CloseAudioDevice, CloseWindow
```

| Layer | Where | Tester-friendly analogy |
|-------|--------|-------------------------|
| Entry | `main` | Opens the window and owns the `Game` on the stack |
| State | `Game` | The fixture. Every assertion about the match reads this struct |
| Simulation | `update_game` / `step_ball` | The only code that changes positions, scores, and flags |
| View | `draw_game` | Reads `Game`. Writes pixels. Never writes `Game` |
| Build | `CMakeLists.txt` | Finds Raylib, or downloads it. The Makefile just invokes that |

Raylib is **polled**, not a callback UI. `IsKeyDown` / `IsKeyPressed`, `update_game`, and `draw_game` run on the same thread. There is no worker and no "invoke later".

Screen space: origin at the **top-left**, **+x right**, **+y down**. A paddle's `y` getting larger moves it toward the bottom of the window. That is the opposite of a school graph.

---

## 1. Entry point and execution lifecycle

### Where `main` lives

The process entry point is `main` in `src/main.c`. It creates the window, the audio device, and one `Game` on the stack. It does not take flags. There is no config file.

```c
while (!WindowShouldClose())
{
    update_game(&game, GetFrameTime());
    draw_game(&game);
}
```

`WindowShouldClose` becomes true when the close button is pressed, or when Esc is pressed. Esc is Raylib's default exit key. There is no separate "back to a menu".

### Lifecycle, step by step

1. **The OS** starts `build/pong` (or `build\pong.exe`).
2. **`InitWindow`** opens a 960×540 window titled `Pong Tribute` and creates the OpenGL context. **`InitAudioDevice`** opens the sound device. **`SetTargetFPS(60)`** asks the loop to wake at 60Hz. **`SetRandomSeed`** seeds serve direction and the up/down coin flip from the clock.
3. **Paddle rectangles** are placed. The left one sits `PADDLE_MARGIN` pixels in from the left edge. The right one mirrors it. **`ai` starts true**: the right paddle is the computer, so a single player sees a rally immediately.
4. **`make_tone`** builds three short square waves (paddle, wall, score) and hands them to Raylib. **`UnloadWave`** frees the sample buffer; the `Sound` keeps its own copy.
5. **`reset_match`** zeros the scores, centres the paddles, and calls **`begin_serve`**. The ball is in the middle. `velocity` is `{0,0}`. `serveTimer` is `0.85` seconds. `serveDir` is `+1` or `-1` at random.
6. The process stays alive inside `while (!WindowShouldClose())`.
7. On the way out, tones are unloaded, then the audio device, then the window. `main` returns 0.

### Two ways to play

**One player** (the default):

```
window opens on READY
  → after 0.85s the ball is served
  → W/S moves the left paddle; the right paddle tracks the ball when it is incoming
  → first to 11 → YOU WIN or AI WINS
  → R starts a new match; Esc quits
```

**Two players** (`A` before the match ends):

```
A clears ai
  → Up/Down moves the right paddle; the computer step is not called
  → the footer line changes to the two-player hint
  → first to 11 → LEFT WINS or RIGHT WINS
```

`A` after the winner is showing does nothing. The headline stays. `R` is the only way back into a match, apart from quitting.

### Why testers care

- **There is no CLI feature flag.** "Computer on the right" is the initial value of `game.ai`, not an argv switch. Automating a two-player match means sending `A` to the window, or starting from a breakpoint after that assignment.
- **Nothing is saved.** Quit and relaunch and the score is 0, the computer is back, and the serve direction is a new coin flip. Do not look for an INI file.
- **Exit is process-level.** Close the window or press Esc. There is no tray icon.
- **The first thing on screen is READY, not a moving ball.** A test that samples the first frame and expects `velocity.x != 0` will fail on purpose. Motion starts when `serveTimer` reaches 0.
- **Pause freezes the serve clock too.** Space during READY leaves the ball in the centre until you unpause. The countdown does not run in the background.

---

## 2. The `Game` struct

This is the whole model. Draw code receives it as `const Game *`.

| Field | Meaning |
|-------|---------|
| `left`, `right` | Axis-aligned paddle rectangles in pixels. `y` is the top edge |
| `ball` | Centre of the ball in pixels |
| `velocity` | Pixels per second. `{0,0}` means waiting to serve, or the match is over |
| `leftScore`, `rightScore` | Points. First to 11 wins. Not win-by-two |
| `serveDir` | `+1` the next (or current) serve travels right; `-1` travels left |
| `serveTimer` | Seconds remaining before `launch_from_serve`. `0` means the ball is live or the match is over |
| `ai` | Right paddle is the computer |
| `paused` | Simulation does not run. Draw still does, and adds `PAUSED` |
| `over` | A side reached 11. Ball is hidden. `R` clears this |
| `paddleSound`, `wallSound`, `scoreSound` | Synthesized tones. `frameCount == 0` means "no audio device; stay silent" |

`draw_game` does not write these. If a caption is wrong, the bug is in the flag. If the flag is right and the caption is wrong, the bug is in the `if (paused) / else if (over) / else if (serveTimer)` chain. That chain is ordered: pause hides the winner line, and the winner line hides READY.

### What is *not* in the struct

There is no camera, no entity list, and no accumulated transform. Positions are already in screen pixels. The window is not resizable, so those pixels stay the coordinate system for the whole process.

`TextFormat` (used for the score digits) returns a pointer into a rotating static buffer inside Raylib. `draw_score` measures and draws it immediately. Do not store that pointer.

---

## 3. How the frame loop works

```
while the window is open
  dt = GetFrameTime()          // seconds since the previous frame
  clamp dt to 0.033
  update_game
  draw_game                    // ClearBackground, then every primitive
  EndDrawing                   // swap / present
```

`SetTargetFPS(60)` aims at 16.7 ms. Movement is **not** "add 4 pixels every tick". It is `position += velocity * dt`, so a 30Hz hitch does not halve the speed. The `0.033` cap is the other half of that idea: a multi-second stall must not teleport the ball through a paddle. A tester who breaks in the debugger and then continues should see at most one 30fps step, not a ball that has left the table.

### Update vs draw

| Function | Mutates | Draws |
|----------|---------|-------|
| `update_game` | paddles, ball, velocity, scores, flags | no |
| `step_ball` | ball, velocity, scores | no |
| `draw_game` | nothing on `Game` | yes |
| `make_tone` | fills a `Sound` once, at startup | no |

### Keys, and when they are ignored

| Key | When it works | Effect |
|-----|---------------|--------|
| `R` | always, including game over and pause | `reset_match`, then this frame ends |
| `A` | not when `over` | flips `ai` |
| `P`, Space | not when `over` | flips `paused` |
| `W` / `S`, arrows | not when `paused` or `over` | move a paddle |
| Esc | always, including pause and game over | not read in `update_game`. Raylib's default exit key makes `WindowShouldClose` true, the same path as the close button. The process ends. There is no confirmation |
| `F` | never | not bound. No fullscreen flag, no `ToggleFullscreen`. The window stays 960×540 |

`draw_hint` writes that list along the bottom of the court, including `Esc quit`. It does not mention `F`, because `F` does nothing. `R` returns before the ball step so a brand-new serve is not advanced on the same frame it was created.

### Why one thread

The pixel buffer and the `Game` struct would need a lock if a second thread drew them. `EndDrawing` already waits toward the next frame. One thread keeps the keys and the ball in lockstep.

---

## 4. Ball and paddle math

Constants live at the top of `src/main.c`.

| Name | Value | Role |
|------|-------|------|
| `PADDLE_SPEED` | 460 px/s | Human paddles |
| `AI_SPEED` | 285 px/s | Computer, deliberately slower |
| `BALL_SPEED` | 340 px/s | Speed at the moment of the serve |
| `MAX_BALL_SPEED` | 720 px/s | Cap after repeated hits |
| `BALL_RADIUS` | 8 px | Collision and drawing |
| `PADDLE_W` × `PADDLE_H` | 14 × 96 | The thing the ball must not tunnel through |
| `SERVE_DELAY` | 0.85 s | READY |
| `WIN_SCORE` | 11 | `over` becomes true |

### Serve

`begin_serve` parks the ball at the centre and sets `velocity` to zero. When the timer elapses, `launch_from_serve` sets:

```text
velocity.x = 340 * serveDir
velocity.y = 340 * 0.28 * (±1)
```

So a serve is mostly horizontal, with a small random vertical component. The side that just missed is the side `serveDir` points toward.

### Walls

```text
if ball.y <= radius and velocity.y < 0:
    ball.y = radius
    velocity.y = -velocity.y
if ball.y >= SCREEN_H - radius and velocity.y > 0:
    ball.y = SCREEN_H - radius
    velocity.y = -velocity.y
```

The sign check is the state guard. Without it, a ball left one pixel outside the wall would reverse on every substep and buzz. Pulling `ball.y` back inside is what makes the next substep see a ball that is no longer colliding.

### Paddles

`CheckCollisionCircleRec` is Raylib's circle-versus-rectangle test (the circle against the paddle's axis-aligned box).

A collision is ignored when the ball is already travelling away from that paddle. That is the "do not get stuck inside the paddle" rule.

Otherwise the hit offset is how far the contact sits from the paddle's vertical centre, scaled to about `-1..1`:

```text
offset = (ball.y - paddleCenter) / (paddleHeight / 2)
angle  = offset * 45°
speed  = min(720, max(340, currentSpeed * 1.04))
velocity.x = cos(angle) * speed * direction
velocity.y = sin(angle) * speed
```

`direction` is `+1` for the left paddle and `-1` for the right, so the ball leaves toward the other side. A hit in the centre leaves horizontally. A hit near the end of the paddle leaves at 45°. `cos(45°)` is about `0.707`, so the horizontal component never collapses to nothing.

The ball's centre is then placed just outside the paddle face (`radius + 0.5` px). The next substep starts clear of the rectangle, so it cannot bounce twice on one contact.

### Scoring

The ball must fully leave the 960px width (`centre` past the edge by `radius`). Crossing the left edge increments `rightScore` and serves toward the left (`serveDir = -1`). Crossing the right edge increments `leftScore` and serves toward the right. If either score is now 11, `over` is set, the ball is parked and then not drawn, and no new serve is armed.

### Substeps

`update_game` divides the clamped `dt` into four calls to `step_ball`. At 720 px/s and 60fps, one full frame is 12 px of travel; a quarter of that is 3 px. The paddle is 14 px thick and the ball's radius adds another 8 px of reach, so a normal frame cannot skip the paddle. The substeps exist for the clamped hitch (0.033 s / 4 ≈ 6 px at top speed), which is still inside that reach. If a point is scored, the remaining substeps are skipped.

### Computer paddle

Target is the ball's `y` when `velocity.x > 0` (ball moving right) or when a serve toward the right is still on the clock. Otherwise the target is the vertical middle of the screen. The paddle moves at most `AI_SPEED * dt` toward that target, and it does not bother if it is already within 8 px. It never moves while `paused` or `over` is set, because `update_game` returns before `move_ai_paddle`.

---

## 5. Drawing

`BeginDrawing` / `EndDrawing` bracket one frame. `ClearBackground(BLACK)` throws the previous frame away. Then, in order:

1. Dashed net (short white rectangles down the centre).
2. A 1 px border inset by a pixel, both paddles, and the ball unless the match is over.
3. Scores, centred in each half, using Raylib's built-in font.
4. One caption: `PAUSED`, or the winner, or `READY`, or nothing during a rally.
5. The footer hint (`draw_hint`), which follows `ai` and ends with `Esc quit`. `F` is not on that line.

There is no retained-mode "object" that remembers it was drawn. If you comment out `ClearBackground`, old ball positions will smear. That smear is the easiest way to see that drawing is immediate.

The tones are not files. `make_tone` writes a 16-bit mono square wave with a short attack and a linear decay, at 44.1 kHz, then `LoadSoundFromWave` copies it into Raylib. If the audio device failed, the `Sound` stays zeroed and `play_sound` returns. The match still runs.

---

## Quick map

```
src/main.c
  Game                  # the match
  make_tone             # startup audio, no asset files
  begin_serve           # park the ball, arm the clock
  launch_from_serve     # waiting → in play
  reset_match           # any state → a new match
  move_player_paddle    # W/S or arrows
  move_ai_paddle        # right paddle, when ai is set
  bounce_walls          # vertical velocity sign
  bounce_paddle         # angle, speed cap, push-out
  finish_point          # next serve, or over
  step_ball             # integrate, collide, maybe score
  update_game           # keys, then up to four ball steps
  draw_game             # clear and redraw
  main                  # window, loop, shutdown
```

If you are tracing in a debugger, put breakpoints on `main`, `reset_match`, `launch_from_serve`, `bounce_paddle`, `finish_point`, and `draw_game`. You will see: **keys and dt change `Game` → `draw_game` reads it → `EndDrawing` shows it**.

See `EXECUTION_FLOW.md` for the same path as a numbered trace.
