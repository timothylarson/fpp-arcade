#include <fpp-pch.h>

#include "FPPBarrelClimb.h"
#include "ArcadeCanvas.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;

// Girders are one LED thick, five rows apart, each with a one-row step in the
// middle so it slopes. They alternate direction, with a gap at the low end
// that barrels drop through to the girder below. The climber is two LEDs
// tall (head and body) and a barrel is one. Positions are whole LEDs; speeds
// are LEDs per second, stepped one LED at a time.

namespace {

const RGB GIRDER = {1.0f, 0.15f, 0.35f};
const RGB LADDER = {0.1f, 0.8f, 1.0f};
const RGB APE = {0.6f, 0.3f, 0.08f};
const RGB FACE = {1.0f, 0.7f, 0.45f};
const RGB HEAD = {1.0f, 0.72f, 0.5f};
const RGB BODY = {1.0f, 0.08f, 0.08f};
const RGB DRESS = {1.0f, 0.3f, 0.75f};
const RGB BARREL = {0.95f, 0.5f, 0.1f};

enum : uint32_t { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8 };

constexpr float WALK = 7.0f;
constexpr float CLIMB = 5.0f;
constexpr float FALL = 14.0f;
constexpr float JUMP_TIME = 0.6f;

}  // namespace

class BarrelClimbEffect : public FPPArcadeGameEffect {
public:
    BarrelClimbEffect(PixelOverlayModel *m, int lives) : FPPArcadeGameEffect(m), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(16, W);
        H = std::max(14, H);
        canvas.resize(W, H);
        for (int y = H - 1; y >= 4; y -= 5) {
            base.push_back(y);
        }
        top = (int)base.size() - 1;
        startLives = std::clamp(lives, 1, 9);
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "BarrelClimb";
        return NAME;
    }

    void button(const std::string &b) override {
        const bool pressed = b.size() > 8 && b.compare(b.size() - 8, 8, " Pressed") == 0;
        const bool released = b.size() > 9 && b.compare(b.size() - 9, 9, " Released") == 0;
        if (pressed || released) {
            const std::string n = b.substr(0, b.find(" - "));
            uint32_t bits = 0;
            if (n == "Up") bits = UP;
            else if (n == "Down") bits = DOWN;
            else if (n == "Left") bits = LEFT;
            else if (n == "Right") bits = RIGHT;
            else if (n == "Up/Left") bits = UP | LEFT;
            else if (n == "Up/Right") bits = UP | RIGHT;
            else if (n == "Down/Left") bits = DOWN | LEFT;
            else if (n == "Down/Right") bits = DOWN | RIGHT;
            if (pressed) {
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
        if (b == "A Button - Pressed" || b == "B Button - Pressed" || b == "Fire - Pressed") {
            jumpNow.store(true);
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
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return FRAME_MS;
        }
        const float dt = (float)(consumeElapsedMs(FRAME_MS, 100.0) / 1000.0);
        clock += dt;
        stateT += dt;
        step(dt);
        if (state == State::Over && stateT > 5.0f) {
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            finished = true;
            return EFFECT_AFTER_NEXT_OUTPUT;
        }
        render();
        model->setOverlayBufferDirty(true);
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 25;
    enum class State { Intro, Play, Dying, Clear, Over };
    enum class Mode { Ground, Climb, Jump, Fall };
    enum class BMode { Roll, Fall, Ladder };

    struct Ladder {
        int x, k;   // joins girder k to girder k + 1
    };
    struct Barrel {
        int x, y, k, dir;
        float acc;
        BMode mode;
        bool scored;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    // ---------------------------------------------------------------- the level
    // Barrels roll right on the top girder, and each girder below the other way.
    bool rollsRight(int k) const { return ((top - k) % 2) == 0; }
    int spanLo(int k) const { return (k == 0 || rollsRight(k)) ? 0 : 3; }
    int spanHi(int k) const { return (k == 0 || !rollsRight(k)) ? W - 1 : W - 4; }
    bool onSpan(int k, int x) const { return x >= spanLo(k) && x <= spanHi(k); }
    // The row of girder k's LEDs at column x: a step up on its high half.
    int surface(int k, int x) const {
        if (k == 0) {
            return base[0];
        }
        const bool high = rollsRight(k) ? x < W / 2 : x >= W / 2;
        return base[k] - (high ? 1 : 0);
    }
    const Ladder *ladderAt(int x, int k) const {
        for (auto &l : ladders) {
            if (l.x == x && l.k == k) {
                return &l;
            }
        }
        return nullptr;
    }

    void buildLadders() {
        ladders.clear();
        const int perLevel = W >= 40 ? 2 : 1;
        std::vector<int> below;
        for (int k = 0; k < top; ++k) {
            const int lo = std::max(spanLo(k), spanLo(k + 1)) + 2, hi = std::min(spanHi(k), spanHi(k + 1)) - 2;
            std::vector<int> here;
            for (int tries = 0; tries < 60 && (int)here.size() < perLevel; ++tries) {
                const int x = lo + (int)(rnd() * (hi - lo + 1));
                bool ok = x != W / 2 && x != W / 2 - 1;   // not on the step
                for (int o : here) ok = ok && std::abs(o - x) >= 8;
                for (int o : below) ok = ok && std::abs(o - x) >= 4;   // make them walk between ladders
                if (ok) {
                    here.push_back(x);
                }
            }
            if (here.empty()) {
                here.push_back(rollsRight(k + 1) ? hi : lo);
            }
            for (int x : here) {
                ladders.push_back({x, k});
            }
            below = here;
        }
        apeX = rollsRight(top) ? 1 : W - 4;
        goalX = rollsRight(top) ? std::min(spanHi(top) - 1, W * 3 / 5) : std::max(spanLo(top) + 1, W * 2 / 5);
    }

    // ---------------------------------------------------------------- game flow
    void newGame() {
        level = 1;
        score = 0;
        lives = startLives;
        startLevel();
    }

    void startLevel() {
        buildLadders();
        resetRun();
        state = State::Intro;
        stateT = 0;
    }

    void resetRun() {
        barrels.clear();
        px = rollsRight(0) ? 1 : W - 2;
        pk = 0;
        feet = surface(0, px) - 1;
        mode = Mode::Ground;
        pacc = 0;
        throwT = 1.0f;
        bonus = 3000;
        bonusT = 0;
        jumpNow.store(false);
    }

    void die() {
        state = State::Dying;
        stateT = 0;
    }

    void step(float dt) {
        switch (state) {
        case State::Intro:
            if (stateT > 1.8f) {
                state = State::Play;
                stateT = 0;
            }
            return;
        case State::Dying:
            if (stateT > 1.6f) {
                if (--lives > 0) {
                    resetRun();
                    state = State::Play;
                    stateT = 0;
                } else {
                    state = State::Over;
                    stateT = 0;
                }
            }
            return;
        case State::Clear:
            if (stateT > 2.5f) {
                ++level;
                startLevel();
            }
            return;
        case State::Over:
            return;
        case State::Play:
            break;
        }
        bonusT += dt;
        if (bonusT >= 2.0f) {
            bonusT = 0;
            bonus = std::max(0, bonus - 100);
        }

        // The ape: arms up, then a barrel.
        throwT -= dt;
        if (throwT <= 0) {
            throwT = std::max(1.1f, 3.2f - 0.3f * (level - 1)) * (0.8f + 0.4f * rnd());
            const int x = rollsRight(top) ? apeX + 3 : apeX - 1;
            barrels.push_back({x, surface(top, x) - 1, top, rollsRight(top) ? 1 : -1, 0, BMode::Roll, false});
        }

        movePlayer(dt);
        if (state != State::Play) {
            return;
        }
        moveBarrels(dt);
        if (state == State::Play && mode == Mode::Ground && pk == top) {
            score += bonus;
            state = State::Clear;
            stateT = 0;
        }
    }

    bool hitsPlayer(int x, int y) const { return x == px && (y == feet || y == feet - 1); }

    bool touchingBarrel() const {
        for (auto &b : barrels) {
            if (hitsPlayer(b.x, b.y)) {
                return true;
            }
        }
        return false;
    }

    void movePlayer(float dt) {
        const uint32_t h = held.load();
        const int dir = ((h & RIGHT) ? 1 : 0) - ((h & LEFT) ? 1 : 0);
        switch (mode) {
        case Mode::Ground: {
            if ((h & UP) && ladderAt(px, pk)) {
                mode = Mode::Climb;
                climbK = pk;
                pacc = 0;
                break;
            }
            if ((h & DOWN) && pk > 0 && ladderAt(px, pk - 1)) {
                mode = Mode::Climb;
                climbK = pk - 1;
                pacc = 0;
                break;
            }
            if (jumpNow.exchange(false)) {
                mode = Mode::Jump;
                jumpT = 0;
                jumpDir = dir;
                pacc = 0;
                break;
            }
            if (dir) {
                pacc += WALK * dt;
                while (pacc >= 1.0f) {
                    pacc -= 1.0f;
                    walkTo(px + dir);
                    if (mode != Mode::Ground) {
                        break;
                    }
                }
            } else {
                pacc = 0;
            }
            break;
        }
        case Mode::Climb: {
            jumpNow.store(false);
            const int upTo = surface(climbK + 1, px) - 1, downTo = surface(climbK, px) - 1;
            const int v = (h & UP) ? -1 : ((h & DOWN) ? 1 : 0);
            if (!v) {
                pacc = 0;
                break;
            }
            pacc += CLIMB * dt;
            while (pacc >= 1.0f) {
                pacc -= 1.0f;
                feet += v;
                if (feet <= upTo) {
                    feet = upTo;
                    pk = climbK + 1;
                    mode = Mode::Ground;
                    break;
                }
                if (feet >= downTo) {
                    feet = downTo;
                    pk = climbK;
                    mode = Mode::Ground;
                    break;
                }
            }
            break;
        }
        case Mode::Jump: {
            jumpT += dt;
            if (jumpDir) {
                pacc += WALK * dt;
                while (pacc >= 1.0f) {
                    pacc -= 1.0f;
                    px = std::clamp(px + jumpDir, 0, W - 1);
                }
            }
            const int lift = (int)std::lround(2.2f * std::sin(3.14159f * std::min(1.0f, jumpT / JUMP_TIME)));
            if (jumpT >= JUMP_TIME) {
                if (onSpan(pk, px)) {
                    feet = surface(pk, px) - 1;
                    mode = Mode::Ground;
                } else {
                    startFall();
                }
            } else {
                feet = surface(pk, px) - 1 - lift;
                // Jumped clean over a barrel on this girder: 100.
                for (auto &b : barrels) {
                    if (!b.scored && b.mode == BMode::Roll && b.k == pk && b.x == px && b.y > feet) {
                        b.scored = true;
                        score += 100;
                    }
                }
            }
            break;
        }
        case Mode::Fall: {
            pacc += FALL * dt;
            while (pacc >= 1.0f && mode == Mode::Fall) {
                pacc -= 1.0f;
                ++feet;
                for (int k = 0; k <= top; ++k) {
                    if (onSpan(k, px) && feet == surface(k, px) - 1) {
                        pk = k;
                        mode = Mode::Ground;
                        if (feet - fallFrom > 3) {
                            die();   // too far
                            return;
                        }
                    }
                }
                if (feet >= H) {
                    die();
                    return;
                }
            }
            break;
        }
        }
        if (touchingBarrel()) {
            die();
        }
    }

    void walkTo(int x) {
        if (x < 0 || x >= W) {
            return;
        }
        px = x;
        if (!onSpan(pk, px)) {
            startFall();
            return;
        }
        feet = surface(pk, px) - 1;
    }

    void startFall() {
        mode = Mode::Fall;
        fallFrom = feet;
        pacc = 0;
    }

    void moveBarrels(float dt) {
        const float rollSpeed = std::min(16.0f, 8.0f + 0.7f * (level - 1));
        for (size_t j = 0; j < barrels.size();) {
            Barrel &b = barrels[j];
            const float speed = b.mode == BMode::Roll ? rollSpeed : (b.mode == BMode::Fall ? FALL : 6.0f);
            b.acc += speed * dt;
            bool gone = false;
            while (b.acc >= 1.0f && !gone) {
                b.acc -= 1.0f;
                if (b.mode == BMode::Roll) {
                    // At the top of a ladder down, sometimes take it.
                    if (b.k > 0 && ladderAt(b.x, b.k - 1) && rnd() < 0.3f) {
                        b.mode = BMode::Ladder;
                        continue;
                    }
                    b.x += b.dir;
                    if (b.x < 0 || b.x >= W) {
                        gone = true;   // off the end of the floor
                    } else if (!onSpan(b.k, b.x)) {
                        b.mode = BMode::Fall;
                    } else {
                        b.y = surface(b.k, b.x) - 1;
                    }
                } else {
                    ++b.y;
                    const int k = b.k - 1;
                    if (k < 0 || b.y >= H) {
                        gone = true;
                    } else if (onSpan(k, b.x) && b.y >= surface(k, b.x) - 1) {
                        b.y = surface(k, b.x) - 1;
                        b.k = k;
                        b.mode = BMode::Roll;
                        b.dir = rollsRight(k) ? 1 : -1;
                    }
                }
                if (!gone && hitsPlayer(b.x, b.y)) {
                    die();
                    return;
                }
            }
            if (gone) {
                barrels.erase(barrels.begin() + j);
            } else {
                ++j;
            }
        }
    }

    // ---------------------------------------------------------------- drawing
    void render() {
        canvas.clear();
        for (int k = 0; k <= top; ++k) {
            for (int x = spanLo(k); x <= spanHi(k); ++x) {
                canvas.add(x, surface(k, x), GIRDER, (x % 3 == 0) ? 0.55f : 0.35f);
            }
        }
        for (auto &l : ladders) {
            for (int y = surface(l.k + 1, l.x) + 1; y < surface(l.k, l.x); ++y) {
                canvas.add(l.x, y, LADDER, 0.3f);
            }
        }
        // The ape, with its arms up just before a throw.
        const int ay = surface(top, apeX) - 3;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                canvas.set(apeX + x, ay + y, (y == 0 && x == 1) ? FACE : APE, 0.8f);
            }
        }
        if (state == State::Play && throwT < 0.35f) {
            canvas.set(apeX - 1, ay, APE, 0.8f);
            canvas.set(apeX + 3, ay, APE, 0.8f);
        }
        // Who you're climbing for.
        const int gy = surface(top, goalX) - 1;
        canvas.set(goalX, gy, DRESS, 0.9f);
        canvas.set(goalX, gy - 1, HEAD, 0.9f);
        if (std::fmod(clock, 2.0f) < 0.5f && gy - 2 >= 0) {
            canvas.set(goalX + (rollsRight(top) ? 1 : -1), gy - 2, DRESS, 0.6f);
        }
        for (auto &b : barrels) {
            canvas.set(b.x, b.y, BARREL, (b.x & 1) ? 0.9f : 0.6f);
        }
        const bool showPlayer = state != State::Over && !(state == State::Dying && std::fmod(stateT, 0.2f) < 0.1f);
        if (showPlayer) {
            canvas.set(px, feet, mode == Mode::Climb ? RGB{0.2f, 0.3f, 1.0f} : BODY, 1.0f);
            canvas.set(px, feet - 1, HEAD, 1.0f);
        }
        canvas.writeTo(model);

        if (state == State::Intro) {
            textBox({"LEVEL", std::to_string(level)}, 255, 255, 255);
        } else if (state == State::Clear && stateT > 0.6f) {
            textBox({std::to_string(score)}, 255, 220, 0);
        } else if (state == State::Over && stateT > 1.0f) {
            textBox({"GAME", "OVER", std::to_string(score)}, 255, 255, 255);
        }
    }

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 0, H = 0, top = 0;
    std::vector<int> base;
    std::vector<Ladder> ladders;
    int apeX = 1, goalX = 0;

    State state = State::Intro;
    float stateT = 0, clock = 0, throwT = 0, bonusT = 0;
    int level = 1, score = 0, lives = 3, startLives = 3, bonus = 3000;

    int px = 0, pk = 0, feet = 0, climbK = 0, jumpDir = 0, fallFrom = 0;
    Mode mode = Mode::Ground;
    float pacc = 0, jumpT = 0;
    std::vector<Barrel> barrels;

    std::atomic<uint32_t> held{0};
    std::atomic<bool> jumpNow{false};
    bool finished = false;
};


const std::string &FPPBarrelClimb::getName() {
    static const std::string name = "Barrel Climb";
    return name;
}

void FPPBarrelClimb::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    BarrelClimbEffect *effect = dynamic_cast<BarrelClimbEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new BarrelClimbEffect(m, findIntOption("Lives", 3));
        m->setRunningEffect(effect, 25);
    }
    // The starting press counts (a held direction walks straight away) -
    // except Start, which would pause a game that has only just begun.
    if (!starting || button.compare(0, 5, "Start") != 0) {
        effect->button(button);
    }
}
