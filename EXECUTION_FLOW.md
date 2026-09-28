# Execution flow: from `main` to a drawn frame

A step-by-step trace of what happens from `int main` through the Raylib window, down to how one frame moves the ball and then throws the previous picture away.

Default launch (`make run` or `./run.sh`) opens a 960×540 window with the computer on the right and the word READY in the middle. The ball does not move until the serve clock runs out.

One thread does everything after startup:

- **main** — window, audio, the `Game` struct, update, draw, shutdown

There is no second thread. `GetFrameTime`, `IsKeyPressed`, and `EndDrawing` run on the same stack that called `main`.

---

## Phase A — process entry

**1.** The OS loads `build/pong`. The C runtime starts. `Game` does not exist yet; it is a local in `main`, not a global.

**2.** `InitWindow(960, 540, "Pong Tribute")` creates the native window and the OpenGL context. Until this returns, there is nothing to draw and `IsKeyDown` is meaningless.

**3.** `InitAudioDevice`, `SetTargetFPS(60)`, `SetRandomSeed(time)`, `SetMasterVolume(0.85)`. The seed only affects `GetRandomValue`: the initial serve side, each later serve's vertical sign, and the serve side after `R`. It does not affect paddle speed.

**4.** The two rectangles are written. Left x is `36`. Right x is `960 - 36 - 14`. Width `14`, height `96`. Their `y` is filled in by `reset_match`, not here.

**5.** `ai` is set **true** before any frame. That is the state change from "zeroed struct" to "computer opponent".

**6.** `make_tone` runs three times (680 Hz paddle, 340 Hz wall, 190 Hz score). Each allocates a short 16-bit buffer, loads it with `LoadSoundFromWave`, and frees the buffer with `UnloadWave`. The `Sound` value stored on `Game` is the one that will be played later.

**7.** `reset_match`:

```text
leftScore = 0, rightScore = 0
paused = false, over = false
both paddles centred vertically
begin_serve(random ±1)
```

`begin_serve` is the state change **into waiting**:

```text
ball = (480, 270)
velocity = (0, 0)
serveTimer = 0.85
serveDir = +1 or -1
```

No pixels have been drawn yet. The loop has not started.

---

## Phase B — the repeating loop

Every frame the same thread does **update**, then **draw**.

```text
while !WindowShouldClose()
  dt = GetFrameTime()
  update_game(dt)
  draw_game()          # BeginDrawing … EndDrawing
```

`WindowShouldClose` is how Esc and the close button end the process. It is checked at the top of the next frame, so the frame you were on still finishes drawing.

There is no separate paint callback. `draw_game` runs because `main` calls it.

---

## Phase C — first frame (READY, ball not moving)

**8.** `GetFrameTime` on the first frame can be an odd number (time since `InitWindow`, not "one 60Hz slice"). `update_game` caps it:

```text
if dt > 0.033 then dt = 0.033
```

So the first READY tick burns at most 33 ms of the 850 ms serve clock. It cannot burn the whole delay, and it cannot launch the ball.

**9.** No key is down unless you are already holding one. `R` / `A` / `P` are `IsKeyPressed` (edge, not level). `W` and `S` are `IsKeyDown` (level), but they are not read yet if we return early. On this frame we do not return early for pause or game-over.

**10.** The left paddle moves if `W` or `S` is held. The computer paddle runs because `ai` is true. The ball's velocity is still zero and `serveTimer > 0`, and `serveDir` may be `+1`. If it is, the computer's target is the ball (still the centre). If the serve will go left, the computer's target is the middle of the screen. Either way it is already there, inside the 8 px dead zone, so its `y` does not change.

**11.** `serveTimer` decreases by `dt` and is still positive. `update_game` **returns**. `launch_from_serve` is not called. `step_ball` is not called. Velocity stays `{0,0}`.

**12.** `draw_game` starts. `ClearBackground(BLACK)` fills all 960×540. The net, border, paddles, and ball are drawn. The caption chain sees `paused == false`, `over == false`, `serveTimer > 0`, so it draws **READY**. The footer is the one-player hint, because `ai` is true. `EndDrawing` puts that picture on the window.

A screenshot of frame 1 is a still life. That is correct.

---

## Phase D — the serve (waiting → in play)

**13.** Later frames repeat phase C. Each one subtracts a small `dt` from `serveTimer`. Paddles can move. The ball stays at `(480, 270)`.

**14.** The frame where `serveTimer - dt` is no longer positive does not return. It calls `launch_from_serve`:

```text
velocity.x = 340 * serveDir
velocity.y = 340 * 0.28 * (±1)     # GetRandomValue
serveTimer = 0
```

That is the state change **waiting → in play**. The ball now has a velocity. READY will not be drawn again until the next `begin_serve`.

**15.** The same frame then runs `step_ball` four times with `dt/4`. The ball leaves the centre by a few pixels. It is nowhere near a paddle yet, so `bounce_paddle` returns at the circle-rectangle test. It is nowhere near a wall. Both score tests fail. The function returns false four times.

**16.** `draw_game` draws the ball at its new centre. The caption chain finds no pause, no winner, and `serveTimer == 0`, so it draws no READY and no PAUSED. Only the footer remains.

---

## Phase E — a rally frame (this is the steady state)

**17.** `update_game` again. Keys first:

| Event | State change |
|-------|----------------|
| `R` | any → new match (`reset_match`), then **return**. The new serve is not stepped this frame |
| `over` already set | return. `A` is not read, so the winner line cannot be relabelled |
| `A` | `ai` flips. Score unchanged. Footer changes on the draw that follows |
| `P` or Space | `paused` flips, then return. Serve clock and ball do not move |
| none of the above | fall through to paddles and ball |

**18.** Human paddle:

```text
direction = (S or Down ? +1 : 0) + (W or Up ? -1 : 0)
paddle.y += direction * 460 * dt
clamp to 0 .. 540-96
```

`+y` is downward. `W` decreases `y`.

**19.** If `ai` is still true, `move_ai_paddle`. Incoming means `velocity.x > 0` (after the serve has gone right, or the ball has bounced back toward the computer). The paddle steps at most `285 * dt` toward the ball's `y`, or toward `270` when the ball is travelling left. The dead zone is 8 px.

**20.** `step_ball` four times:

```text
ball += velocity * (dt/4)
bounce_walls
bounce_paddle(left, direction +1)
bounce_paddle(right, direction -1)
maybe score, and then stop the remaining substeps
```

**21.** Wall, when it happens. Example, top edge:

```text
ball.y was moving up (velocity.y < 0) and ball.y <= 8
  → ball.y = 8
  → velocity.y = -velocity.y          # now moving down
  → wall tone
```

The position write is what makes substep `i+1` not flip the sign back.

**22.** Paddle, when it happens. `CheckCollisionCircleRec` is true **and** the ball is still travelling toward that paddle (left paddle: `velocity.x < 0`; right paddle: `velocity.x > 0`). Then:

```text
offset = where on the face, clamped to -1..1
speed  = clamp(length(velocity) * 1.04, 340, 720)
angle  = offset * 45°
velocity.x = cos(angle) * speed * direction
velocity.y = sin(angle) * speed
ball.x     = just outside the paddle face
paddle tone
```

A centre hit leaves with `velocity.y == 0`. An edge hit leaves at 45°. The push-out is why the next substep does not see the collision again: the early-out on velocity sign would also save us, and both are there on purpose.

**23.** Score, when the centre has fully crossed an end:

```text
ball.x + 8 < 0        → rightScore++, finish_point(serveDir -1)
ball.x - 8 > 960      → leftScore++,  finish_point(serveDir +1)
```

`finish_point` plays the score tone, then either:

```text
score >= 11  →  over = true, velocity = 0, ball parked at centre, return
else         →  begin_serve(serveDir)     # back to phase C's waiting state
```

`step_ball` returns true, and the `for` loop breaks. A point cannot be awarded twice in one frame.

---

## Phase F — draw (every frame, including the ones that returned early)

**24.** `draw_game` does not take a non-const pointer. A breakpoint here should never show `leftScore` changing.

**25.** Order inside the bracket:

1. `ClearBackground(BLACK)` — previous ball position is gone.
2. Net: a 2×12 rectangle every 22 px down the centre line.
3. Border, left paddle, right paddle.
4. Ball, **unless `over`**. The parked centre ball of a finished match is not drawn; the banner covers that spot anyway.
5. Scores at x = 240 and x = 720, font size 64, each string centred on that x. `TextFormat`'s pointer is used before any other `TextFormat` call.
6. Caption, first match wins:
   - `paused` → `PAUSED`
   - else `over` → `YOU WIN` / `AI WINS` when `ai`, else `LEFT WINS` / `RIGHT WINS`, plus `R TO PLAY AGAIN`
   - else `serveTimer > 0` → `READY`
   - else no caption (the rally)
7. Footer from `ai`, via `draw_hint`. One-player: `W/S move`, `A two players`, `P pause`, `R restart`, `Esc quit`. Two-player: the left-paddle and arrow keys, `A computer opponent`, then the same pause, restart, and quit words. `F` is absent on purpose.
8. `EndDrawing` — presents the frame and paces toward 60Hz.

The winner banner is a black rectangle behind the words so the net does not cut through the letters. It is paint, not a widget.

---

## Phase G — state changes worth a breakpoint

All of these are commented at the assignment in `src/main.c`.

| Event | From → to |
|-------|-----------|
| `reset_match` at startup, or `R` | anything → scores 0, not paused, not over, paddles centred, waiting to serve |
| `launch_from_serve` | waiting (`velocity` 0, timer running) → in play (`serveTimer` 0, velocity set) |
| `P` / Space | running ↔ paused. The serve clock does not tick while paused |
| `A` | computer opponent ↔ two humans. Ignored once `over` is set |
| top or bottom wall | `velocity.y` sign flips, `ball.y` clamped inside |
| paddle contact while travelling inward | incoming horizontal sign → outgoing, new angle, speed × 1.04 up to 720, ball pushed clear |
| paddle contact while already travelling outward | no change (the stuck-ball guard) |
| ball fully past the left edge | `rightScore += 1`, then serve toward the left, or match over |
| ball fully past the right edge | `leftScore += 1`, then serve toward the right, or match over |
| either score reaches 11 | in play → `over`. Ball hidden. Only `R` or quit will leave |
| Esc or close | loop ends → shutdown. `update_game` never sees Esc; Raylib reports it as the window closing |
| `F` | no change. Fullscreen is not implemented |

`serveDir` does **not** flip when a paddle hits the ball. It records who receives the next serve. During a rally the computer ignores it and watches `velocity.x` instead. It consults `serveDir` only while `serveTimer > 0`.

---

## Phase H — shutdown

**26.** Esc or the close button makes `WindowShouldClose` true. The `while` ends. No "are you sure".

**27.** Each tone is unloaded if `frameCount > 0`. Then `CloseAudioDevice`, then `CloseWindow` (the OpenGL context goes here; do not draw after this). `main` returns 0.

```c
unload_tone(game.paddleSound);
unload_tone(game.wallSound);
unload_tone(game.scoreSound);
CloseAudioDevice();
CloseWindow();
return 0;
```

---

## One-line map

`main` → open window → `reset_match` parks the ball → each frame **`update_game` moves `Game` by `dt`** → **`draw_game` clears and redraws** → Esc closes the context.

Debugger: `main`, `reset_match`, `launch_from_serve`, `bounce_paddle`, `finish_point`, `draw_game`. The first frames only tick `serveTimer`. The ball's velocity stays zero until `launch_from_serve`.

See also `WORKINGS.md` for the struct fields and the bounce formula in one place.
