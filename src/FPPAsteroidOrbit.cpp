#include <fpp-pch.h>

#include "FPPAsteroidOrbit.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

// After the Asteroid Orbit iOS game: a thruster ship, asteroids with gravity,
// landing to reload, crystals, and a black hole out of each cleared sector.
// Everything is in panel pixels and seconds. Row 0 is the status bar; the
// field below it wraps at every edge.

namespace {

struct RGB {
    float r, g, b;
};
RGB mix(const RGB &a, const RGB &b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

const RGB WHITE = {1.0f, 1.0f, 1.0f};
const RGB SHIP = {0.2f, 0.85f, 1.0f};
const RGB FLAME = {1.0f, 0.45f, 0.05f};
const RGB BULLET = {1.0f, 0.95f, 0.6f};
const RGB FUEL = {0.15f, 0.45f, 1.0f};
const RGB AMMO = {1.0f, 0.8f, 0.1f};
const RGB PULSE = {1.0f, 0.2f, 0.9f};
const RGB HOLE = {0.55f, 0.25f, 1.0f};
// Rock colours by sector, repeating.
const std::array<RGB, 4> ROCK = {{{0.8f, 0.62f, 0.45f}, {0.95f, 0.42f, 0.2f}, {0.5f, 0.75f, 0.95f}, {0.75f, 0.45f, 0.95f}}};

constexpr float THRUST = 16.0f;        // px/s^2
constexpr float MAX_SPEED = 22.0f;     // px/s
constexpr float DRAG = 0.15f;          // per second
constexpr float GRAVITY = 12.0f;       // rock pull = GRAVITY * r^2 / distance^2
constexpr float MAX_PULL = 9.0f;       // px/s^2 from one rock
constexpr float HOLE_PULL = 190.0f;
constexpr float LAND_SPEED = 7.0f;     // slower than this (relative to the rock) lands
constexpr float BULLET_SPEED = 30.0f;
constexpr float BULLET_LIFE = 0.9f;
constexpr float FIRE_EVERY = 0.28f;    // seconds between shots while A is held
constexpr int MAX_AMMO = 20;
constexpr float FUEL_PER_S = 0.06f;    // of a full tank, while thrusting
constexpr float PULSE_RADIUS = 9.0f;
constexpr float PULSE_TIME = 0.45f;

// Held buttons, set from the command thread and read by the effect.
enum : uint32_t { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, FIRE = 16 };

}  // namespace

class AsteroidOrbitEffect : public FPPArcadeGameEffect {
public:
    AsteroidOrbitEffect(PixelOverlayModel *m, int startRocks, int gravityPct, int lives)
        : FPPArcadeGameEffect(m), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(8, W);
        H = std::max(8, H);
        top = 1;
        PH = H - top;
        R.assign(W * H, 0.0f);
        G.assign(W * H, 0.0f);
        B.assign(W * H, 0.0f);
        baseRocks = startRocks > 0 ? startRocks : std::max(2, W * H / 400);
        gravityScale = std::clamp(gravityPct, 0, 300) / 100.0f;
        startLives = std::clamp(lives, 1, 9);
        for (int k = 0; k < std::max(6, W * H / 40); ++k) {
            stars.push_back({rnd() * W, top + rnd() * PH, 0.03f + rnd() * 0.08f, rnd() * 6.28f});
        }
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "AsteroidOrbit";
        return NAME;
    }

    void button(const std::string &b) override {
        // Held state first and always, so a release during the pause menu is
        // not lost and the ship doesn't come back thrusting by itself.
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
        if (b == "A Button - Pressed" || b == "Fire - Pressed") {
            fireNow.store(true);
        } else if (b == "B Button - Pressed") {
            pulseNow.store(true);
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
    enum class State { Intro, Play, Dead, Over };
    enum class Kind { Fuel, Ammo, Pulse };

    struct Rock {
        float x, y, vx, vy, r, spin, spinV;
        int size, id;
    };
    struct Shot {
        float x, y, vx, vy, life;
    };
    struct Crystal {
        float x, y, vx, vy, life;
        Kind kind;
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
    };
    struct Star {
        float x, y, k, phase;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    // ---------------------------------------------------------------- the field wraps
    float wrapX(float x) const {
        x = std::fmod(x, (float)W);
        return x < 0 ? x + W : x;
    }
    float wrapY(float y) const {
        y = std::fmod(y - top, (float)PH);
        return (y < 0 ? y + PH : y) + top;
    }
    float dX(float a, float b) const {   // shortest a - b across the wrap
        float d = a - b;
        if (d > W / 2.0f) d -= W;
        if (d < -W / 2.0f) d += W;
        return d;
    }
    float dY(float a, float b) const {
        float d = a - b;
        if (d > PH / 2.0f) d -= PH;
        if (d < -PH / 2.0f) d += PH;
        return d;
    }
    void add(int px, int py, const RGB &c, float k) {
        px %= W;
        if (px < 0) px += W;
        py = (py - top) % PH;
        if (py < 0) py += PH;
        const int i = (py + top) * W + px;
        R[i] += c.r * k;
        G[i] += c.g * k;
        B[i] += c.b * k;
    }
    void addAt(float x, float y, const RGB &c, float k) {
        add((int)std::floor(x), (int)std::floor(y), c, k);
    }

    // ---------------------------------------------------------------- game flow
    void newGame() {
        sector = 1;
        score = 0;
        lives = startLives;
        ammo = MAX_AMMO;
        fuel = 1.0f;
        pulses = 2;
        shots.clear();
        crystals.clear();
        sparks.clear();
        held.store(0);
        fireNow.store(false);
        pulseNow.store(false);
        placeShip(W / 2.0f, top + PH / 2.0f);
        startSector();
    }

    void startSector() {
        rocks.clear();
        crystals.clear();
        shots.clear();
        hole = false;
        const float speedUp = 1.0f + 0.12f * (sector - 1);
        const int count = std::min(12, baseRocks + sector - 1);
        for (int k = 0; k < count; ++k) {
            // Start well away from the ship.
            float x = 0, y = 0;
            for (int tries = 0; tries < 30; ++tries) {
                x = rnd() * W;
                y = top + rnd() * PH;
                if (std::hypot(dX(x, shipX), dY(y, shipY)) > std::min(W, PH) * 0.35f + 3) {
                    break;
                }
            }
            spawnRock(x, y, 3, 0, 0, speedUp);
        }
        state = State::Intro;
        stateT = 0;
    }

    void spawnRock(float x, float y, int size, float vx, float vy, float speedUp) {
        const float sp = (size == 3 ? 2.0f + rnd() * 2.0f : size == 2 ? 3.0f + rnd() * 3.0f : 5.0f + rnd() * 3.0f) * speedUp;
        const float a = rnd() * 6.2831853f;
        Rock r;
        r.x = wrapX(x);
        r.y = wrapY(y);
        r.vx = vx + std::cos(a) * sp;
        r.vy = vy + std::sin(a) * sp * 0.7f;
        r.size = size;
        r.r = size == 3 ? 2.6f : size == 2 ? 1.7f : 0.9f;
        r.spin = rnd() * 6.28f;
        r.spinV = (rnd() - 0.5f) * 2.0f;
        r.id = nextId++;
        rocks.push_back(r);
    }

    void placeShip(float x, float y) {
        shipX = x;
        shipY = y;
        shipVX = shipVY = 0;
        headX = 0;
        headY = -1;
        landedOn = -1;
        noPullFrom = -1;
        safeT = 2.5f;
    }

    // The open spot furthest from every rock.
    void respawn() {
        float bestX = W / 2.0f, bestY = top + PH / 2.0f, best = -1;
        for (int k = 0; k < 24; ++k) {
            const float x = rnd() * W, y = top + rnd() * PH;
            float nearest = 1e9f;
            for (auto &r : rocks) {
                nearest = std::min(nearest, std::hypot(dX(x, r.x), dY(y, r.y)) - r.r);
            }
            if (nearest > best) {
                best = nearest;
                bestX = x;
                bestY = y;
            }
        }
        placeShip(bestX, bestY);
        state = State::Play;
        stateT = 0;
    }

    void burst(float x, float y, const RGB &col, int n, float speed) {
        for (int k = 0; k < n; ++k) {
            const float a = rnd() * 6.2831853f, sp = speed * (0.4f + rnd() * 0.8f);
            sparks.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp, 0.0f, 0.4f + rnd() * 0.5f, col});
        }
    }

    void crash() {
        burst(shipX, shipY, SHIP, 14, 12.0f);
        burst(shipX, shipY, FLAME, 10, 9.0f);
        landedOn = -1;
        --lives;
        state = lives > 0 ? State::Dead : State::Over;
        stateT = 0;
    }

    // A rock hit by a shot or a shockwave: split it, score it, maybe drop a crystal.
    void breakRock(size_t idx) {
        const Rock r = rocks[idx];
        rocks.erase(rocks.begin() + idx);
        if (landedOn == r.id) {
            landedOn = -1;
        }
        score += r.size == 3 ? 20 : r.size == 2 ? 50 : 100;
        burst(r.x, r.y, rockColour(), 4 + r.size * 3, 7.0f);
        if (r.size > 1) {
            const float speedUp = 1.0f + 0.12f * (sector - 1);
            spawnRock(r.x, r.y, r.size - 1, r.vx * 0.5f, r.vy * 0.5f, speedUp);
            spawnRock(r.x, r.y, r.size - 1, r.vx * 0.5f, r.vy * 0.5f, speedUp);
        }
        const float drop = r.size == 3 ? 0.35f : r.size == 2 ? 0.25f : 0.15f;
        if (rnd() < drop) {
            const float pick = rnd();
            const Kind k = pick < 0.5f ? Kind::Fuel : (pick < 0.8f ? Kind::Ammo : Kind::Pulse);
            crystals.push_back({r.x, r.y, r.vx * 0.3f, r.vy * 0.3f, 12.0f, k});
        }
    }

    RGB rockColour() const { return ROCK[(sector - 1) % ROCK.size()]; }

    // ---------------------------------------------------------------- simulation
    void step(float dt) {
        // Rocks drift and turn; nothing else moves them except a shockwave.
        for (auto &r : rocks) {
            r.x = wrapX(r.x + r.vx * dt);
            r.y = wrapY(r.y + r.vy * dt);
            r.spin += r.spinV * dt;
        }
        stepSparks(dt);
        stepShots(dt);
        stepCrystals(dt);
        stepPulse(dt);
        if (hole) {
            holeR = std::min(2.2f, holeR + dt * 2.0f);
        }

        switch (state) {
        case State::Intro:
            if (stateT > 2.0f) {
                state = State::Play;
                stateT = 0;
            }
            return;
        case State::Dead:
            if (stateT > 1.6f) {
                respawn();
            }
            return;
        case State::Over:
            return;
        case State::Play:
            break;
        }
        stepShip(dt);

        if (rocks.empty() && !hole) {
            // Sector clear: the way out opens somewhere away from the ship.
            hole = true;
            holeR = 0;
            holeX = wrapX(shipX + W / 2.0f + (rnd() - 0.5f) * W * 0.3f);
            holeY = wrapY(shipY + PH / 2.0f + (rnd() - 0.5f) * PH * 0.3f);
        }
    }

    void stepShip(float dt) {
        safeT = std::max(0.0f, safeT - dt);
        const uint32_t h = held.load();
        float tx = ((h & RIGHT) ? 1.0f : 0.0f) - ((h & LEFT) ? 1.0f : 0.0f);
        float ty = ((h & DOWN) ? 1.0f : 0.0f) - ((h & UP) ? 1.0f : 0.0f);
        const float tl = std::hypot(tx, ty);
        if (tl > 0) {
            tx /= tl;
            ty /= tl;
        }
        thrusting = false;

        Rock *on = nullptr;
        for (auto &r : rocks) {
            if (r.id == landedOn) {
                on = &r;
            }
        }
        if (landedOn >= 0 && !on) {
            landedOn = -1;   // shot out from under us
        }
        if (on) {
            // Sitting on a rock: ride it, nose out, and refill.
            const float nl = std::max(0.01f, std::hypot(landNX, landNY));
            shipX = wrapX(on->x + landNX / nl * (on->r + 0.7f));
            shipY = wrapY(on->y + landNY / nl * (on->r + 0.7f));
            shipVX = on->vx;
            shipVY = on->vy;
            headX = landNX / nl;
            headY = landNY / nl;
            fuel = std::min(1.0f, fuel + dt * 0.12f);
            reloadT += dt;
            if (reloadT >= 0.8f && ammo < MAX_AMMO) {
                reloadT = 0;
                ++ammo;
            }
            // Thrust that isn't into the rock takes off.
            if (tl > 0 && tx * headX + ty * headY > -0.3f && fuel > 0) {
                shipVX += tx * 6.0f;
                shipVY += ty * 6.0f;
                noPullFrom = landedOn;
                noPullT = 0.5f;
                landedOn = -1;
                headX = tx;
                headY = ty;
            }
        } else {
            if (tl > 0) {
                headX = tx;
                headY = ty;
                if (fuel > 0) {
                    shipVX += tx * THRUST * dt;
                    shipVY += ty * THRUST * dt;
                    fuel = std::max(0.0f, fuel - FUEL_PER_S * dt);
                    thrusting = true;
                }
            }
            noPullT = std::max(0.0f, noPullT - dt);
            for (auto &r : rocks) {
                if (r.id == noPullFrom && noPullT > 0) {
                    continue;
                }
                const float dx = dX(r.x, shipX), dy = dY(r.y, shipY);
                const float d2 = std::max(dx * dx + dy * dy, (r.r + 0.5f) * (r.r + 0.5f));
                const float a = std::min(MAX_PULL, GRAVITY * gravityScale * (1.0f + 0.08f * (sector - 1)) * r.r * r.r / d2);
                const float d = std::sqrt(d2);
                shipVX += dx / d * a * dt;
                shipVY += dy / d * a * dt;
            }
            if (hole) {
                const float dx = dX(holeX, shipX), dy = dY(holeY, shipY);
                const float d2 = std::max(dx * dx + dy * dy, 2.0f);
                const float a = std::min(25.0f, HOLE_PULL / d2), d = std::sqrt(d2);
                shipVX += dx / d * a * dt;
                shipVY += dy / d * a * dt;
                if (dx * dx + dy * dy < 1.7f && holeR > 1.5f) {
                    // Through to the next sector.
                    score += 500;
                    ++sector;
                    pulses = std::min(9, pulses + 1);
                    placeShip(W / 2.0f, top + PH / 2.0f);
                    startSector();
                    return;
                }
            }
            const float keep = std::exp(-DRAG * dt);
            shipVX *= keep;
            shipVY *= keep;
            const float sp = std::hypot(shipVX, shipVY);
            if (sp > MAX_SPEED) {
                shipVX *= MAX_SPEED / sp;
                shipVY *= MAX_SPEED / sp;
            }
            shipX = wrapX(shipX + shipVX * dt);
            shipY = wrapY(shipY + shipVY * dt);

            // Touching a rock: land if slow enough, otherwise crash.
            for (auto &r : rocks) {
                const float dx = dX(shipX, r.x), dy = dY(shipY, r.y);
                if (dx * dx + dy * dy >= (r.r + 0.5f) * (r.r + 0.5f)) {
                    continue;
                }
                if (r.id == noPullFrom && noPullT > 0) {
                    continue;   // just took off from it
                }
                const float rel = std::hypot(shipVX - r.vx, shipVY - r.vy);
                if (rel < LAND_SPEED) {
                    landedOn = r.id;
                    landNX = dx;
                    landNY = dy;
                    reloadT = 0;
                    ammo = std::min(MAX_AMMO, ammo + 3);
                } else if (safeT > 0) {
                    // Still flashing after a respawn: bounce off instead.
                    const float d = std::max(0.01f, std::hypot(dx, dy));
                    shipVX = r.vx + dx / d * 6.0f;
                    shipVY = r.vy + dy / d * 6.0f;
                } else {
                    crash();
                    return;
                }
                break;
            }
        }

        // Fire: once on the press, then repeatedly while A stays down.
        fireT = std::max(0.0f, fireT - dt);
        const bool want = fireNow.exchange(false) || ((h & FIRE) && fireT <= 0);
        if (want && fireT <= 0 && ammo > 0) {
            shots.push_back({shipX + headX, shipY + headY, shipVX * 0.5f + headX * BULLET_SPEED,
                             shipVY * 0.5f + headY * BULLET_SPEED, BULLET_LIFE});
            --ammo;
            fireT = FIRE_EVERY;
        }
        if (pulseNow.exchange(false) && pulses > 0 && pulseT < 0) {
            --pulses;
            pulseT = 0;
            pulseX = shipX;
            pulseY = shipY;
            pushed.clear();
        }

        for (size_t k = 0; k < crystals.size();) {
            const Crystal &c = crystals[k];
            if (std::hypot(dX(c.x, shipX), dY(c.y, shipY)) < 1.4f) {
                score += 50;
                if (c.kind == Kind::Fuel) fuel = std::min(1.0f, fuel + 0.35f);
                if (c.kind == Kind::Ammo) ammo = std::min(MAX_AMMO, ammo + 6);
                if (c.kind == Kind::Pulse) pulses = std::min(9, pulses + 1);
                burst(c.x, c.y, crystalColour(c.kind), 6, 5.0f);
                crystals.erase(crystals.begin() + k);
            } else {
                ++k;
            }
        }
    }

    void stepShots(float dt) {
        for (size_t k = 0; k < shots.size();) {
            Shot &s = shots[k];
            s.life -= dt;
            s.x = wrapX(s.x + s.vx * dt);
            s.y = wrapY(s.y + s.vy * dt);
            bool gone = s.life <= 0;
            for (size_t j = 0; !gone && j < rocks.size(); ++j) {
                const Rock &r = rocks[j];
                if (std::hypot(dX(s.x, r.x), dY(s.y, r.y)) < r.r + 0.4f) {
                    breakRock(j);
                    gone = true;
                }
            }
            if (gone) {
                shots.erase(shots.begin() + k);
            } else {
                ++k;
            }
        }
    }

    void stepCrystals(float dt) {
        for (size_t k = 0; k < crystals.size();) {
            Crystal &c = crystals[k];
            c.life -= dt;
            if (hole) {   // the black hole takes what's left
                const float dx = dX(holeX, c.x), dy = dY(holeY, c.y), d = std::max(1.0f, std::hypot(dx, dy));
                c.vx += dx / d * 8.0f * dt;
                c.vy += dy / d * 8.0f * dt;
            }
            c.x = wrapX(c.x + c.vx * dt);
            c.y = wrapY(c.y + c.vy * dt);
            if (c.life <= 0) {
                crystals.erase(crystals.begin() + k);
            } else {
                ++k;
            }
        }
    }

    // The shockwave: a ring that pushes each rock it reaches once, and breaks
    // the smallest ones.
    void stepPulse(float dt) {
        if (pulseT < 0) {
            return;
        }
        pulseT += dt;
        const float radius = PULSE_RADIUS * pulseT / PULSE_TIME;
        for (size_t j = 0; j < rocks.size();) {
            Rock &r = rocks[j];
            const float dx = dX(r.x, pulseX), dy = dY(r.y, pulseY), d = std::hypot(dx, dy);
            if (d - r.r <= radius && std::find(pushed.begin(), pushed.end(), r.id) == pushed.end()) {
                if (r.size == 1) {
                    breakRock(j);
                    continue;
                }
                pushed.push_back(r.id);
                const float n = std::max(0.3f, d);
                r.vx += dx / n * 12.0f / r.size;
                r.vy += dy / n * 12.0f / r.size;
                if (landedOn == r.id) {
                    landedOn = -1;
                }
            }
            ++j;
        }
        if (pulseT >= PULSE_TIME) {
            pulseT = -1;
        }
    }

    void stepSparks(float dt) {
        const float drag = std::pow(0.4f, dt);
        for (auto &s : sparks) {
            s.age += dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.vx *= drag;
            s.vy *= drag;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark &s) { return s.age >= s.life; }),
                     sparks.end());
    }

    static RGB crystalColour(Kind k) { return k == Kind::Fuel ? FUEL : (k == Kind::Ammo ? AMMO : PULSE); }

    // ---------------------------------------------------------------- drawing
    void render() {
        std::fill(R.begin(), R.end(), 0.0f);
        std::fill(G.begin(), G.end(), 0.0f);
        std::fill(B.begin(), B.end(), 0.0f);

        for (auto &s : stars) {
            addAt(s.x, s.y, {0.6f, 0.7f, 1.0f}, s.k * (0.6f + 0.4f * std::sin(clock * 1.7f + s.phase)));
        }
        if (hole) {
            drawHole();
        }
        for (auto &r : rocks) {
            drawRock(r);
        }
        for (auto &c : crystals) {
            const float blink = c.life < 3.0f ? (std::fmod(c.life, 0.4f) < 0.2f ? 0.3f : 1.0f) : 1.0f;
            addAt(c.x, c.y, crystalColour(c.kind), (0.6f + 0.4f * std::sin(clock * 8.0f)) * blink);
        }
        for (auto &s : shots) {
            addAt(s.x, s.y, BULLET, 1.0f);
            addAt(s.x - s.vx * 0.03f, s.y - s.vy * 0.03f, BULLET, 0.25f);
        }
        if (pulseT >= 0) {
            const float radius = PULSE_RADIUS * pulseT / PULSE_TIME, k = 1.0f - pulseT / PULSE_TIME;
            const int n = std::max(12, (int)(radius * 7));
            for (int j = 0; j < n; ++j) {
                const float a = j * 6.2831853f / n;
                addAt(pulseX + std::cos(a) * radius, pulseY + std::sin(a) * radius, PULSE, 0.9f * k);
            }
        }
        if (state == State::Play || state == State::Intro) {
            drawShip();
        }
        for (auto &s : sparks) {
            const float k = 1.0f - s.age / s.life;
            addAt(s.x, s.y, mix(s.col, WHITE, k * 0.4f), k);
        }
        drawStatus();

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int i = y * W + x;
                // Clamped at both ends: the black hole darkens what is behind it.
                model->setOverlayPixelValue(x, y, std::clamp((int)(R[i] * 255.0f + 0.5f), 0, 255),
                                            std::clamp((int)(G[i] * 255.0f + 0.5f), 0, 255),
                                            std::clamp((int)(B[i] * 255.0f + 0.5f), 0, 255));
            }
        }
        drawWords();
    }

    // A shaded ball, lit from the top left, with a few darker patches that
    // turn as it spins. Edge LEDs are lit by how much of them the rock covers.
    void drawRock(const Rock &r) {
        const RGB col = rockColour();
        const float L[3] = {-0.5f, -0.6f, 0.62f};
        for (int py = (int)std::floor(r.y - r.r - 1); py <= (int)std::ceil(r.y + r.r + 1); ++py) {
            for (int px = (int)std::floor(r.x - r.r - 1); px <= (int)std::ceil(r.x + r.r + 1); ++px) {
                const float dx = px + 0.5f - r.x, dy = py + 0.5f - r.y, d = std::hypot(dx, dy);
                const float cover = clamp01(r.r + 0.5f - d);
                if (cover <= 0) {
                    continue;
                }
                const float nx = dx / r.r, ny = dy / r.r, nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                const float lam = std::max(0.0f, nx * L[0] + ny * L[1] + nz * L[2]);
                const float ang = std::atan2(dy, dx);
                const float pits = 0.8f + 0.2f * std::sin(3.0f * ang + r.spin + r.id) * std::sin(d * 2.1f + r.id);
                add(px, py, col, (0.18f + 0.75f * lam) * pits * cover);
            }
        }
    }

    void drawShip() {
        if (safeT > 0 && std::fmod(safeT, 0.3f) < 0.12f) {
            return;   // flashing just after a respawn
        }
        addAt(shipX, shipY, SHIP, 0.85f);
        addAt(shipX + headX * 1.1f, shipY + headY * 1.1f, WHITE, 1.0f);
        if (thrusting && std::fmod(clock, 0.1f) < 0.07f) {
            addAt(shipX - headX * 1.1f, shipY - headY * 1.1f, FLAME, 0.9f);
        }
    }

    // A dark middle with a ring of light swirling into it.
    void drawHole() {
        const float reach = holeR + 1.8f;
        for (int py = (int)std::floor(holeY - reach); py <= (int)std::ceil(holeY + reach); ++py) {
            for (int px = (int)std::floor(holeX - reach); px <= (int)std::ceil(holeX + reach); ++px) {
                const float dx = px + 0.5f - holeX, dy = py + 0.5f - holeY, d = std::hypot(dx, dy);
                if (d > reach) {
                    continue;
                }
                if (d < holeR * 0.55f) {
                    // Swallow the stars behind it.
                    add(px, py, {-1.0f, -1.0f, -1.0f}, 1.0f);
                    continue;
                }
                const float swirl = 0.5f + 0.5f * std::sin(3.0f * std::atan2(dy, dx) - clock * 6.0f + d * 2.0f);
                const float edge = clamp01(1.0f - (d - holeR * 0.55f) / (reach - holeR * 0.55f));
                add(px, py, mix(HOLE, WHITE, swirl * edge * 0.4f), (0.15f + 0.85f * swirl) * edge);
            }
        }
    }

    // Row 0: fuel from the left, ammo from the right, lives and shockwaves in
    // the middle.
    void drawStatus() {
        // Straight into row 0: add() wraps rows into the field below it.
        auto status = [&](int x, const RGB &c, float k) {
            if (x >= 0 && x < W) {
                R[x] += c.r * k;
                G[x] += c.g * k;
                B[x] += c.b * k;
            }
        };
        const int third = std::max(3, W / 3);
        for (int x = 0; x < (int)std::ceil(fuel * third); ++x) {
            status(x, fuel < 0.2f && std::fmod(clock, 0.5f) < 0.25f ? FLAME : FUEL, 0.3f);
        }
        for (int x = 0; x < (int)std::ceil((float)ammo / MAX_AMMO * third); ++x) {
            status(W - 1 - x, AMMO, 0.25f);
        }
        int x = W / 2 - (lives + pulses);
        for (int k = 0; k < lives; ++k, x += 2) {
            status(x, WHITE, 0.35f);
        }
        for (int k = 0; k < pulses; ++k, x += 2) {
            status(x, PULSE, 0.35f);
        }
    }

    // Text goes on last, over the field, with the arcade's letters.
    void drawWords() {
        if (state == State::Intro && stateT < 1.6f) {
            const std::string num = std::to_string(sector);
            const int scl = fitTextScale({"SECTOR"}, std::max(1, H / 12));
            // In letter rows (outputString scales y too): SECTOR, a gap, the number.
            const int y0 = std::max(0, (H / scl - 11) / 2);
            outputString("SECTOR", centerTextX("SECTOR", scl), y0, 120, 200, 255, scl);
            outputString(num, centerTextX(num, scl), y0 + 6, 255, 255, 255, scl);
        } else if (state == State::Over && stateT > 1.0f) {
            const std::string s = std::to_string(score);
            const int scl = fitTextScale({"GAME", "OVER", s}, 1);
            const int lineH = H >= 17 ? 6 : 5;
            const int y0 = std::max(0, (H - (lineH * 2 + 5)) / 2);
            outputString("GAME", centerTextX("GAME", scl), y0, 255, 255, 255, scl);
            outputString("OVER", centerTextX("OVER", scl), y0 + lineH, 255, 255, 255, scl);
            if (H >= 17) {
                outputString(s, centerTextX(s, scl), y0 + lineH * 2, 255, 200, 60, scl);
            }
        }
    }

    std::mt19937 rng;
    int W = 0, H = 0, top = 1, PH = 0;
    std::vector<float> R, G, B;
    std::vector<Star> stars;

    int baseRocks = 3;
    float gravityScale = 1.0f;
    int startLives = 3;

    State state = State::Intro;
    float stateT = 0, clock = 0;
    int sector = 1, score = 0, lives = 3, ammo = MAX_AMMO, pulses = 2, nextId = 1;
    float fuel = 1.0f;

    float shipX = 0, shipY = 0, shipVX = 0, shipVY = 0, headX = 0, headY = -1;
    float safeT = 0, fireT = 0, reloadT = 0, noPullT = 0;
    int landedOn = -1, noPullFrom = -1;
    float landNX = 0, landNY = -1;
    bool thrusting = false;

    std::vector<Rock> rocks;
    std::vector<Shot> shots;
    std::vector<Crystal> crystals;
    std::vector<Spark> sparks;

    bool hole = false;
    float holeX = 0, holeY = 0, holeR = 0;

    float pulseT = -1, pulseX = 0, pulseY = 0;
    std::vector<int> pushed;

    std::atomic<uint32_t> held{0};
    std::atomic<bool> fireNow{false}, pulseNow{false};
    bool finished = false;
};


const std::string &FPPAsteroidOrbit::getName() {
    static const std::string name = "Asteroid Orbit";
    return name;
}

void FPPAsteroidOrbit::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    AsteroidOrbitEffect *effect = dynamic_cast<AsteroidOrbitEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new AsteroidOrbitEffect(m, findIntOption("Asteroids", 0), findIntOption("Gravity", 100),
                                         findIntOption("Lives", 3));
        m->setRunningEffect(effect, 25);
    }
    // The button that starts the game counts too, so a direction held from
    // the first press thrusts straight away - except Start, which would
    // open the pause menu on a game that has only just begun.
    if (!starting || button.compare(0, 5, "Start") != 0) {
        effect->button(button);
    }
}
