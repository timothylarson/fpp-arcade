#include <fpp-pch.h>

#include "FPPSnowballFight.h"
#include "ArcadeCanvas.h"
#include "SnowballFightShow.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;
using SnowballFightShow::HAT;

// Two elves on snowy hills take turns throwing a snowball at each other: aim,
// charge, let go. The snowball flies on a real arc - gravity and the wind,
// which changes every turn - and wherever it lands it blows a crater out of
// the snow, so the hills wear away as the fight goes on and an elf whose
// snow is blown from under it drops into the hole. Landing close enough to
// an elf knocks off one of its health pips; the first knocked out loses.
//
// The red elf on the left is the D-pad's: Left and Right aim, hold Up to
// charge and let go to throw. The green elf on the right is A and B's: hold
// B to swing the aim (each press swings it the other way), hold A to charge.
// Nobody on A and B by the green elf's first turn means the computer plays
// it, and a press of A or B later takes it back.
//
// Everything is in LEDs and seconds, and the flight is stepped at a fixed
// rate, so the computer's practice throws come out exactly as real ones do.

namespace {

const char *const SFX = "snowballfight";
constexpr int FRAME_MS = 25;
constexpr float SIM_DT = 1.0f / 240.0f;   // the flight's fixed step: a quarter LED at most
constexpr float CHARGE_S = 1.6f;          // empty to full power, holding the button
constexpr float SPLASH = 1.5f;            // how near an elf a snowball must land to count
constexpr float SAFE_S = 0.3f;            // your own snowball can't hit you this soon
constexpr float IDLE_TAKEOVER_S = 8.0f;   // a two-player turn nobody touches: the computer steps in
constexpr float ABANDON_S = 45.0f;        // no presses at all: everybody's gone home
constexpr float AIM_SPEED = 45.0f;        // degrees a second, holding
constexpr float AIM_REPEAT_S = 0.2f;      // a tap is one degree; holding longer sweeps

// Dim: the snow is a third of the wall, and the elves, the snowball, the
// aim line and the pips have to stand out against it. The sky is left black
// - a coloured fill just looks washed out on the lights.
// The hills' surface, then all but black below it (2% at most): a filled
// hill washes out on the real wall, and the line shows its shape and craters.
const RGB SNOW_TOP = {0.16f, 0.19f, 0.26f};
const RGB SNOW_DEEP = {0.006f, 0.008f, 0.02f};
const RGB MOON = {1.0f, 0.93f, 0.7f};
const RGB AIM_IDLE = {1.0f, 0.95f, 0.7f};
const RGB POWER_LO = {1.0f, 0.9f, 0.2f};
const RGB POWER_HI = {1.0f, 0.15f, 0.05f};
const RGB WIND = {0.45f, 0.8f, 1.0f};
const RGB WHITE = {1.0f, 1.0f, 1.0f};

// Held buttons, as bits: the D-pad's, then A and B.
enum : uint32_t { LEFT = 1, RIGHT = 2, UP = 4, DOWN = 8, A_BTN = 16, B_BTN = 32 };
constexpr uint32_t PAD = LEFT | RIGHT | UP | DOWN, AB = A_BTN | B_BTN;

}  // namespace

class SnowballFightEffect : public FPPArcadeGameEffect {
public:
    // hits: health each elf starts with; turnSecs: the turn timer; maxWind:
    // the strongest the wind gets, 0-10; skill: the computer's, 1-5.
    SnowballFightEffect(PixelOverlayModel *m, int hits, int turnSecs, int maxWind, int skill, int showMode, int good)
        : FPPArcadeGameEffect(m), endShow(showMode), goodGame(good), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(20, W);
        H = std::max(16, H);
        canvas.resize(W, H);
        startHealth = std::clamp(hits, 1, 9);
        turnTime = (float)std::clamp(turnSecs, 5, 60);
        windMax = std::clamp(maxWind, 0, 10);
        cpuSkill = std::clamp(skill, 1, 5);
        // Gravity for the height and throwing speed for the width, so that a
        // full-power throw at 45 degrees carries about 1.1 widths on the flat:
        // across the wall into a stiff headwind, with a little to spare, and
        // up to a second and a half in the air on a 60 x 25 wall.
        g = 55.0f * H / 25.0f;
        vMax = std::sqrt(1.1f * W * g);
        // The strongest wind moves a cross-wall throw nine or ten LEDs.
        windUnit = 1.3f * W / 60.0f;
        craterR = W >= 40 ? 2.4f : 2.0f;
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "SnowballFight";
        return NAME;
    }

    void button(const std::string &b) override {
        if (show.on()) {
            if (b.find("Pressed") != std::string::npos) {
                show.skip();
            }
            return;
        }
        const bool pressed = b.size() > 8 && b.compare(b.size() - 8, 8, " Pressed") == 0;
        const bool released = b.size() > 9 && b.compare(b.size() - 9, 9, " Released") == 0;
        uint32_t bits = 0;
        if (pressed || released) {
            const std::string n = b.substr(0, b.find(" - "));
            if (n == "Left") bits = LEFT;
            else if (n == "Right") bits = RIGHT;
            else if (n == "Up") bits = UP;
            else if (n == "Down") bits = DOWN;
            else if (n == "Up/Left") bits = UP | LEFT;
            else if (n == "Up/Right") bits = UP | RIGHT;
            else if (n == "Down/Left") bits = DOWN | LEFT;
            else if (n == "Down/Right") bits = DOWN | RIGHT;
            else if (n == "A Button" || n == "Fire" || n == "Fire Button") bits = A_BTN;
            else if (n == "B Button") bits = B_BTN;
            if (pressed) {
                // A stick can swing from one side to the other without
                // letting go in between, so a direction replaces its opposite.
                uint32_t clear = 0;
                if (bits & LEFT) clear |= RIGHT;
                if (bits & RIGHT) clear |= LEFT;
                if (bits & UP) clear |= DOWN;
                if (bits & DOWN) clear |= UP;
                held.fetch_and(~clear);
                held.fetch_or(bits);
            } else {
                held.fetch_and(~bits);
            }
        }
        const bool wasPaused = paused;
        FPPArcadeGameEffect::button(b);
        if (wasPaused || paused) {
            return;
        }
        if (pressed) {
            // The game sees these once each, on its own thread.
            newPresses.fetch_or(bits);
        }
    }

    void restart() override {
        newGame();
        resetFrameTimer();
    }

    int32_t update() override {
        if (finished) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return EFFECT_DONE;
        }
        if (show.on()) {
            if (show.step(model, consumeElapsedMs(50.0))) {
                return 50;
            }
            // Over: the game ends as it would have after the result.
            return finish();
        }
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return FRAME_MS;
        }
        const float dt = (float)(consumeElapsedMs(FRAME_MS, 100.0) / 1000.0);
        clock += dt;
        stateT += dt;
        const int cheat = takeCheat();
        if (cheat && state != State::Over && state != State::Intro) {
            useCheat(cheat);
        }
        step(dt);
        if (show.on()) {
            resetFrameTimer();   // the game ended well: the show starts next frame
            return 50;
        }
        if (state == State::Over && stateT > (winner < 0 ? 2.5f : 5.0f)) {
            return finish();
        }
        beginFrame();   // drawn in passes, the text last: see beginFrame()
        render();
        endFrame();
        return FRAME_MS;
    }

private:
    enum class State { Intro, Aim, Flight, Settle, Over };

    struct Elf {
        int cx = 0;            // the middle column; the sprite is cx-1 .. cx+1
        int facing = 1;        // +1 throws to the right
        float y = 0, vy = 0;   // feet: the sprite fills the four rows above
        int health = 3;
        float angle = 45;      // degrees above the horizontal, toward the other elf
        float lastAngle = -1, lastPower = -1;   // the last throw, shown faintly next turn
        bool human = false;
        bool comeBack = false; // its control was pressed while the computer had it
        bool hinted = false;   // its control hint has been shown
        int throws = 0;
        float flashT = 0;
        int swing = 1;         // which way B swings the green elf's aim
    };
    struct Ball {
        float x, y, vx, vy, t;
        int owner;
    };
    // Where a flight ended: hit an elf (who), landed (who -1), or flew off
    // the side or ran out of time (who -2).
    struct Landing {
        float x, y, t;
        int who;
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
        float gravity;
    };
    struct Flake {
        float x, y, vy, phase;
    };
    struct Star {
        int x, y;
        float phase;
    };
    // The computer's turn: a moment to think, swing the aim round, charge.
    struct Cpu {
        float angle = 45, power = 0.5f;
        int phase = 0;
        float t = 0, aimSoundAt = 0;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    float gauss() { return std::normal_distribution<float>(0.0f, 1.0f)(rng); }

    // ---------------------------------------------------------------- game flow
    void newGame() {
        makeHills();
        for (int s = 0; s < 2; ++s) {
            Elf &e = elf[s];
            e = Elf();
            // Red is whoever started the game; green is the computer's
            // unless someone presses A or B before its first turn.
            e.human = s == 0;
            e.health = startHealth;
            e.facing = s == 0 ? 1 : -1;
            e.y = -2.0f - 3.0f * s;   // they drop in from the sky, the green one just after
        }
        elf[0].cx = (int)std::lround(W * 0.1f) + (int)(rnd() * 3);
        elf[1].cx = W - 1 - ((int)std::lround(W * 0.1f) + (int)(rnd() * 3));
        flattenUnder(elf[0].cx);
        flattenUnder(elf[1].cx);
        cur = 1;   // so the first turn is the red elf's
        winner = -1;
        wind = 0;
        windTarget = 0;
        sparks.clear();
        trail.clear();
        ballOn = false;
        giant = false;
        swungOnce = false;
        word.clear();
        wordT = 0;
        anyPressAt = 0;
        padAt[0] = padAt[1] = 0;
        clock = 0;
        newPresses.store(0);
        makeSky();
        state = State::Intro;
        stateT = 0;
        sfxStop(SFX);
        sfx(SFX, "Start");
    }

    // Rolling hills: two waves of different lengths, and now and then a
    // hill (or a dip) in the middle so that some fights need a high lob.
    void makeHills() {
        ground.assign(W, 0.0f);
        const float p1 = rnd() * 6.283f, p2 = rnd() * 6.283f;
        const float f1 = 0.8f + rnd() * 0.8f, f2 = 2.0f + rnd() * 1.5f;
        const float bump = (rnd() < 0.6f ? -1.0f : 0.5f) * H * (0.08f + rnd() * 0.1f);
        for (int x = 0; x < W; ++x) {
            const float u = (x + 0.5f) / W;
            const float mid = (u - 0.5f) / 0.14f;
            float y = H * 0.68f + H * 0.11f * std::sin(6.283f * u * f1 + p1) + H * 0.045f * std::sin(6.283f * u * f2 + p2) +
                      bump * std::exp(-mid * mid);
            ground[x] = std::clamp(y, H * 0.48f, H - 2.0f);
        }
    }
    // A little level patch to stand on.
    void flattenUnder(int cx) {
        float sum = 0;
        int n = 0;
        for (int x = cx - 2; x <= cx + 2; ++x) {
            if (x >= 0 && x < W) {
                sum += ground[x];
                ++n;
            }
        }
        const float level = std::round(sum / std::max(1, n));
        for (int x = cx - 2; x <= cx + 2; ++x) {
            if (x >= 0 && x < W) {
                ground[x] = level;
            }
        }
        // Eases into the hill either side, so there's no step.
        for (int d = 3; d <= 4; ++d) {
            for (int x : {cx - d, cx + d}) {
                if (x >= 0 && x < W) {
                    ground[x] = ground[x] + (level - ground[x]) * (d == 3 ? 0.6f : 0.3f);
                }
            }
        }
    }

    void makeSky() {
        stars.clear();
        const int n = std::max(6, W * H / 90);
        for (int i = 0; i < n; ++i) {
            stars.push_back({(int)(rnd() * W), (int)(1 + rnd() * H * 0.4f), rnd() * 6.28f});
        }
        flakes.clear();
        const int f = std::max(6, W * H / 80);
        for (int i = 0; i < f; ++i) {
            flakes.push_back({rnd() * W, rnd() * H, 2.0f + rnd() * 2.5f, rnd() * 6.28f});
        }
        // Up on the red side, clear of the wind arrow in the middle of the top.
        moonX = W * 0.3f;
        moonY = H * 0.17f;
        moonR = std::max(1.6f, H / 12.0f);
    }

    // The next elf's turn: new wind, and whoever has its control.
    void nextTurn() {
        cur = 1 - cur;
        Elf &e = elf[cur];
        // The wind shifts a few steps each turn, drifting back toward calm,
        // so a fight has calm spells and gales without leaping from one to
        // the other or sitting at a gale. It always changes, so every turn
        // needs a fresh look at the arrow.
        if (windMax > 0) {
            const int was = windTarget;
            for (int tries = 0; tries < 8 && windTarget == was; ++tries) {
                windTarget = std::clamp((int)std::lround(was * 0.6f + gauss() * windMax * 0.45f), -windMax, windMax);
            }
        }
        if (e.comeBack) {
            e.comeBack = false;
            e.human = true;
            if (cur == 1) {
                sfx(SFX, "Join");
            }
        }
        turnLeft = turnTime;
        turnStart = clock;
        ticked = (int)turnTime;
        power = 0;
        charging = false;
        aimHoldT = 0;
        aimSoundAt = e.angle;
        if (!e.human) {
            planCpu();
        }
        state = State::Aim;
        stateT = 0;
        sfx(SFX, "Turn", 0.8f);
        if (std::abs(windTarget) >= 3) {
            sfx(SFX, "Wind", 0.35f + 0.08f * std::abs(windTarget));
        }
    }

    // The turn's wind, as it acts on a snowball: the same from the start of
    // the turn to the end, so the computer's practice throws match its real one.
    float windAccel() const { return windTarget * windUnit; }
    float speedFor(float p) const {
        // The square root makes distance grow evenly with how long the
        // button's held, which is easier to judge than speed.
        return vMax * std::sqrt(0.04f + 0.96f * std::clamp(p, 0.0f, 1.0f));
    }
    float handX(const Elf &e) const { return e.cx + 0.5f + 2.0f * e.facing; }
    float handY(const Elf &e) const { return e.y - 1.5f; }
    int feet(const Elf &e) const { return (int)std::lround(e.y); }
    // Where an elf stands: on the highest of the three columns under it.
    float standY(const Elf &e) const {
        float y = (float)H;
        for (int x = e.cx - 1; x <= e.cx + 1; ++x) {
            if (x >= 0 && x < W) {
                y = std::min(y, ground[x]);
            }
        }
        return y;
    }

    void step(float dt) {
        stepSparks(dt);
        stepFlakes(dt);
        settle(dt);
        wordT = std::max(0.0f, wordT - dt);
        for (auto &e : elf) {
            e.flashT = std::max(0.0f, e.flashT - dt);
        }
        // The arrow (and the falling snow) swing round to the new wind at the
        // start of a turn; the snowball only ever feels the new one.
        wind += std::clamp((float)windTarget - wind, -8.0f * dt, 8.0f * dt);

        const uint32_t press = newPresses.exchange(0);
        if (press) {
            anyPressAt = clock;
        }
        for (int s = 0; s < 2; ++s) {
            if (press & (s == 0 ? PAD : AB)) {
                padAt[s] = clock;
                // Its player's here: back in charge at once if the computer
                // hasn't thrown yet, or from its next turn.
                Elf &e = elf[s];
                if (!e.human && state != State::Over) {
                    if (state == State::Aim && cur == s) {
                        e.human = true;
                        if (charging) {
                            sfxStop(SFX);   // the computer's rising note
                        }
                        charging = false;
                        power = 0;
                        if (s == 1) {
                            sfx(SFX, "Join");
                        }
                    } else {
                        e.comeBack = true;
                    }
                }
            }
        }
        if (state != State::Over && clock - anyPressAt > ABANDON_S) {
            // Nobody's touched the controls for a long while: everybody's
            // gone, so end it quietly.
            winner = -1;
            state = State::Over;
            stateT = 0;
            return;
        }

        switch (state) {
        case State::Intro:
            // The elves drop in (settle() does that); then it's red's turn.
            if (stateT > 1.4f) {
                nextTurn();
            }
            return;
        case State::Aim:
            aim(dt, press);
            return;
        case State::Flight:
            fly(dt);
            return;
        case State::Settle: {
            const bool still = std::fabs(elf[0].vy) + std::fabs(elf[1].vy) < 0.01f &&
                               std::fabs(elf[0].y - standY(elf[0])) < 0.05f && std::fabs(elf[1].y - standY(elf[1])) < 0.05f;
            if (stateT > 1.0f && still) {
                for (int s = 0; s < 2; ++s) {
                    if (elf[s].health <= 0) {
                        gameOver(1 - s);
                        return;
                    }
                }
                nextTurn();
            }
            return;
        }
        case State::Over:
            if (stateT > 2.0f && showDue) {
                showDue = false;
                sfxStop(SFX);
                sfxEndShow(SFX);
                const int cpuSide = !elf[0].human ? 0 : (!elf[1].human ? 1 : -1);
                show.start(model, SnowballFightShow::config(winner, elf[winner].throws, elf[winner].health, cpuSide),
                           std::make_unique<SnowballFightShow::Story>(winner));
            }
            return;
        }
    }

    // The elf whose turn it is: a person on its control, or the computer.
    void aim(float dt, uint32_t press) {
        Elf &e = elf[cur];
        const uint32_t h = held.load();
        turnLeft -= dt;
        if (e.human) {
            // The red elf: Left lifts the aim (its tip moves left, toward
            // straight up), Right lowers it. The green one: B swings it, and
            // each press swings it back the other way.
            int dir = 0;
            bool tapped = false;
            if (cur == 0) {
                dir = ((h & LEFT) ? 1 : 0) - ((h & RIGHT) ? 1 : 0);
                tapped = (press & (LEFT | RIGHT)) != 0;
                if (tapped && !dir) {
                    dir = (press & LEFT) ? 1 : -1;   // pressed and let go within a frame
                }
            } else {
                if (press & B_BTN) {
                    if (swungOnce) {
                        e.swing = -e.swing;
                    }
                    swungOnce = true;
                    tapped = true;
                }
                dir = ((h & B_BTN) || tapped) ? e.swing : 0;
            }
            if (tapped) {
                aimHoldT = 0;
                turnAim(e, (float)dir, true);
            } else if (dir) {
                aimHoldT += dt;
                if (aimHoldT > AIM_REPEAT_S) {
                    turnAim(e, dir * AIM_SPEED * dt);
                }
            }
            if (cur == 1 && (e.angle <= 0.0f || e.angle >= 90.0f) && dir) {
                e.swing = e.angle <= 0.0f ? 1 : -1;   // bounces off the ends
            }
            const uint32_t chargeBit = cur == 0 ? UP : A_BTN;
            if (!charging && (press & chargeBit)) {
                startCharge();
                e.hinted = true;
            }
            if (charging) {
                power = std::min(1.0f, power + dt / CHARGE_S);
                if (!(h & chargeBit) || power >= 1.0f) {
                    throwBall();
                    return;
                }
            }
            // Two people playing, and this one has walked off: the computer
            // takes the turn and the elf, until its control is pressed again.
            if (!charging && elf[1 - cur].human && clock - turnStart > IDLE_TAKEOVER_S && padAt[cur] < turnStart) {
                e.human = false;
                planCpu();
            }
        } else {
            cpuStep(dt);
            if (state != State::Aim) {
                return;
            }
        }
        // The last five seconds tick.
        if (e.human && turnLeft < ticked && ticked <= 5 && ticked > 0) {
            sfx(SFX, "Tick", 0.5f);
        }
        while (ticked > turnLeft && ticked > 0) {
            --ticked;
        }
        if (turnLeft <= 0) {
            if (charging) {
                throwBall();   // out of time with it wound up: away it goes
                return;
            }
            sfx(SFX, "TimeUp");
            word = "TIME!";
            wordT = 1.0f;
            state = State::Settle;
            stateT = 0;
        }
    }

    // tap: a single press, which always ticks.
    void turnAim(Elf &e, float by, bool tap = false) {
        const float was = e.angle;
        e.angle = std::clamp(e.angle + by, 0.0f, 90.0f);
        // A soft tick every few degrees, so the aim can be felt as well as seen.
        if (e.angle != was && (tap || std::fabs(e.angle - aimSoundAt) >= 4.0f)) {
            aimSoundAt = e.angle;
            sfx(SFX, "Aim", 0.35f);
        }
    }

    void startCharge() {
        charging = true;
        power = 0;
        sfx(SFX, "Charge", 0.5f);
    }

    void throwBall() {
        Elf &e = elf[cur];
        charging = false;
        const float v = speedFor(power), a = e.angle * 3.14159265f / 180.0f;
        ball = {handX(e), handY(e), e.facing * v * std::cos(a), -v * std::sin(a), 0.0f, cur};
        ballOn = true;
        simAcc = 0;
        trail.clear();
        e.lastAngle = e.angle;
        e.lastPower = power;
        ++e.throws;
        e.hinted = true;
        sfxStop(SFX);   // the charge's rising note
        sfx(SFX, "Throw");
        state = State::Flight;
        stateT = 0;
    }

    // One fixed step of a snowball's flight. True once it's come down, with
    // where in l. Shared by the real flight and the computer's planning.
    bool advance(Ball &b, Landing &l) const {
        b.vx += windAccel() * SIM_DT;
        b.vy += g * SIM_DT;
        b.x += b.vx * SIM_DT;
        b.y += b.vy * SIM_DT;
        b.t += SIM_DT;
        if (b.x < -2.0f || b.x > W + 2.0f || b.t > 10.0f) {
            l = {b.x, b.y, b.t, -2};
            return true;
        }
        for (int s = 0; s < 2; ++s) {
            const Elf &e = elf[s];
            if (e.health <= 0 || (s == b.owner && b.t < SAFE_S)) {
                continue;
            }
            const float fy = std::round(e.y);
            if (b.x >= e.cx - 1 && b.x < e.cx + 2 && b.y >= fy - 4 && b.y < fy) {
                l = {b.x, b.y, b.t, s};
                return true;
            }
        }
        const int xi = (int)std::floor(b.x);
        if (b.y >= H - 0.5f || (xi >= 0 && xi < W && b.y >= ground[xi])) {
            l = {b.x, std::min(b.y, (float)H - 0.5f), b.t, -1};
            return true;
        }
        return false;
    }

    // How far a landing is from an elf: 0 inside its sprite.
    float distanceTo(const Elf &e, float x, float y) const {
        const float fy = std::round(e.y);
        const float dx = std::max({(float)(e.cx - 1) - x, 0.0f, x - (float)(e.cx + 2)});
        const float dy = std::max({(fy - 4) - y, 0.0f, y - fy});
        return std::hypot(dx, dy);
    }

    void fly(float dt) {
        simAcc += dt;
        Landing l;
        while (simAcc >= SIM_DT) {
            simAcc -= SIM_DT;
            if (advance(ball, l)) {
                land(l);
                return;
            }
        }
        // A short fading trail behind it.
        if (trail.empty() || std::hypot(trail.back().first - ball.x, trail.back().second - ball.y) >= 1.0f) {
            trail.push_back({ball.x, ball.y});
            if (trail.size() > 4) {
                trail.erase(trail.begin());
            }
        }
    }

    void land(const Landing &l) {
        ballOn = false;
        trail.clear();
        state = State::Settle;
        stateT = 0;
        const bool big = giant;
        giant = false;
        if (l.who == -2) {
            sfx(SFX, "Miss");
            return;
        }
        const float r = big ? craterR * 2.2f : craterR;
        crater(l.x, l.y, r);
        bool hit = false;
        for (int s = 0; s < 2; ++s) {
            Elf &e = elf[s];
            if (e.health <= 0 || (s == ball.owner && l.t < SAFE_S)) {
                continue;
            }
            if (l.who == s || distanceTo(e, l.x, l.y) <= (big ? 4.0f : SPLASH)) {
                --e.health;
                e.flashT = 0.5f;
                hit = true;
                burst(e.cx + 0.5f, std::round(e.y) - 2, HAT[s], 14, 9.0f);
                burst(e.cx + 0.5f, std::round(e.y) - 2, WHITE, 10, 9.0f);
                if (e.health <= 0) {
                    sfx(SFX, "KO");
                }
            }
        }
        sfx(SFX, hit ? "Hit" : "Splat");
    }

    // Blows a round hole out of the snow. Any snow above the hole in a
    // column falls into it, so the hills stay a height map: each column
    // simply loses as much snow as the hole overlapped. One row is always
    // left at the bottom.
    void crater(float cx, float cy, float r) {
        for (int x = (int)std::floor(cx - r); x <= (int)std::ceil(cx + r); ++x) {
            if (x < 0 || x >= W) {
                continue;
            }
            const float dx = x + 0.5f - cx;
            if (std::fabs(dx) >= r) {
                continue;
            }
            const float h = std::sqrt(r * r - dx * dx);
            const float overlap = std::min(cy + h, (float)H) - std::max(cy - h, ground[x]);
            if (overlap > 0) {
                ground[x] = std::min((float)H - 1.0f, ground[x] + overlap);
            }
        }
        // Snow sprays up out of it.
        const int n = (int)(8 + r * 4);
        for (int k = 0; k < n; ++k) {
            const float a = -3.14159f * (0.1f + 0.8f * rnd());
            const float sp = 6.0f + rnd() * 10.0f;
            sparks.push_back({cx, cy - 0.5f, std::cos(a) * sp, std::sin(a) * sp, 0.0f, 0.5f + rnd() * 0.5f,
                              {0.8f, 0.88f, 1.0f}, 40.0f});
        }
    }

    // Elves drop when the snow under them goes, landing with a soft thump.
    void settle(float dt) {
        for (auto &e : elf) {
            const float to = standY(e);
            if (e.y < to - 0.01f) {
                e.vy += g * dt;
                e.y = std::min(to, e.y + e.vy * dt);
                if (e.y >= to) {
                    if (e.vy > 8.0f && state != State::Intro) {
                        sfx(SFX, "Land", 0.6f);
                    }
                    if (state == State::Intro) {
                        sfx(SFX, "Land", 0.4f);
                        burst(e.cx + 0.5f, to - 0.5f, {0.8f, 0.88f, 1.0f}, 6, 30.0f);
                    }
                    e.vy = 0;
                }
            } else {
                e.y = to;
                e.vy = 0;
            }
        }
    }

    void gameOver(int win) {
        winner = win;
        state = State::Over;
        stateT = 0;
        sfxStop(SFX);
        // Good: beating the computer, or, between two people, winning with
        // at least goodGame health left.
        const Elf &w = elf[win], &l = elf[1 - win];
        bool good = false;
        if (w.human && !l.human) {
            good = true;
        } else if (w.human && l.human) {
            good = w.health >= goodGame;
        }
        showDue = arcadeWantShow(endShow, good, false) && (w.human || l.human);
        sfx(SFX, w.human ? "Win" : "GameOver");
    }

    // The cheat codes: the next level knocks out the elf whose turn it isn't
    // (whoever's playing wins on the spot); the last level makes the next
    // throw a giant snowball, with a crater to match.
    void useCheat(int cheat) {
        if (cheat == CHEAT_NEXT_LEVEL) {
            Elf &e = elf[1 - cur];
            e.health = 0;
            e.flashT = 0.5f;
            ballOn = false;
            crater(e.cx + 0.5f, std::round(e.y), craterR * 1.5f);
            burst(e.cx + 0.5f, std::round(e.y) - 2, HAT[1 - cur], 20, 9.0f);
            sfx(SFX, "KO");
            state = State::Settle;
            stateT = 0;
        } else if (cheat == CHEAT_LAST_LEVEL) {
            giant = true;
            word = "GIANT!";
            wordT = 1.2f;
            sfx(SFX, "Giant");
        }
    }

    // ---------------------------------------------------------------- the computer
    // Tries throws until it finds the one that lands on the other elf, in
    // this very wind and over these very hills, then spoils it a little -
    // a lot at first, less with every throw it makes, so the first throw or
    // two are sighting shots and it gets dangerous as the fight goes on.
    // Well ahead of a person, it eases off.
    void planCpu() {
        const Elf &me = elf[cur], &them = elf[1 - cur];
        // A favourite angle for this throw, so it doesn't always lob the same.
        const float like = 35.0f + rnd() * 25.0f;
        float bestA = 45, bestP = 0.7f, bestScore = 1e9f;
        auto score = [&](float a, float p) {
            const float v = speedFor(p), r = a * 3.14159265f / 180.0f;
            Ball b = {handX(me), handY(me), me.facing * v * std::cos(r), -v * std::sin(r), 0.0f, cur};
            Landing l;
            while (!advance(b, l)) {
            }
            if (l.who == -2) {
                return 1000.0f + std::fabs(l.x - them.cx);
            }
            if (l.who == cur || (l.t >= SAFE_S && distanceTo(me, l.x, l.y) <= SPLASH)) {
                return 500.0f;   // not itself
            }
            return l.who == 1 - cur ? 0.0f : std::max(0.0f, distanceTo(them, l.x, l.y) - SPLASH * 0.5f);
        };
        for (float a = 12.0f; a <= 78.0f; a += 3.0f) {
            float pBest = 0, sBest = 1e9f;
            for (float p = 0.0f; p <= 1.0f; p += 0.025f) {
                const float sc = score(a, p);
                if (sc < sBest) {
                    sBest = sc;
                    pBest = p;
                }
            }
            for (float p = pBest - 0.025f; p <= pBest + 0.025f; p += 0.005f) {
                const float sc = score(a, std::clamp(p, 0.0f, 1.0f));
                if (sc < sBest) {
                    sBest = sc;
                    pBest = std::clamp(p, 0.0f, 1.0f);
                }
            }
            // Equally good throws: the one nearest the angle it fancies.
            const float total = sBest + std::fabs(a - like) * 0.01f;
            if (total < bestScore) {
                bestScore = total;
                bestA = a;
                bestP = pBest;
            }
        }
        static const float SKILL[5] = {1.8f, 1.35f, 1.0f, 0.75f, 0.5f};
        float wobble = SKILL[cpuSkill - 1] * (1.0f + 2.5f * std::pow(0.6f, (float)me.throws));
        if (me.health - them.health >= 2) {
            wobble *= 1.5f;
        }
        // On a narrow wall the other elf is a bigger share of the distance,
        // so the same slip in power misses by less: slip more.
        const float narrow = std::sqrt(60.0f / std::max(20, W));
        cpu = Cpu();
        cpu.angle = std::clamp(bestA + gauss() * 4.0f * wobble, 5.0f, 85.0f);
        cpu.power = std::clamp(bestP + gauss() * 0.06f * wobble * narrow, 0.05f, 1.0f);
        cpu.aimSoundAt = me.angle;
    }

    void cpuStep(float dt) {
        Elf &e = elf[cur];
        cpu.t += dt;
        switch (cpu.phase) {
        case 0:   // thinking
            if (cpu.t > 0.35f) {
                cpu.phase = 1;
            }
            break;
        case 1: {   // swinging the aim round to where it wants it
            const float d = cpu.angle - e.angle;
            e.angle += std::clamp(d, -70.0f * dt, 70.0f * dt);
            if (std::fabs(e.angle - cpu.aimSoundAt) >= 4.0f) {
                cpu.aimSoundAt = e.angle;
                sfx(SFX, "Aim", 0.25f);
            }
            if (std::fabs(d) < 0.01f) {
                cpu.phase = 2;
                cpu.t = 0;
            }
            break;
        }
        default:   // a beat, then charging up to the power it chose
            if (cpu.t < 0.15f) {
                break;
            }
            if (!charging) {
                startCharge();
            }
            power += dt / CHARGE_S;
            if (power >= cpu.power) {
                power = cpu.power;
                throwBall();
            }
            break;
        }
    }

    // ---------------------------------------------------------------- effects
    void burst(float x, float y, const RGB &col, int n, float gravity) {
        for (int k = 0; k < n; ++k) {
            const float a = rnd() * 6.2832f, sp = 4.0f + rnd() * 8.0f;
            sparks.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp, 0.0f, 0.4f + rnd() * 0.4f, col, gravity});
        }
    }
    void stepSparks(float dt) {
        for (auto &s : sparks) {
            s.age += dt;
            s.vy += s.gravity * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark &s) { return s.age >= s.life; }),
                     sparks.end());
    }
    // Falling snow, blown by the wind: the easiest way to see which way it's
    // blowing and how hard.
    void stepFlakes(float dt) {
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x += (wind * windUnit * 0.9f + std::sin(clock * 1.5f + f.phase) * 0.6f) * dt;
            const int xi = (int)std::floor(f.x);
            if (f.y >= H || (xi >= 0 && xi < W && f.y >= ground[xi])) {
                f.y = -rnd() * 2.0f;
                f.x = rnd() * W;
            }
            if (f.x < 0) {
                f.x += W;
            } else if (f.x >= W) {
                f.x -= W;
            }
        }
    }

    // ---------------------------------------------------------------- drawing
    // Light spread over the four LEDs nearest (x, y), so a dot can sit
    // between them and a slowly turning aim line moves smoothly.
    void splat(float x, float y, const RGB &col, float k) {
        const float fx = x - 0.5f, fy = y - 0.5f;
        const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        const float ax = fx - x0, ay = fy - y0;
        canvas.add(x0, y0, col, k * (1 - ax) * (1 - ay));
        canvas.add(x0 + 1, y0, col, k * ax * (1 - ay));
        canvas.add(x0, y0 + 1, col, k * (1 - ax) * ay);
        canvas.add(x0 + 1, y0 + 1, col, k * ax * ay);
    }

    // From the hand out along the angle: four LEDs long while aiming, out to
    // thirteen at full power, yellow turning red as it charges.
    void drawAimLine(const Elf &e, float angle, float pw, bool live, float k) {
        const float a = angle * 3.14159265f / 180.0f;
        const float dx = e.facing * std::cos(a), dy = -std::sin(a);
        const float len = 4.0f + 9.0f * pw;
        const RGB col = live && charging ? ArcadeCanvas::mix(POWER_LO, POWER_HI, pw) : AIM_IDLE;
        for (float d = 1.0f; d <= len + 0.01f; d += 1.0f) {
            // Idle, a glint runs out along it so it reads as "aim here".
            float b = live && charging ? 0.75f : 0.45f;
            if (live && !charging) {
                b += 0.35f * std::max(0.0f, std::cos((d - std::fmod(clock * 6.0f, len + 3.0f)) * 1.2f));
            }
            splat(handX(e) + dx * d, handY(e) + dy * d, col, b * k);
        }
    }

    void render() {
        canvas.clear();
        // The night: a black sky with stars and the moon.
        for (auto &s : stars) {
            if (s.y < ground[s.x] - 1) {
                canvas.add(s.x, s.y, {0.9f, 0.9f, 1.0f}, 0.1f + 0.08f * std::sin(clock * 2.0f + s.phase));
            }
        }
        for (int y = (int)(moonY - moonR - 1); y <= (int)(moonY + moonR + 1); ++y) {
            for (int x = (int)(moonX - moonR - 1); x <= (int)(moonX + moonR + 1); ++x) {
                const float in = ArcadeCanvas::clamp01(moonR - std::hypot(x + 0.5f - moonX, y + 0.5f - moonY) + 0.5f);
                canvas.add(x, y, MOON, 0.4f * in);
            }
        }
        // The snow: soft-edged on top, darker deeper down, with now and then
        // a sparkle on the surface.
        for (int x = 0; x < W; ++x) {
            const float g0 = ground[x];
            for (int y = std::max(0, (int)std::floor(g0)); y < H; ++y) {
                const float cover = ArcadeCanvas::clamp01(y + 1 - g0);
                const float depth = std::clamp((y - g0) / 1.5f, 0.0f, 1.0f);
                canvas.set(x, y, ArcadeCanvas::mix(SNOW_TOP, SNOW_DEEP, depth), cover);
            }
            const int top = (int)std::floor(g0);
            const float tw = std::sin(clock * 0.9f + x * 12.9898f) * 43758.5f;
            if (tw - std::floor(tw) > 0.985f && top >= 0 && top < H) {
                canvas.add(x, top, WHITE, 0.3f);
            }
        }
        for (auto &f : flakes) {
            canvas.add((int)std::floor(f.x), (int)std::floor(f.y), WHITE, 0.16f);
        }

        // Last throw's aim, faintly, to adjust from; then the live one.
        if (state == State::Aim) {
            const Elf &e = elf[cur];
            if (e.human && e.lastPower >= 0) {
                drawAimLine(e, e.lastAngle, e.lastPower, false, 0.25f);
            }
            drawAimLine(e, e.angle, charging ? power : 0.0f, true, 1.0f);
        }

        // The elves: the one whose turn it is blinks its hat as the turn
        // starts; the winner hops; a knocked-out one lies flat with stars
        // going round its head.
        for (int s = 0; s < 2; ++s) {
            const Elf &e = elf[s];
            int fy = feet(e);
            if (state == State::Over && s == winner && std::fmod(stateT, 0.5f) < 0.25f) {
                fy -= 1;
            }
            const bool blink = state == State::Aim && s == cur && stateT < 0.8f && std::fmod(stateT, 0.2f) < 0.1f;
            const float flash = e.flashT > 0 && std::fmod(e.flashT, 0.16f) < 0.08f ? 0.8f : 0.0f;
            SnowballFightShow::drawElf(canvas, e.cx, fy, e.facing, HAT[s], 1.0f, e.health <= 0, flash, blink ? 0.7f : 0.0f);
            if (e.health <= 0) {
                const float a = clock * 5.0f;
                splat(e.cx + 0.5f - e.facing * 1.5f + std::cos(a) * 1.5f, fy - 2.5f + std::sin(a) * 0.6f, {1.0f, 0.9f, 0.3f}, 0.7f);
            } else if (state == State::Aim && s == cur) {
                canvas.set((int)std::floor(handX(e)), (int)std::floor(handY(e)), WHITE, 0.8f);   // the snowball in hand
            }
            drawPips(e, s);
        }

        if (ballOn) {
            for (size_t i = 0; i < trail.size(); ++i) {
                splat(trail[i].first, trail[i].second, WHITE, 0.08f + 0.08f * i);
            }
            if (ball.y < 0) {
                // Up over the top: a marker on the top row shows where it is.
                if (std::fmod(clock, 0.2f) < 0.12f) {
                    canvas.add((int)std::floor(ball.x), 0, WHITE, 0.5f);
                }
            } else if (giant) {
                for (int dy = -1; dy <= 0; ++dy) {
                    for (int dx = -1; dx <= 0; ++dx) {
                        canvas.set((int)std::floor(ball.x) + dx + 1, (int)std::floor(ball.y) + dy + 1, WHITE, 1.0f);
                    }
                }
            } else {
                canvas.set((int)std::floor(ball.x), (int)std::floor(ball.y), WHITE, 1.0f);
            }
        }
        for (auto &s : sparks) {
            canvas.add((int)std::floor(s.x), (int)std::floor(s.y), s.col, 0.8f * (1.0f - s.age / s.life));
        }
        drawWind();
        drawTimer();
        canvas.writeTo(model);

        if (state == State::Over && stateT > 0.8f) {
            if (winner < 0) {
                textBox({"GAME", "OVER"}, 255, 255, 255);
            } else {
                const RGB &c = HAT[winner];
                textBox({SnowballFightShow::NAME[winner], "WINS!"}, (int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255));
            }
        } else if (wordT > 0) {
            textBox({word}, 255, 255, 255);
        } else if (state == State::Aim && elf[cur].human && !elf[cur].hinted && stateT > 0.6f) {
            drawHint();
        }
    }

    // Health pips over an elf's head, in its colour; lost ones dim.
    void drawPips(const Elf &e, int s) {
        const int n = startHealth, gap = n <= 5 ? 2 : 1;
        const int y = feet(e) - 7;   // two rows clear of the bobble, so they don't look like part of the hat
        const int x0 = std::clamp(e.cx - (n - 1) * gap / 2, 0, std::max(0, W - 1 - (n - 1) * gap));
        for (int i = 0; i < n; ++i) {
            // The red elf's run out from the right, the green one's from the
            // left, both from the middle of the wall.
            const bool full = s == 0 ? i < e.health : (n - 1 - i) < e.health;
            canvas.add(x0 + i * gap, y, full ? HAT[s] : RGB{0.3f, 0.3f, 0.35f}, full ? 0.85f : 0.3f);
        }
    }

    // The wind in the middle of the top: an arrow as long as it's strong,
    // with light running along it the way it blows, and a chevron head.
    // Calm is a single dot.
    void drawWind() {
        const int cx = W / 2, y = 2;
        const int level = (int)std::lround(wind);
        if (level == 0) {
            canvas.add(cx, y, WIND, 0.35f);
            return;
        }
        const int dir = level > 0 ? 1 : -1;
        // At least four long, so even a breeze reads as an arrow, not a cross.
        const int len = std::abs(level) * (W >= 40 ? 2 : 1) + (W >= 40 ? 2 : 3);
        const int start = cx - dir * (len / 2);
        for (int i = 0; i < len; ++i) {
            const int x = start + dir * i;
            const float run = 0.5f + 0.5f * std::sin(i * 1.1f - clock * 9.0f);
            canvas.add(x, y, WIND, 0.25f + 0.35f * run);
        }
        const int tip = start + dir * (len - 1);
        canvas.add(tip, y, WIND, 0.3f);
        for (int i = 1; i <= 2; ++i) {
            canvas.add(tip - dir * i, y - i, WIND, 0.55f);
            canvas.add(tip - dir * i, y + i, WIND, 0.55f);
        }
    }

    // The turn timer along the top row, from the thrower's edge toward the
    // middle; red and blinking for its last five seconds.
    void drawTimer() {
        if (state != State::Aim || !elf[cur].human) {
            return;
        }
        const int full = std::max(3, W / 2 - 6);
        const int n = (int)std::ceil(std::max(0.0f, turnLeft) / turnTime * full);
        const bool late = turnLeft < 5.0f;
        if (late && std::fmod(turnLeft, 0.5f) < 0.2f) {
            return;
        }
        for (int i = 0; i < n; ++i) {
            const int x = cur == 0 ? i : W - 1 - i;
            canvas.add(x, 0, late ? POWER_HI : HAT[cur], late ? 0.6f : 0.3f);
        }
    }

    // How to throw, in the sky over the thrower, on its player's first turn
    // until they've charged: HOLD UP, or B AIMS and HOLD A in turn.
    void drawHint() {
        const bool red = cur == 0;
        const std::string s = red ? "HOLD UP" : (std::fmod(stateT, 2.4f) < 1.2f ? "B AIMS" : "HOLD A");
        const int w = (int)s.size() * 4 - 1;
        int x = red ? 1 : W - 1 - w;
        if (w > W / 2) {
            x = (W - w) / 2;
        }
        const int y = std::max(3, std::min(feet(elf[0]), feet(elf[1])) - 14);
        PanelTransform pt(this);
        const RGB &c = HAT[cur];
        outputString(s, x, y, (int)(c.r * 200), (int)(c.g * 200), (int)(c.b * 200), 1);
    }

    int32_t finish() {
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();
        finished = true;
        return EFFECT_AFTER_NEXT_OUTPUT;
    }

    int endShow = 1, goodGame = 2;   // the End Show and Good Game (health left) settings
    ArcadeShowPlayer show;
    bool showDue = false;

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 0, H = 0;
    int startHealth = 3, windMax = 5, cpuSkill = 3;
    float turnTime = 15.0f;
    float g = 55, vMax = 60, windUnit = 1.3f, craterR = 2.4f;

    State state = State::Intro;
    float stateT = 0, clock = 0;
    std::vector<float> ground;   // the top of the snow in each column, in rows from the top
    Elf elf[2];
    int cur = 0, winner = -1;
    int windTarget = 0;   // this turn's wind, -windMax .. windMax
    float wind = 0;       // the wind as the arrow shows it, easing to windTarget

    float turnLeft = 15, turnStart = 0, power = 0, aimHoldT = 0, aimSoundAt = 0;
    int ticked = 15;
    bool charging = false, swungOnce = false;
    Cpu cpu;
    float padAt[2] = {0, 0}, anyPressAt = 0;

    Ball ball{0, 0, 0, 0, 0, 0};
    bool ballOn = false, giant = false;
    float simAcc = 0;
    std::vector<std::pair<float, float>> trail;

    std::vector<Spark> sparks;
    std::vector<Flake> flakes;
    std::vector<Star> stars;
    float moonX = 0, moonY = 0, moonR = 2;
    std::string word;
    float wordT = 0;

    std::atomic<uint32_t> held{0}, newPresses{0};
    bool finished = false;
};


const std::string &FPPSnowballFight::getName() {
    static const std::string name = "Snowball Fight";
    return name;
}

void FPPSnowballFight::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    SnowballFightEffect *effect = dynamic_cast<SnowballFightEffect *>(m->getRunningEffect());
    if (!effect) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new SnowballFightEffect(m, findIntOption("Hits", 3), findIntOption("Turn Time", 15),
                                         findIntOption("Wind", 5), findIntOption("Computer Skill", 3), endShowOption(),
                                         goodGameOption(2));
        m->setRunningEffect(effect, FRAME_MS);
        // The press that started it only starts it: a lone player's A
        // shouldn't count as a second player arriving.
        return;
    }
    effect->button(button);
}
