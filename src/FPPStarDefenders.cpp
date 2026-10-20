#include <fpp-pch.h>

#include "FPPStarDefenders.h"
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

// Aliens are 3x2 sprites with two frames, five LEDs apart in rows three apart,
// and the whole formation steps one LED at a time - faster as it thins out -
// dropping a row at each edge. Shields lose an LED to every shot that hits
// them, from either side.

namespace {

// Three kinds, two frames each, rows top to bottom.
const char *SPRITES[3][2][2] = {
    {{".#.", "#.#"}, {".#.", ".#."}},
    {{"#.#", "###"}, {"###", "#.#"}},
    {{"###", "#.#"}, {"###", ".#."}},
};
const RGB KIND_COLOUR[3] = {{1.0f, 0.2f, 0.9f}, {0.1f, 0.9f, 1.0f}, {0.3f, 1.0f, 0.2f}};
const int KIND_POINTS[3] = {30, 20, 10};
const RGB CANNON = {0.2f, 1.0f, 0.4f};
const RGB SHIELD = {0.1f, 0.85f, 0.2f};
const RGB SHOT = {1.0f, 1.0f, 0.8f};
const RGB BOMB = {1.0f, 0.35f, 0.1f};
const RGB SAUCER = {1.0f, 0.1f, 0.1f};
const RGB LASER = {1.0f, 0.2f, 0.9f};

enum : uint32_t { LEFT = 1, RIGHT = 2, FIRE = 4 };

}  // namespace

class StarDefendersEffect : public FPPArcadeGameEffect {
public:
    StarDefendersEffect(PixelOverlayModel *m, int lives) : FPPArcadeGameEffect(m), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(20, W);
        H = std::max(16, H);
        canvas.resize(W, H);
        cols = std::clamp((W - 14) / 5, 3, 8);   // leaves room to march
        rows = std::clamp((H - 13) / 3, 2, 5);
        startLives = std::clamp(lives, 1, 9);
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "StarDefenders";
        return NAME;
    }

    void button(const std::string &b) override {
        const bool pressed = b.size() > 8 && b.compare(b.size() - 8, 8, " Pressed") == 0;
        const bool released = b.size() > 9 && b.compare(b.size() - 9, 9, " Released") == 0;
        if (pressed || released) {
            const std::string n = b.substr(0, b.find(" - "));
            uint32_t bits = 0;
            if (n == "Left" || n == "Up/Left" || n == "Down/Left") bits = LEFT;
            else if (n == "Right" || n == "Up/Right" || n == "Down/Right") bits = RIGHT;
            else if (n == "A Button" || n == "Fire") bits = FIRE;
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
        if (b == "A Button - Pressed" || b == "Fire - Pressed" || b == "Up - Pressed") {
            fireNow.store(true);
        } else if (b == "B Button - Pressed") {
            laserNow.store(true);
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

    struct Shot {
        float x, y, vy;
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    // ---------------------------------------------------------------- game flow
    void newGame() {
        wave = 1;
        score = 0;
        lives = startLives;
        startWave();
    }

    void startWave() {
        alive.assign(rows * cols, 1);
        aliensLeft = rows * cols;
        fx = 1;
        fy = 2 + std::min(4, wave - 1);   // each wave starts a little lower
        fdir = 1;
        frame = 0;
        marchT = 0;
        shots.clear();
        bombs.clear();
        saucer = false;
        saucerT = 10 + rnd() * 10;
        laser = 1;
        laserT = -1;
        buildShields();
        cannonX = W / 2 - 1;
        state = State::Intro;
        stateT = 0;
    }

    void buildShields() {
        shield.assign(W * H, 0);
        const int n = std::max(2, W / 15);
        const int y = H - 6;
        for (int k = 0; k < n; ++k) {
            const int cx = (int)((k + 0.5f) * W / n);
            for (int dx = -2; dx <= 2; ++dx) {
                shield[y * W + cx + dx] = 1;
                if (std::abs(dx) != 0) {
                    shield[(y + 1) * W + cx + dx] = 1;   // an arch: open under the middle
                }
            }
        }
    }

    int formationW() const { return cols * 5 - 2; }
    int alienX(int c) const { return fx + c * 5; }
    int alienY(int r) const { return fy + r * 3; }
    int kindOf(int r) const { return r == 0 ? 0 : (r <= (rows - 1) / 2 ? 1 : 2); }
    float marchEvery() const { return 0.04f + 0.55f * (float)aliensLeft / (rows * cols) / (1.0f + 0.15f * (wave - 1)); }

    void burst(float x, float y, const RGB &col, int n) {
        for (int k = 0; k < n; ++k) {
            const float a = rnd() * 6.2831853f, sp = 4.0f + rnd() * 8.0f;
            sparks.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp, 0.0f, 0.3f + rnd() * 0.4f, col});
        }
    }

    void step(float dt) {
        stepSparks(dt);
        switch (state) {
        case State::Intro:
            if (stateT > 1.8f) {
                state = State::Play;
                stateT = 0;
                fireNow.store(false);   // the press that started the game isn't a shot
                laserNow.store(false);
            }
            return;
        case State::Dying:
            if (stateT > 1.5f) {
                if (--lives > 0) {
                    bombs.clear();
                    shots.clear();
                    state = State::Play;
                    stateT = 0;
                } else {
                    state = State::Over;
                    stateT = 0;
                }
            }
            return;
        case State::Clear:
            if (stateT > 2.0f) {
                ++wave;
                startWave();
            }
            return;
        case State::Over:
            return;
        case State::Play:
            break;
        }

        // The cannon.
        const uint32_t h = held.load();
        const int dir = ((h & RIGHT) ? 1 : 0) - ((h & LEFT) ? 1 : 0);
        cannonAcc += dir ? 12.0f * dt : 0;
        if (!dir) {
            cannonAcc = 0;
        }
        while (cannonAcc >= 1.0f) {
            cannonAcc -= 1.0f;
            cannonX = std::clamp(cannonX + dir, 0, W - 3);
        }
        fireT -= dt;
        if ((fireNow.exchange(false) || ((h & FIRE) && fireT <= 0)) && shots.size() < 2 && fireT <= 0) {
            shots.push_back({cannonX + 1.0f, (float)H - 3, -30.0f});
            fireT = 0.35f;
        }
        if (laserNow.exchange(false) && laser > 0) {
            --laser;
            laserT = 0.35f;
            laserX = cannonX + 1;
            fireLaser();
        }
        laserT -= dt;

        // The formation: one step at a time, down a row at the edges.
        marchT += dt;
        if (marchT >= marchEvery()) {
            marchT = 0;
            frame ^= 1;
            int lo = W, hi = -1;
            for (int c = 0; c < cols; ++c) {
                for (int r = 0; r < rows; ++r) {
                    if (alive[r * cols + c]) {
                        lo = std::min(lo, alienX(c));
                        hi = std::max(hi, alienX(c) + 2);
                    }
                }
            }
            if ((fdir > 0 && hi + 1 >= W) || (fdir < 0 && lo - 1 < 0)) {
                fdir = -fdir;
                ++fy;
            } else {
                fx += fdir;
            }
        }
        // Landed: the formation's bottom row reached the cannon.
        for (int r = rows - 1; r >= 0; --r) {
            bool any = false;
            for (int c = 0; c < cols; ++c) {
                any = any || alive[r * cols + c];
            }
            if (any) {
                if (alienY(r) + 1 >= H - 2) {
                    lives = 1;
                    die();
                    return;
                }
                // Aliens wipe out the shields they march through.
                for (int c = 0; c < cols; ++c) {
                    if (alive[r * cols + c]) {
                        for (int y = alienY(r); y <= alienY(r) + 1; ++y) {
                            for (int x = alienX(c); x <= alienX(c) + 2; ++x) {
                                if (x >= 0 && x < W && y >= 0 && y < H) {
                                    shield[y * W + x] = 0;
                                }
                            }
                        }
                    }
                }
                break;
            }
        }

        // Bombs from the lowest alien in a random column.
        bombT -= dt;
        if (bombT <= 0 && (int)bombs.size() < 2 + wave / 2) {
            bombT = std::max(0.25f, 1.1f - 0.1f * wave) * (0.5f + rnd());
            const int c = (int)(rnd() * cols) % cols;
            for (int r = rows - 1; r >= 0; --r) {
                if (alive[r * cols + c]) {
                    bombs.push_back({alienX(c) + 1.0f, alienY(r) + 2.0f, std::min(16.0f, 8.0f + wave)});
                    break;
                }
            }
        }

        // The saucer along the top row now and then.
        saucerT -= dt;
        if (!saucer && saucerT <= 0) {
            saucer = true;
            saucerDir = rnd() < 0.5f ? 1 : -1;
            saucerX = saucerDir > 0 ? -3.0f : (float)W;
        }
        if (saucer) {
            saucerX += saucerDir * 9.0f * dt;
            if (saucerX < -4 || saucerX > W + 1) {
                saucer = false;
                saucerT = 12 + rnd() * 12;
            }
        }

        moveShots(dt);
        if (state != State::Play) {
            return;
        }
        if (aliensLeft == 0) {
            score += 100 * wave;
            state = State::Clear;
            stateT = 0;
        }
    }

    // B: a beam straight up from the cannon, three LEDs wide, through
    // everything - aliens, shields, bombs and the saucer.
    void fireLaser() {
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (alive[r * cols + c] && std::abs(alienX(c) + 1 - laserX) <= 2) {
                    killAlien(r, c);
                }
            }
        }
        for (int y = 0; y < H; ++y) {
            for (int x = laserX - 1; x <= laserX + 1; ++x) {
                if (x >= 0 && x < W) {
                    shield[y * W + x] = 0;
                }
            }
        }
        bombs.erase(std::remove_if(bombs.begin(), bombs.end(), [&](const Shot &b) { return std::fabs(b.x - laserX) <= 1.5f; }),
                    bombs.end());
        if (saucer && std::fabs(saucerX + 1 - laserX) <= 2.5f) {
            hitSaucer();
        }
    }

    void killAlien(int r, int c) {
        alive[r * cols + c] = 0;
        --aliensLeft;
        score += KIND_POINTS[kindOf(r)];
        burst(alienX(c) + 1, alienY(r) + 1, KIND_COLOUR[kindOf(r)], 8);
    }

    void hitSaucer() {
        static const int values[] = {50, 100, 150, 300};
        score += values[(int)(rnd() * 4) % 4];
        burst(saucerX + 1, 0, SAUCER, 14);
        saucer = false;
        saucerT = 12 + rnd() * 12;
    }

    void die() {
        burst(cannonX + 1, H - 1, CANNON, 16);
        state = State::Dying;
        stateT = 0;
    }

    // Shots and bombs move one LED at a time so nothing passes through.
    void moveShots(float dt) {
        for (size_t k = 0; k < shots.size();) {
            Shot &s = shots[k];
            bool gone = false;
            const float to = s.y + s.vy * dt;
            while (!gone && s.y > to) {
                s.y = std::max(to, s.y - 1.0f);
                const int x = (int)s.x, y = (int)std::lround(s.y);
                if (y < 0) {
                    gone = true;
                    break;
                }
                if (shield[y * W + x]) {
                    shield[y * W + x] = 0;
                    gone = true;
                    break;
                }
                if (saucer && y <= 0 && x >= (int)std::floor(saucerX) && x <= (int)std::floor(saucerX) + 2) {
                    hitSaucer();
                    gone = true;
                    break;
                }
                for (int r = 0; r < rows && !gone; ++r) {
                    for (int c = 0; c < cols && !gone; ++c) {
                        if (alive[r * cols + c] && x >= alienX(c) && x <= alienX(c) + 2 && y >= alienY(r) && y <= alienY(r) + 1 &&
                            SPRITES[kindOf(r)][frame][y - alienY(r)][x - alienX(c)] == '#') {
                            killAlien(r, c);
                            gone = true;
                        }
                    }
                }
                for (size_t j = 0; j < bombs.size() && !gone; ++j) {
                    if ((int)bombs[j].x == x && std::abs((int)std::lround(bombs[j].y) - y) <= 1) {
                        bombs.erase(bombs.begin() + j);
                        gone = true;
                    }
                }
            }
            if (gone) {
                shots.erase(shots.begin() + k);
            } else {
                ++k;
            }
        }
        for (size_t k = 0; k < bombs.size();) {
            Shot &b = bombs[k];
            bool gone = false;
            const float to = b.y + b.vy * dt;
            while (!gone && b.y < to) {
                b.y = std::min(to, b.y + 1.0f);
                const int x = (int)b.x, y = (int)std::lround(b.y);
                if (y >= H) {
                    gone = true;
                    break;
                }
                if (shield[y * W + x]) {
                    shield[y * W + x] = 0;
                    gone = true;
                    break;
                }
                // The cannon: ".#." over "###" on the bottom two rows.
                if ((y == H - 1 && x >= cannonX && x <= cannonX + 2) || (y == H - 2 && x == cannonX + 1)) {
                    gone = true;
                    die();
                }
            }
            if (gone) {
                bombs.erase(bombs.begin() + k);
                if (state != State::Play) {
                    return;
                }
            } else {
                ++k;
            }
        }
    }

    void stepSparks(float dt) {
        for (auto &s : sparks) {
            s.age += dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark &s) { return s.age >= s.life; }),
                     sparks.end());
    }

    // ---------------------------------------------------------------- drawing
    void render() {
        canvas.clear();
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (shield[y * W + x]) {
                    canvas.add(x, y, SHIELD, 0.45f);
                }
            }
        }
        if (state != State::Over) {
            for (int r = 0; r < rows; ++r) {
                const int k = kindOf(r);
                for (int c = 0; c < cols; ++c) {
                    if (!alive[r * cols + c]) {
                        continue;
                    }
                    for (int sy = 0; sy < 2; ++sy) {
                        for (int sx = 0; sx < 3; ++sx) {
                            if (SPRITES[k][frame][sy][sx] == '#') {
                                canvas.add(alienX(c) + sx, alienY(r) + sy, KIND_COLOUR[k], 0.75f);
                            }
                        }
                    }
                }
            }
        }
        if (saucer) {
            const int sx = (int)std::floor(saucerX);
            canvas.add(sx, 0, SAUCER, 0.9f);
            canvas.add(sx + 1, 0, ArcadeCanvas::WHITE, std::fmod(clock, 0.3f) < 0.15f ? 1.0f : 0.4f);
            canvas.add(sx + 2, 0, SAUCER, 0.9f);
        }
        for (auto &s : shots) {
            canvas.add((int)s.x, (int)std::lround(s.y), SHOT, 1.0f);
        }
        for (auto &b : bombs) {
            canvas.add((int)b.x, (int)std::lround(b.y), BOMB, std::fmod(clock + b.x, 0.2f) < 0.1f ? 1.0f : 0.6f);
        }
        if (laserT > 0) {
            for (int y = 0; y < H - 2; ++y) {
                canvas.add(laserX, y, ArcadeCanvas::WHITE, laserT / 0.35f);
                canvas.add(laserX - 1, y, LASER, 0.6f * laserT / 0.35f);
                canvas.add(laserX + 1, y, LASER, 0.6f * laserT / 0.35f);
            }
        }
        const bool showCannon = state != State::Over && !(state == State::Dying && std::fmod(stateT, 0.2f) < 0.1f);
        if (showCannon) {
            canvas.add(cannonX + 1, H - 2, CANNON, 1.0f);
            for (int x = 0; x < 3; ++x) {
                canvas.add(cannonX + x, H - 1, CANNON, 0.9f);
            }
        }
        for (auto &s : sparks) {
            canvas.add((int)std::lround(s.x), (int)std::lround(s.y), s.col, 1.0f - s.age / s.life);
        }
        // Lives (white) and the laser charge (magenta) in the top corner.
        for (int k = 0; k < lives - 1; ++k) {
            canvas.add(W - 1 - 2 * k, 0, ArcadeCanvas::WHITE, 0.3f);
        }
        if (laser > 0) {
            canvas.add(0, 0, LASER, 0.5f);
        }
        canvas.writeTo(model);

        if (state == State::Intro) {
            textBox({"WAVE", std::to_string(wave)}, 255, 255, 255);
        } else if (state == State::Clear && stateT > 0.5f) {
            textBox({std::to_string(score)}, 255, 220, 0);
        } else if (state == State::Over && stateT > 1.0f) {
            textBox({"GAME", "OVER", std::to_string(score)}, 255, 255, 255);
        }
    }

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 0, H = 0, cols = 8, rows = 4;

    State state = State::Intro;
    float stateT = 0, clock = 0;
    int wave = 1, score = 0, lives = 3, startLives = 3;

    std::vector<uint8_t> alive, shield;
    int aliensLeft = 0, fx = 1, fy = 2, fdir = 1, frame = 0;
    float marchT = 0, bombT = 1.0f;

    int cannonX = 0;
    float cannonAcc = 0, fireT = 0;
    std::vector<Shot> shots, bombs;
    std::vector<Spark> sparks;

    bool saucer = false;
    int saucerDir = 1;
    float saucerX = 0, saucerT = 15;

    int laser = 1, laserX = 0;
    float laserT = -1;

    std::atomic<uint32_t> held{0};
    std::atomic<bool> fireNow{false}, laserNow{false};
    bool finished = false;
};


const std::string &FPPStarDefenders::getName() {
    static const std::string name = "Star Defenders";
    return name;
}

void FPPStarDefenders::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    StarDefendersEffect *effect = dynamic_cast<StarDefendersEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new StarDefendersEffect(m, findIntOption("Lives", 3));
        m->setRunningEffect(effect, 25);
    }
    if (!starting || button.compare(0, 5, "Start") != 0) {
        effect->button(button);
    }
}
