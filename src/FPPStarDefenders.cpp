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
// dropping a row at each edge. Each wave lays the formation out in a
// different shape. Armoured aliens take two hits, divers peel off and swoop
// at the cannon, and capsules dropped by the saucer and some aliens give the
// cannon a weapon for a while. Shields lose an LED to every shot that hits
// them, from either side.

namespace {

// Three kinds, two frames each, rows top to bottom.
const char *SPRITES[3][2][2] = {
    {{".#.", "#.#"}, {".#.", ".#."}},
    {{"#.#", "###"}, {"###", "#.#"}},
    {{"###", "#.#"}, {"###", ".#."}},
};
const RGB KIND_COLOUR[3] = {{1.0f, 0.2f, 0.9f}, {0.1f, 0.9f, 1.0f}, {0.3f, 1.0f, 0.2f}};
const RGB ARMOUR = {0.85f, 0.85f, 1.0f};
const int KIND_POINTS[3] = {30, 20, 10};
const RGB CANNON = {0.2f, 1.0f, 0.4f};
const RGB SHIELD = {0.1f, 0.85f, 0.2f};
const RGB SHOT = {1.0f, 1.0f, 0.8f};
const RGB BOMB = {1.0f, 0.35f, 0.1f};
const RGB SAUCER = {1.0f, 0.1f, 0.1f};
const RGB LASER = {1.0f, 0.2f, 0.9f};

// Formations, in an 8 x 4 grid; other sizes sample it. The last is the
// "fortress", where every row but the bottom is armoured.
const char *FORMATIONS[7][4] = {
    {"########", "########", "########", "########"},   // block
    {"##....##", ".##..##.", "..####..", "...##..."},   // V
    {"..####..", ".######.", ".######.", "..####.."},   // diamond
    {"#.#.#.#.", ".#.#.#.#", "#.#.#.#.", ".#.#.#.#"},   // checkerboard
    {"###..###", "###..###", "###..###", "###..###"},   // two squads
    {"...##...", "..####..", ".######.", "########"},   // pyramid
    {"########", "########", "########", "########"},   // fortress
};

enum class Power { None, Double, Rapid, Pierce, Shields, Laser, Life };
struct PowerInfo {
    const char *word;
    RGB colour;
    const char *sound;   // played when its capsule is caught
};
const PowerInfo POWERS[] = {
    {"", {0, 0, 0}, ""},
    {"DOUBLE", {0.1f, 0.9f, 1.0f}, "PowerDouble"},
    {"RAPID", {1.0f, 0.85f, 0.1f}, "PowerRapid"},
    {"PIERCE", {1.0f, 1.0f, 1.0f}, "PowerPierce"},
    {"SHIELDS", {0.2f, 1.0f, 0.3f}, "PowerShields"},
    {"LASER", {1.0f, 0.2f, 0.9f}, "PowerLaser"},
    {"LIFE", {1.0f, 0.15f, 0.15f}, "ExtraLife"},
};
// The march: four bass notes going down, one per formation step.
const char *MARCH[4] = {"March1", "March2", "March3", "March4"};
constexpr float POWER_TIME = 12.0f;

enum : uint32_t { LEFT = 1, RIGHT = 2, FIRE = 4 };

}  // namespace

class StarDefendersEffect : public FPPArcadeGameEffect {
public:
    StarDefendersEffect(PixelOverlayModel *m, int lives, int firstWave) : FPPArcadeGameEffect(m), rng(std::random_device{}()) {
        startWaveNumber = std::clamp(firstWave, 1, 99);
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
        const int cheat = state != State::Over ? takeCheat() : 0;
        if (cheat == CHEAT_NEXT_LEVEL) {
            ++wave;
            startWave();
        }
        if (cheat == CHEAT_LAST_LEVEL) {
            // Wave 14, the last formation (the fortress) on
            // its second time round, with bombs and divers at full rate - and
            // 9 lives, 5 lasers and the piercing shot.
            wave = 14;
            startWave();
            lives = std::max(lives, 9);
            laser = 5;
            power = Power::Pierce;   // one weapon at a time; this one cuts through armour
            powerT = POWER_TIME;
        }
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

    struct Alien {
        int c, r, kind, hp;
        bool alive;
        bool diving;
        float x, y;      // where a diver is
        float targetX;   // where it's swooping to
        int phase;       // 0 down at the cannon, 1 back to its place
    };
    struct Shot {
        float x, y, vy;
        bool pierce;
    };
    struct Capsule {
        float x, y;
        Power p;
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    // ---------------------------------------------------------------- game flow
    void newGame() {
        wave = startWaveNumber;
        score = 0;
        lives = startLives;
        laser = 0;
        power = Power::None;
        powerT = 0;
        startWave();
    }

    int formationIndex() const { return (wave - 1) % 7; }

    void startWave() {
        aliens.clear();
        const int f = formationIndex();
        // Armour: the top row from wave 4 and the top two from wave 8, and
        // every row but the bottom in the fortress.
        const int armouredRows = f == 6 ? rows - 1 : (wave >= 8 ? 2 : (wave >= 4 ? 1 : 0));
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (FORMATIONS[f][r * 4 / rows][c * 8 / cols] == '#') {
                    aliens.push_back({c, r, kindOf(r), r < armouredRows ? 2 : 1, true, false, 0, 0, 0, 0});
                }
            }
        }
        total = (int)aliens.size();
        fx = 1;
        fy = 2 + std::min(4, (wave - 1) / 2);   // later waves start a little lower
        fdir = 1;
        frame = 0;
        marchT = 0;
        shots.clear();
        bombs.clear();
        capsules.clear();
        saucer = false;
        saucerT = 8 + rnd() * 8;
        diveT = 4.0f;
        laser = std::max(laser, 1);   // at least one laser a wave
        buildShields();
        cannonX = W / 2 - 1;
        marchNote = 0;
        state = State::Intro;
        stateT = 0;
        sfxStop("stardefenders");
        sfx("stardefenders", "WaveStart");
    }

    void buildShields() {
        shield.assign(W * H, 0);
        const int n = std::max(2, W / 15);
        const int y = H - 6;
        for (int k = 0; k < n; ++k) {
            const int cx = (int)((k + 0.5f) * W / n);
            for (int dx = -2; dx <= 2; ++dx) {
                shield[y * W + cx + dx] = 1;
                if (dx != 0) {
                    shield[(y + 1) * W + cx + dx] = 1;   // an arch: open under the middle
                }
            }
        }
    }

    int alienX(const Alien &a) const { return a.diving ? (int)std::lround(a.x) : fx + a.c * 5; }
    int alienY(const Alien &a) const { return a.diving ? (int)std::lround(a.y) : fy + a.r * 3; }
    int homeX(const Alien &a) const { return fx + a.c * 5; }
    int homeY(const Alien &a) const { return fy + a.r * 3; }
    int kindOf(int r) const { return r == 0 ? 0 : (r <= (rows - 1) / 2 ? 1 : 2); }
    int aliveCount() const {
        int n = 0;
        for (auto &a : aliens) n += a.alive ? 1 : 0;
        return n;
    }
    float marchEvery() const {
        return 0.04f + 0.55f * (float)aliveCount() / std::max(1, total) / (1.0f + 0.15f * (wave - 1));
    }

    void burst(float x, float y, const RGB &col, int n) {
        for (int k = 0; k < n; ++k) {
            const float a = rnd() * 6.2831853f, sp = 4.0f + rnd() * 8.0f;
            sparks.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp, 0.0f, 0.3f + rnd() * 0.4f, col});
        }
    }

    void step(float dt) {
        stepSparks(dt);
        wordT = std::max(0.0f, wordT - dt);
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
                    for (auto &a : aliens) {
                        a.diving = false;   // divers go home while you respawn
                    }
                    state = State::Play;
                    stateT = 0;
                } else {
                    state = State::Over;
                    stateT = 0;
                    sfxStop("stardefenders");
                    sfx("stardefenders", "GameOver");
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

        // The weapon in hand runs down.
        if (power != Power::None) {
            powerT -= dt;
            if (powerT <= 0) {
                power = Power::None;
            }
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
        const bool rapid = power == Power::Rapid, twin = power == Power::Double;
        const size_t maxShots = (rapid ? 4 : 2) * (twin ? 2 : 1);
        if ((fireNow.exchange(false) || ((h & FIRE) && fireT <= 0)) && fireT <= 0 && shots.size() < maxShots) {
            const bool pierce = power == Power::Pierce;
            if (twin) {
                shots.push_back({(float)cannonX, (float)H - 2, -30.0f, pierce});
                shots.push_back({cannonX + 2.0f, (float)H - 2, -30.0f, pierce});
            } else {
                shots.push_back({cannonX + 1.0f, (float)H - 3, -30.0f, pierce});
            }
            fireT = rapid ? 0.12f : 0.35f;
            sfx("stardefenders", "Shoot", rapid ? 0.7f : 1.0f);
        }
        if (laserNow.exchange(false) && laser > 0) {
            --laser;
            laserT = 0.35f;
            laserX = cannonX + 1;
            sfx("stardefenders", "Laser");
            fireLaser();
        }
        laserT -= dt;

        march(dt);
        if (state != State::Play) {
            return;
        }
        dive(dt);
        if (state != State::Play) {
            return;
        }

        // Bombs from the lowest alien in a random column of the formation.
        bombT -= dt;
        if (bombT <= 0 && (int)bombs.size() < 2 + wave / 2) {
            bombT = std::max(0.25f, 1.1f - 0.1f * wave) * (0.5f + rnd());
            const int c = (int)(rnd() * cols) % cols;
            const Alien *low = nullptr;
            for (auto &a : aliens) {
                if (a.alive && !a.diving && a.c == c && (!low || a.r > low->r)) {
                    low = &a;
                }
            }
            if (low) {
                bombs.push_back({alienX(*low) + 1.0f, alienY(*low) + 2.0f, std::min(16.0f, 8.0f + wave), false});
            }
        }

        // The saucer along the top row now and then; it always drops a capsule.
        saucerT -= dt;
        if (!saucer && saucerT <= 0) {
            saucer = true;
            saucerDir = rnd() < 0.5f ? 1 : -1;
            saucerX = saucerDir > 0 ? -3.0f : (float)W;
            saucerSfxAt = 0;
        }
        if (saucer) {
            // Its warble, back to back for as long as it's on the screen.
            if (clock >= saucerSfxAt) {
                saucerSfxAt = clock + 0.4f;
                sfx("stardefenders", "Saucer", 0.6f);
            }
            saucerX += saucerDir * 9.0f * dt;
            if (saucerX < -4 || saucerX > W + 1) {
                saucer = false;
                saucerT = 14 + rnd() * 10;
            }
        }

        moveShots(dt);
        if (state != State::Play) {
            return;
        }
        moveCapsules(dt);
        if (aliveCount() == 0) {
            sfx("stardefenders", "WaveClear");
            score += 100 * wave;
            state = State::Clear;
            stateT = 0;
        }
    }

    // The formation: one step at a time, down a row at the edges. Divers are
    // out of it and don't count toward the edges.
    void march(float dt) {
        marchT += dt;
        if (marchT < marchEvery()) {
            return;
        }
        marchT = 0;
        frame ^= 1;
        int lo = W, hi = -1;
        for (auto &a : aliens) {
            if (a.alive && !a.diving) {
                lo = std::min(lo, homeX(a));
                hi = std::max(hi, homeX(a) + 2);
            }
        }
        if (hi < 0) {
            return;
        }
        // It speeds up with the formation, as it always has.
        sfx("stardefenders", MARCH[marchNote]);
        marchNote = (marchNote + 1) % 4;
        if ((fdir > 0 && hi + 1 >= W) || (fdir < 0 && lo - 1 < 0)) {
            fdir = -fdir;
            ++fy;
        } else {
            fx += fdir;
        }
        for (auto &a : aliens) {
            if (!a.alive || a.diving) {
                continue;
            }
            // Landed on the cannon's row: the game is over.
            if (homeY(a) + 1 >= H - 2) {
                lives = 1;
                die();
                return;
            }
            // Aliens wipe out the shields they march through.
            for (int y = homeY(a); y <= homeY(a) + 1; ++y) {
                for (int x = homeX(a); x <= homeX(a) + 2; ++x) {
                    if (x >= 0 && x < W && y >= 0 && y < H) {
                        shield[y * W + x] = 0;
                    }
                }
            }
        }
    }

    // From wave 3, aliens peel off now and then and swoop at the cannon,
    // dropping a bomb on the way, then fly back to their place.
    void dive(float dt) {
        if (wave >= 3) {
            diveT -= dt;
            if (diveT <= 0) {
                diveT = std::max(1.5f, 6.0f - 0.4f * wave) * (0.7f + 0.6f * rnd());
                std::vector<Alien *> ready;
                for (auto &a : aliens) {
                    if (a.alive && !a.diving) ready.push_back(&a);
                }
                int diving = 0;
                for (auto &a : aliens) diving += (a.alive && a.diving) ? 1 : 0;
                if (!ready.empty() && diving < 1 + wave / 4) {
                    Alien &a = *ready[(size_t)(rnd() * ready.size()) % ready.size()];
                    a.diving = true;
                    a.phase = 0;
                    a.x = (float)homeX(a);
                    a.y = (float)homeY(a);
                    a.targetX = cannonX + (rnd() - 0.5f) * 6.0f;
                    diveBombed = false;
                }
            }
        }
        for (auto &a : aliens) {
            if (!a.alive || !a.diving) {
                continue;
            }
            if (a.phase == 0) {
                a.y += 11.0f * dt;
                a.x += std::clamp(a.targetX - a.x, -7.0f * dt, 7.0f * dt) + std::sin(a.y * 0.6f) * 6.0f * dt;
                a.x = std::clamp(a.x, 0.0f, (float)(W - 3));
                if (!diveBombed && a.y > H * 0.45f) {
                    diveBombed = true;
                    bombs.push_back({a.x + 1, a.y + 2, std::min(16.0f, 9.0f + wave), false});
                }
                if (hitsCannon(alienX(a), alienY(a))) {
                    a.alive = false;
                    burst(a.x + 1, a.y + 1, KIND_COLOUR[a.kind], 8);
                    die();
                    return;
                }
                if (a.y > H) {
                    a.phase = 1;
                    a.y = -2;   // round the back of the screen to its place
                }
            } else {
                const float tx = (float)homeX(a), ty = (float)homeY(a);
                const float dx = tx - a.x, dy = ty - a.y, d = std::hypot(dx, dy);
                if (d < 1.0f) {
                    a.diving = false;
                } else {
                    a.x += dx / d * 14.0f * dt;
                    a.y += dy / d * 14.0f * dt;
                }
            }
        }
    }

    bool hitsCannon(int x, int y) const {
        for (int sy = 0; sy < 2; ++sy) {
            for (int sx = 0; sx < 3; ++sx) {
                const int px = x + sx, py = y + sy;
                if ((py == H - 1 && px >= cannonX && px <= cannonX + 2) || (py == H - 2 && px == cannonX + 1)) {
                    return true;
                }
            }
        }
        return false;
    }

    // B: a beam straight up from the cannon, three LEDs wide, through
    // everything - aliens (armour too), shields, bombs and the saucer.
    void fireLaser() {
        for (auto &a : aliens) {
            if (a.alive && std::abs(alienX(a) + 1 - laserX) <= 2) {
                a.hp = 1;
                hitAlien(a);
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

    // Armour takes the first hit; the second kills. Divers are worth double,
    // and a quarter of them, and a few others, drop a capsule.
    void hitAlien(Alien &a) {
        if (--a.hp > 0) {
            burst(alienX(a) + 1, alienY(a) + 1, ARMOUR, 3);
            sfx("stardefenders", "ArmourHit");
            return;
        }
        // The laser or a piercing shot can take out a column at once: one sound.
        if (killSfxAt != clock) {
            killSfxAt = clock;
            sfx("stardefenders", a.diving ? "DiverKill" : "AlienKill");
        }
        a.alive = false;
        score += KIND_POINTS[a.kind] * (a.diving ? 2 : 1);
        burst(alienX(a) + 1, alienY(a) + 1, KIND_COLOUR[a.kind], 8);
        if (rnd() < (a.diving ? 0.25f : 0.04f)) {
            dropCapsule(alienX(a) + 1.0f, alienY(a) + 1.0f);
        }
    }

    void hitSaucer() {
        static const int values[] = {50, 100, 150, 300};
        score += values[(int)(rnd() * 4) % 4];
        burst(saucerX + 1, 0, SAUCER, 14);
        sfx("stardefenders", "SaucerHit");
        dropCapsule(saucerX + 1, 1);
        saucer = false;
        saucerT = 14 + rnd() * 10;
    }

    void dropCapsule(float x, float y) {
        // Extra lives are rare; the rest are equally likely.
        const float p = rnd();
        Power kind = p < 0.06f ? Power::Life : (Power)(1 + (int)(rnd() * 5) % 5);
        capsules.push_back({std::clamp(x, 0.0f, (float)(W - 1)), y, kind});
    }

    void moveCapsules(float dt) {
        for (size_t k = 0; k < capsules.size();) {
            Capsule &c = capsules[k];
            c.y += 6.0f * dt;
            const int x = (int)std::lround(c.x), y = (int)std::lround(c.y);
            if (y >= H - 2 && x >= cannonX && x <= cannonX + 2) {
                collect(c.p);
                capsules.erase(capsules.begin() + k);
            } else if (y >= H) {
                capsules.erase(capsules.begin() + k);
            } else {
                ++k;
            }
        }
    }

    void collect(Power p) {
        word = POWERS[(int)p].word;
        wordColour = POWERS[(int)p].colour;
        wordT = 1.4f;
        burst(cannonX + 1, H - 2, wordColour, 10);
        sfx("stardefenders", POWERS[(int)p].sound);
        switch (p) {
        case Power::Shields:
            buildShields();
            break;
        case Power::Laser:
            laser = std::min(5, laser + 1);
            break;
        case Power::Life:
            lives = std::min(9, lives + 1);
            break;
        default:
            power = p;   // a new weapon replaces the old one
            powerT = POWER_TIME;
            break;
        }
    }

    void die() {
        burst(cannonX + 1, H - 1, CANNON, 16);
        sfx("stardefenders", "Death");
        power = Power::None;
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
                for (auto &a : aliens) {
                    if (!a.alive || gone) {
                        continue;
                    }
                    const int ax = alienX(a), ay = alienY(a);
                    if (x >= ax && x <= ax + 2 && y >= ay && y <= ay + 1 && SPRITES[a.kind][frame][y - ay][x - ax] == '#') {
                        hitAlien(a);
                        gone = !s.pierce;   // a piercing shot carries on up the column
                    }
                }
                for (size_t j = 0; j < bombs.size() && !gone; ++j) {
                    if ((int)bombs[j].x == x && std::abs((int)std::lround(bombs[j].y) - y) <= 1) {
                        bombs.erase(bombs.begin() + j);
                        sfx("stardefenders", "BombShot", 0.7f);
                        gone = !s.pierce;
                        break;
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
                if (y >= 0 && x >= 0 && x < W && shield[y * W + x]) {
                    shield[y * W + x] = 0;
                    gone = true;
                    if (clock >= shieldSfxAt) {   // several bombs can land together
                        shieldSfxAt = clock + 0.1f;
                        sfx("stardefenders", "ShieldHit", 0.5f);
                    }
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
            for (auto &a : aliens) {
                if (!a.alive) {
                    continue;
                }
                // Armour shows silver until its first hit.
                const RGB col = a.hp > 1 ? armourColour(a.kind) : KIND_COLOUR[a.kind];
                const int ax = alienX(a), ay = alienY(a);
                for (int sy = 0; sy < 2; ++sy) {
                    for (int sx = 0; sx < 3; ++sx) {
                        if (SPRITES[a.kind][frame][sy][sx] == '#') {
                            canvas.add(ax + sx, ay + sy, col, a.diving ? 1.0f : 0.75f);
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
        for (auto &c : capsules) {
            // Blinks between its colour and white so it reads as a pickup.
            const bool flash = std::fmod(clock, 0.3f) < 0.15f;
            canvas.add((int)std::lround(c.x), (int)std::lround(c.y), flash ? ArcadeCanvas::WHITE : POWERS[(int)c.p].colour, 1.0f);
        }
        const RGB shotCol = power == Power::None ? SHOT : POWERS[(int)power].colour;
        for (auto &s : shots) {
            canvas.add((int)s.x, (int)std::lround(s.y), shotCol, 1.0f);
            if (s.pierce) {
                canvas.add((int)s.x, (int)std::lround(s.y) + 1, shotCol, 0.4f);
            }
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
            // The cannon takes the colour of the weapon it has.
            const RGB cc = power == Power::None ? CANNON : POWERS[(int)power].colour;
            canvas.add(cannonX + 1, H - 2, cc, 1.0f);
            for (int x = 0; x < 3; ++x) {
                canvas.add(cannonX + x, H - 1, cc, 0.9f);
            }
        }
        for (auto &s : sparks) {
            canvas.add((int)std::lround(s.x), (int)std::lround(s.y), s.col, 1.0f - s.age / s.life);
        }
        // Top row: laser charges (magenta) from the left, then the weapon's
        // time left as a bar in its colour; spare lives (white) top right.
        for (int k = 0; k < laser; ++k) {
            canvas.add(2 * k, 0, LASER, 0.5f);
        }
        if (power != Power::None) {
            const int len = (int)std::ceil(powerT / POWER_TIME * std::max(4, W / 5));
            for (int k = 0; k < len; ++k) {
                canvas.add(11 + k, 0, POWERS[(int)power].colour, 0.35f);
            }
        }
        for (int k = 0; k < lives - 1; ++k) {
            canvas.add(W - 1 - 2 * k, 0, ArcadeCanvas::WHITE, 0.3f);
        }
        canvas.writeTo(model);

        if (state == State::Intro) {
            textBox({"WAVE", std::to_string(wave)}, 255, 255, 255);
        } else if (state == State::Clear && stateT > 0.5f) {
            textBox({std::to_string(score)}, 255, 220, 0);
        } else if (state == State::Over && stateT > 1.0f) {
            textBox({"GAME", "OVER", std::to_string(score)}, 255, 255, 255);
        } else if (wordT > 0) {
            // What you just caught, low on the left, out of the way of the play.
            PanelTransform pt(this);
            outputString(word, 1, H - 13, (int)(wordColour.r * 255), (int)(wordColour.g * 255), (int)(wordColour.b * 255), 1);
        }
    }

    static RGB armourColour(int kind) { return ArcadeCanvas::mix(KIND_COLOUR[kind], ARMOUR, 0.65f); }

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 0, H = 0, cols = 8, rows = 4;

    State state = State::Intro;
    float stateT = 0, clock = 0;
    int wave = 1, score = 0, lives = 3, startLives = 3, startWaveNumber = 1;

    std::vector<Alien> aliens;
    std::vector<uint8_t> shield;
    int total = 1, fx = 1, fy = 2, fdir = 1, frame = 0;
    float marchT = 0, bombT = 1.0f, diveT = 4.0f;
    bool diveBombed = false;
    int marchNote = 0;
    float killSfxAt = -1, shieldSfxAt = 0, saucerSfxAt = 0;

    int cannonX = 0;
    float cannonAcc = 0, fireT = 0;
    std::vector<Shot> shots, bombs;
    std::vector<Capsule> capsules;
    std::vector<Spark> sparks;

    bool saucer = false;
    int saucerDir = 1;
    float saucerX = 0, saucerT = 15;

    int laser = 1, laserX = 0;
    float laserT = -1;
    Power power = Power::None;
    float powerT = 0;
    std::string word;
    RGB wordColour = {1, 1, 1};
    float wordT = 0;

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
        effect = new StarDefendersEffect(m, findIntOption("Lives", 3), findIntOption("Start Wave", 1));
        m->setRunningEffect(effect, 25);
    }
    if (!starting || button.compare(0, 5, "Start") != 0) {
        effect->button(button);
    }
}
