#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "raylib.h"

/*
 * Pong Tribute.
 *
 * One window, one loop, one Game struct. Nothing is stored between frames
 * except that struct: the screen is cleared and drawn again every time.
 * WORKINGS.md says who owns which fields. EXECUTION_FLOW.md follows one frame.
 *
 * The drawing surface is 960×540 pixels. Origin is the top-left.
 * +x points right, +y points down (Raylib's screen space, not a maths graph).
 */
#define SCREEN_W 960
#define SCREEN_H 540

#define PADDLE_W 14.0f
#define PADDLE_H 96.0f
#define PADDLE_MARGIN 36.0f
#define PADDLE_SPEED 460.0f
#define AI_SPEED 285.0f

#define BALL_RADIUS 8.0f
#define BALL_SPEED 340.0f
#define MAX_BALL_SPEED 720.0f
#define SERVE_DELAY 0.85f
#define WIN_SCORE 11

typedef struct
{
    Rectangle left;
    Rectangle right;
    Vector2 ball;
    Vector2 velocity;       /* pixels per second; {0,0} means "waiting to serve" */
    int leftScore;
    int rightScore;
    int serveDir;           /* +1 travels toward the right paddle, -1 toward the left */
    float serveTimer;       /* seconds left before the ball leaves the centre */
    bool ai;                /* right paddle is the computer */
    bool paused;
    bool over;              /* a side has reached WIN_SCORE */
    Sound paddleSound;
    Sound wallSound;
    Sound scoreSound;
} Game;

/* Square-wave beeps live in the binary. There is no wav file to copy between machines. */
static Sound make_tone(float frequency, float duration)
{
    Sound silent = {0};
    const int sampleRate = 44100;
    int frames = (int)(sampleRate * duration);
    short *samples;
    int i;
    Wave wave;

    if (!IsAudioDeviceReady() || frames <= 0)
    {
        return silent;
    }

    samples = calloc((size_t)frames, sizeof(short));
    if (samples == NULL)
    {
        return silent;
    }

    for (i = 0; i < frames; i++)
    {
        float phase = frequency * (float)i / (float)sampleRate;
        float cycle = phase - floorf(phase);
        float sample = (cycle < 0.5f) ? 1.0f : -1.0f;
        float env = 1.0f - ((float)i / (float)frames);
        int attack = sampleRate / 400;

        if (i < attack)
        {
            env *= (float)i / (float)attack;
        }

        samples[i] = (short)(sample * env * 0.32f * 32767.0f);
    }

    wave = (Wave){
        .frameCount = (unsigned int)frames,
        .sampleRate = (unsigned int)sampleRate,
        .sampleSize = 16,
        .channels = 1,
        .data = samples
    };

    silent = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return silent;
}

static void play_sound(Sound sound)
{
    if (sound.frameCount > 0)
    {
        PlaySound(sound);
    }
}

static void unload_tone(Sound sound)
{
    if (sound.frameCount > 0)
    {
        UnloadSound(sound);
    }
}

/* State: in play, or match over → waiting on the centre spot. Velocity is cleared. */
static void begin_serve(Game *game, int serveDir)
{
    game->serveDir = (serveDir < 0) ? -1 : 1;
    game->ball = (Vector2){ SCREEN_W * 0.5f, SCREEN_H * 0.5f };
    game->velocity = (Vector2){ 0.0f, 0.0f };
    game->serveTimer = SERVE_DELAY;
}

/* State: waiting → in play. Horizontal sign is who must receive; vertical is a coin flip. */
static void launch_from_serve(Game *game)
{
    int vertical = GetRandomValue(0, 1) ? 1 : -1;

    game->velocity.x = BALL_SPEED * (float)game->serveDir;
    game->velocity.y = BALL_SPEED * 0.28f * (float)vertical;
    game->serveTimer = 0.0f;
}

/* State: any match → a new one. Scores, pause, and the winner flag all drop. */
static void reset_match(Game *game)
{
    game->leftScore = 0;
    game->rightScore = 0;
    game->paused = false;
    game->over = false;
    game->left.y = (SCREEN_H - PADDLE_H) * 0.5f;
    game->right.y = (SCREEN_H - PADDLE_H) * 0.5f;
    begin_serve(game, GetRandomValue(0, 1) ? 1 : -1);
}

static void clamp_paddle(Rectangle *paddle)
{
    float maxY = SCREEN_H - paddle->height;

    if (paddle->y < 0.0f)
    {
        paddle->y = 0.0f;
    }
    if (paddle->y > maxY)
    {
        paddle->y = maxY;
    }
}

static void move_player_paddle(Rectangle *paddle, int upKey, int downKey, float dt)
{
    float direction = 0.0f;

    if (IsKeyDown(upKey))
    {
        direction -= 1.0f;
    }
    if (IsKeyDown(downKey))
    {
        direction += 1.0f;
    }

    paddle->y += direction * PADDLE_SPEED * dt;
    clamp_paddle(paddle);
}

/*
 * The computer only chases while the ball is coming toward it, or while a
 * serve in its direction is still waiting. Otherwise it drifts home, slowly,
 * so a player can beat it. The 8px dead zone stops the paddle buzzing.
 */
static void move_ai_paddle(Game *game, float dt)
{
    float target = SCREEN_H * 0.5f;
    float center;
    float delta;
    float step;
    bool incoming = (game->velocity.x > 0.0f)
        || (game->serveTimer > 0.0f && game->serveDir > 0);

    if (incoming)
    {
        target = game->ball.y;
    }

    center = game->right.y + game->right.height * 0.5f;
    delta = target - center;
    if (fabsf(delta) < 8.0f)
    {
        return;
    }

    step = AI_SPEED * dt;
    if (delta > step)
    {
        delta = step;
    }
    if (delta < -step)
    {
        delta = -step;
    }

    game->right.y += delta;
    clamp_paddle(&game->right);
}

static void bounce_walls(Game *game)
{
    /*
     * State: vertical direction flips, but only while the ball is still
     * travelling into that wall. The position is pulled back inside so the
     * next substep cannot see it outside and flip the velocity again.
     */
    if (game->ball.y <= BALL_RADIUS && game->velocity.y < 0.0f)
    {
        game->ball.y = BALL_RADIUS;
        game->velocity.y = -game->velocity.y;
        play_sound(game->wallSound);
    }
    else if (game->ball.y >= SCREEN_H - BALL_RADIUS && game->velocity.y > 0.0f)
    {
        game->ball.y = SCREEN_H - BALL_RADIUS;
        game->velocity.y = -game->velocity.y;
        play_sound(game->wallSound);
    }
}

/*
 * Hit position along the paddle picks the leaving angle (±45°).
 * direction is +1 when the ball should leave toward the right.
 * A hit that is already travelling away is ignored: that is what stops the
 * ball sticking inside the paddle and reversing on every substep.
 */
static void bounce_paddle(Game *game, Rectangle paddle, int direction)
{
    float paddleCenter;
    float offset;
    float speed;
    float angle;
    const float maxAngle = 45.0f * DEG2RAD;

    if (!CheckCollisionCircleRec(game->ball, BALL_RADIUS, paddle))
    {
        return;
    }
    if (direction > 0 && game->velocity.x > 0.0f)
    {
        return;
    }
    if (direction < 0 && game->velocity.x < 0.0f)
    {
        return;
    }

    paddleCenter = paddle.y + paddle.height * 0.5f;
    offset = (game->ball.y - paddleCenter) / (paddle.height * 0.5f);
    if (offset < -1.0f)
    {
        offset = -1.0f;
    }
    if (offset > 1.0f)
    {
        offset = 1.0f;
    }

    speed = sqrtf(game->velocity.x * game->velocity.x + game->velocity.y * game->velocity.y);
    speed *= 1.04f;
    if (speed < BALL_SPEED)
    {
        speed = BALL_SPEED;
    }
    if (speed > MAX_BALL_SPEED)
    {
        speed = MAX_BALL_SPEED;
    }

    /* State: approaching this paddle → leaving it, a little faster, at an angle. */
    angle = offset * maxAngle;
    game->velocity.x = cosf(angle) * speed * (float)direction;
    game->velocity.y = sinf(angle) * speed;

    if (direction > 0)
    {
        game->ball.x = paddle.x + paddle.width + BALL_RADIUS + 0.5f;
    }
    else
    {
        game->ball.x = paddle.x - BALL_RADIUS - 0.5f;
    }

    play_sound(game->paddleSound);
}

/* State: rally → next serve, or rally → match over once a side reaches WIN_SCORE. */
static void finish_point(Game *game, int serveDir)
{
    play_sound(game->scoreSound);

    if (game->leftScore >= WIN_SCORE || game->rightScore >= WIN_SCORE)
    {
        game->over = true;
        game->velocity = (Vector2){ 0.0f, 0.0f };
        game->ball = (Vector2){ SCREEN_W * 0.5f, SCREEN_H * 0.5f };
        return;
    }

    /* The side that missed receives. serveDir is the ball's next horizontal sign. */
    begin_serve(game, serveDir);
}

/* Returns true when a point ended, so the caller stops stepping the ball. */
static bool step_ball(Game *game, float dt)
{
    game->ball.x += game->velocity.x * dt;
    game->ball.y += game->velocity.y * dt;

    bounce_walls(game);
    bounce_paddle(game, game->left, 1);
    bounce_paddle(game, game->right, -1);

    if (game->ball.x + BALL_RADIUS < 0.0f)
    {
        /* State: ball left the table on the left → point to the right. */
        game->rightScore += 1;
        finish_point(game, -1);
        return true;
    }
    if (game->ball.x - BALL_RADIUS > SCREEN_W)
    {
        /* State: ball left the table on the right → point to the left. */
        game->leftScore += 1;
        finish_point(game, 1);
        return true;
    }

    return false;
}

static void update_game(Game *game, float dt)
{
    const int substeps = 4;
    int i;

    /*
     * A stalled frame (dragging the window, a breakpoint) must not fling the
     * ball through a 14px paddle. 0.033s is one frame at 30fps.
     */
    if (dt > 0.033f)
    {
        dt = 0.033f;
    }

    if (IsKeyPressed(KEY_R))
    {
        /* State: whatever this match was → a new one. Skip the rest of the frame. */
        reset_match(game);
        return;
    }
    if (game->over)
    {
        /* Match is over. R was already handled. The winner text stays put. */
        return;
    }
    if (IsKeyPressed(KEY_A))
    {
        /* State: computer opponent ↔ two human paddles. The score is kept. */
        game->ai = !game->ai;
    }
    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_SPACE))
    {
        /* State: running ↔ paused. Everything below, including the serve clock, waits. */
        game->paused = !game->paused;
    }
    if (game->paused)
    {
        return;
    }

    move_player_paddle(&game->left, KEY_W, KEY_S, dt);
    if (game->ai)
    {
        move_ai_paddle(game, dt);
    }
    else
    {
        move_player_paddle(&game->right, KEY_UP, KEY_DOWN, dt);
    }

    if (game->serveTimer > 0.0f)
    {
        game->serveTimer -= dt;
        if (game->serveTimer > 0.0f)
        {
            return;
        }
        launch_from_serve(game);
    }

    /*
     * Four small steps instead of one big one. At top speed a single 60fps
     * step is still shorter than the paddle is thick, but a hitch is not.
     */
    for (i = 0; i < substeps; i++)
    {
        if (step_ball(game, dt / (float)substeps))
        {
            break;
        }
    }
}

static void draw_score(int score, int centerX)
{
    const char *text = TextFormat("%d", score);
    int width = MeasureText(text, 64);

    DrawText(text, centerX - width / 2, 28, 64, RAYWHITE);
}

static void draw_centered(const char *text, int y, int fontSize, Color color)
{
    int width = MeasureText(text, fontSize);

    DrawText(text, (SCREEN_W - width) / 2, y, fontSize, color);
}

/* The hint is the only menu. Esc is Raylib's quit key; F is not listed because it is not bound. */
static void draw_hint(const Game *game)
{
    const char *text;
    int size = 18;

    if (game->ai)
    {
        text = "W/S move    A two players    P pause    R restart    Esc quit";
    }
    else
    {
        text = "W/S left    arrows right    A computer opponent    P pause    R restart    Esc quit";
    }

    if (MeasureText(text, size) > SCREEN_W - 24)
    {
        size = 16;
    }

    draw_centered(text, SCREEN_H - 32, size, LIGHTGRAY);
}

/* Reads Game. Does not change it. The previous frame's pixels are discarded. */
static void draw_game(const Game *game)
{
    int y;

    BeginDrawing();
    ClearBackground(BLACK);

    for (y = 0; y < SCREEN_H; y += 22)
    {
        DrawRectangle(SCREEN_W / 2 - 1, y, 2, 12, (Color){ 180, 180, 180, 255 });
    }

    DrawRectangleLines(1, 1, SCREEN_W - 2, SCREEN_H - 2, RAYWHITE);
    DrawRectangleRec(game->left, RAYWHITE);
    DrawRectangleRec(game->right, RAYWHITE);

    if (!game->over)
    {
        DrawCircleV(game->ball, BALL_RADIUS, RAYWHITE);
    }

    draw_score(game->leftScore, SCREEN_W / 4);
    draw_score(game->rightScore, (SCREEN_W * 3) / 4);

    /* Which caption appears is a readout of paused / over / serveTimer, in that order. */
    if (game->paused)
    {
        draw_centered("PAUSED", SCREEN_H / 2 - 20, 40, RAYWHITE);
    }
    else if (game->over)
    {
        const char *headline;

        if (game->ai)
        {
            headline = (game->leftScore > game->rightScore) ? "YOU WIN" : "AI WINS";
        }
        else
        {
            headline = (game->leftScore > game->rightScore) ? "LEFT WINS" : "RIGHT WINS";
        }

        DrawRectangle(SCREEN_W / 2 - 170, SCREEN_H / 2 - 48, 340, 96, BLACK);
        draw_centered(headline, SCREEN_H / 2 - 36, 40, RAYWHITE);
        draw_centered("R TO PLAY AGAIN", SCREEN_H / 2 + 12, 20, LIGHTGRAY);
    }
    else if (game->serveTimer > 0.0f)
    {
        draw_centered("READY", SCREEN_H / 2 - 56, 28, LIGHTGRAY);
    }

    draw_hint(game);

    EndDrawing();
}

int main(void)
{
    Game game = {0};

    InitWindow(SCREEN_W, SCREEN_H, "Pong Tribute");
    InitAudioDevice();
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)time(NULL));
    SetMasterVolume(0.85f);

    game.left = (Rectangle){ PADDLE_MARGIN, 0.0f, PADDLE_W, PADDLE_H };
    game.right = (Rectangle){
        SCREEN_W - PADDLE_MARGIN - PADDLE_W,
        0.0f,
        PADDLE_W,
        PADDLE_H
    };
    /* State: process start → a match that is waiting to serve, computer on the right. */
    game.ai = true;
    game.paddleSound = make_tone(680.0f, 0.045f);
    game.wallSound = make_tone(340.0f, 0.035f);
    game.scoreSound = make_tone(190.0f, 0.16f);
    reset_match(&game);

    /* A frame is: move the world by the time that really passed, then draw it all. */
    while (!WindowShouldClose())
    {
        update_game(&game, GetFrameTime());
        draw_game(&game);
    }

    /* State: window still open → process exit. Audio buffers, then the GL context. */
    unload_tone(game.paddleSound);
    unload_tone(game.wallSound);
    unload_tone(game.scoreSound);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
