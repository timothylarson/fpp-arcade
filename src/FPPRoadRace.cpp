#include <fpp-pch.h>

#include "FPPRoadRace.h"
#include "ArcadeCanvas.h"
#include "ArcadeShow.h"
#include "RoadRaceShow.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;

// The wall is wide and short, so the road runs across it: your car sits a
// fifth of the way in from the left and the road scrolls past it, everything
// ahead coming in from the right. Up and Down change lane; the speed rises on
// its own with the distance, and alone on the D-pad Right puts your foot down
// and Left brakes. Traffic going your way is always slower than you, one
// speed per lane (the slow lane at the bottom), so nothing ever comes up from
// behind and cars in a lane never bump into each other. Further on come
// trucks, buses and the snowplough, and the top lane turns into oncoming
// traffic, each car warned of by a blinking light at the right edge. A crash
// costs a life and slows you down; presents on the road are points, and the
// odd turbo star makes you fast and unstoppable for a few seconds.
//
// Two players on one controller: the D-pad drives the red car and A/B the
// blue one (B up a lane, A down). Whoever presses first gets the whole wall;
// the other control joins at any time and the wall splits into two roads, red
// on top. Both then drive at the automatic speed, and the higher score when
// both are out wins.
//
// Distances are in metres and speeds in metres a second. A metre is a column
// on a 60-wide wall; narrower or wider walls scale it (cols()), so the same
// speed gives the same warning time however wide the road is.

namespace {

enum class Kind { Car, Van, Bus, Truck, Plough };

const RGB RED_CAR = {1.0f, 0.08f, 0.06f};
const RGB BLUE_CAR = {0.15f, 0.4f, 1.0f};
// Traffic is never red or blue, so the players' cars stand out.
const RGB TRAFFIC[] = {{1.0f, 0.85f, 0.1f}, {0.2f, 0.9f, 0.25f}, {0.85f, 0.85f, 0.85f}, {1.0f, 0.45f, 0.05f},
                       {0.75f, 0.25f, 1.0f}, {0.1f, 0.85f, 0.85f}, {1.0f, 0.4f, 0.65f}};
const RGB BUS = {0.15f, 0.75f, 0.3f};
const RGB PLOUGH = {1.0f, 0.42f, 0.0f};
const RGB GLASS = {0.43f, 0.59f, 0.78f};
const RGB HEADLIGHT = {1.0f, 0.94f, 0.75f};
const RGB TAIL = {0.75f, 0.06f, 0.03f};
const RGB WHEEL = {0.22f, 0.22f, 0.25f};
const RGB SNOW = {0.75f, 0.82f, 1.0f};
const RGB WARM = {1.0f, 0.75f, 0.35f};
const RGB AMBER = {1.0f, 0.6f, 0.0f};
const RGB GOLD = {1.0f, 0.8f, 0.15f};
const RGB PINE = {0.05f, 0.5f, 0.12f};
const RGB BULBS[] = {{1.0f, 0.1f, 0.05f}, {0.1f, 1.0f, 0.2f}, {1.0f, 0.75f, 0.1f}, {0.2f, 0.35f, 1.0f}};

// Difficulty bands, by the metres driven: 0 cars and vans; 1 trucks too; 2
// buses, the snowplough and oncoming traffic in the top lane; 3 and 4 just
// busier. The speed and the traffic rise smoothly to TOP_D, and after that
// keep creeping up (to MAX_SPEED and MAX_BUSY), so even a very good run ends.
const float BAND_AT[5] = {0, 450, 1000, 1700, 2500};
constexpr float TOP_D = 3000;
constexpr float MAX_SPEED = 70;
constexpr float MAX_BUSY = 1.4f;   // the traffic's busiest, as a multiple of how busy it is at TOP_D

constexpr float COUNT_STEP = 0.6f;          // each of 3, 2, 1 (the Start sound beeps with them)
constexpr float COUNTDOWN = 3 * COUNT_STEP;  // then GO
constexpr float CRASH_T = 1.0f;             // spinning after a crash
constexpr float GHOST_T = 1.6f;             // blinking, and can't be hit, after that
constexpr float BOOST_T = 3.5f;

enum : uint32_t { HELD_LEFT = 1, HELD_RIGHT = 2 };

uint32_t hash32(int32_t a, uint32_t salt) {
    uint32_t h = (uint32_t)a * 0x9E3779B1u ^ (salt + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

}  // namespace

class RoadRaceEffect : public FPPArcadeGameEffect {
public:
    RoadRaceEffect(PixelOverlayModel *m, int lives, int showMode, int goodMetres)
        : FPPArcadeGameEffect(m), endShow(showMode), goodGame(goodMetres), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(20, W);
        H = std::max(10, H);
        canvas.resize(W, H);
        startLives = std::clamp(lives, 1, 9);
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "RoadRace";
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
        const std::string n = b.substr(0, b.find(" - "));
        if (pressed || released) {
            // Right and Left are held to speed up and brake.
            uint32_t bits = 0;
            if (n == "Left" || n == "Up/Left" || n == "Down/Left") bits = HELD_LEFT;
            else if (n == "Right" || n == "Up/Right" || n == "Down/Right") bits = HELD_RIGHT;
            if (pressed) {
                held.fetch_or(bits);
            } else {
                held.fetch_and(~bits);
            }
        }
        const bool wasPaused = paused;
        FPPArcadeGameEffect::button(b);
        if (wasPaused || paused || n == "Start" || !pressed) {
            return;
        }
        // Played on the effect's own thread, in update(), so the game is
        // only ever changed in one place.
        std::lock_guard<std::mutex> l(pressLock);
        presses.push_back(n);
    }

    void restart() override {
        // The same players again, from the start.
        const bool was[2] = {roads[0].joined, roads[1].joined};
        newGame();
        for (int i = 0; i < 2; ++i) {
            if (was[i]) {
                join(i);
            }
        }
        resetFrameTimer();
    }

    int32_t update() override {
        if (finished) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return EFFECT_DONE;
        }
        if (show.on()) {
            if (show.step(model, consumeElapsedMs(FRAME_MS, 100.0))) {
                return FRAME_MS;
            }
            return finish();   // over: the game ends as it would have after GAME OVER
        }
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return FRAME_MS;
        }
        const float dt = (float)(consumeElapsedMs(FRAME_MS, 100.0) / 1000.0);
        clock += dt;
        std::vector<std::string> todo;
        {
            std::lock_guard<std::mutex> l(pressLock);
            todo.swap(presses);
        }
        const int cheat = takeCheat();
        if (!over) {
            if (cheat) {
                applyCheat(cheat);
            }
            for (auto &p : todo) {
                onPress(p);
            }
        }
        step(dt);
        if (show.on()) {
            resetFrameTimer();   // the game ended well: the show starts next frame
            return FRAME_MS;
        }
        if (over && overT > 5.0f) {
            return finish();
        }
        beginFrame();   // the picture, then the words over it: see beginFrame()
        render();
        endFrame();
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 25;

    struct Vehicle {
        Kind kind;
        int lane;
        float x;    // its left end, in columns
        int dir;    // 1 going your way, -1 oncoming
        RGB col;
        bool passed;
    };
    struct Pickup {
        int lane;
        float x;
        bool boost;   // a turbo star; otherwise a present
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
    };

    // One player's road: the whole wall alone, or half of it each.
    struct Road {
        bool joined = false, out = false;
        RGB colour = RED_CAR;
        int lives = 3;
        // Where it is on the wall: rows top..top+h-1, the kerbs, and the lanes
        // (each carH rows, with a row of markings between them).
        int top = 0, h = 0, lanes = 4, carH = 3, pitch = 4, kerbTop = 0, y0 = 1, kerbBot = 0;
        // The car.
        int lane = 1;
        float y = 0;   // its top row, sliding to its lane's
        float speed = 0;
        double dist = 0;    // metres driven: the distance that's scored
        double diffD = 0;   // what the difficulty goes by: dist, plus any cheat
        int bonus = 0;      // points for overtaking, presents and smashes
        int band = 0;
        float countT = 0, goT = 0, crashT = 0, ghostT = 0, boostT = 0, flashT = 0, outT = 0, bumpT = 0;
        float scroll = 0;   // columns the road has moved, for the markings and the scenery
        std::vector<Vehicle> traffic;
        std::vector<Pickup> pickups;
        std::mt19937 rng;
        double nextSpawn = 0, nextPickup = 0, nextOncoming = 0;
        int pickupsMade = 0;

        int score() const { return (int)dist + bonus; }
        bool driving() const { return joined && !out && countT <= 0; }
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    static float rnd(Road &r) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(r.rng); }

    // Columns per metre.
    float cols() const { return std::clamp(W / 60.0f, 0.5f, 2.0f); }

    // ---------------------------------------------------------------- game flow
    void newGame() {
        roads[0] = Road();
        roads[1] = Road();
        layout(roads[0], 0, H, 4);   // for the waiting screen
        two = false;
        over = false;
        overT = 0;
        decided = false;
        sparks.clear();
        seed = (uint32_t)rng();
        {
            std::lock_guard<std::mutex> l(pressLock);
            presses.clear();
        }
    }

    // Lays a road out in rows top..top+h-1 with up to wantLanes lanes. On a
    // 60x25 wall alone: a five-row verge of trees and lamp posts on top, four
    // lanes three rows deep, and a three-row verge with a string of lights
    // below. Half the wall each: three lanes two rows deep and a row of verge
    // either side.
    void layout(Road &r, int top, int h, int wantLanes) {
        r.top = top;
        r.h = h;
        int carH = std::clamp(h / 8, 2, 6);
        int lanes = wantLanes;
        while (lanes > 2 && lanes * (carH + 1) + 1 > h) {
            --lanes;
        }
        while (carH > 1 && lanes * (carH + 1) + 1 > h) {
            --carH;
        }
        r.lanes = lanes;
        r.carH = carH;
        r.pitch = carH + 1;
        const int verge = std::max(0, h - (lanes * r.pitch + 1));
        r.kerbTop = top + (verge * 5 + 4) / 8;   // a little more verge on top, for the trees
        r.y0 = r.kerbTop + 1;
        r.kerbBot = r.y0 + lanes * r.pitch - 1;
    }

    int laneY(const Road &r, int lane) const { return r.y0 + lane * r.pitch; }
    int carLen(const Road &r) const { return r.carH <= 2 ? 5 : 2 * r.carH; }
    int lenOf(const Road &r, Kind k) const {
        const int c = carLen(r);
        switch (k) {
        case Kind::Car: return c;
        case Kind::Van: return c + 1;
        case Kind::Bus: return c * 5 / 3;
        case Kind::Truck: return c * 2;
        case Kind::Plough: return c + 2;
        }
        return c;
    }
    int playerX() const { return (int)std::lround(W * 0.2f); }

    // A press from a control nobody's driving with brings in its car;
    // otherwise it changes lane. The D-pad is control 0 (the red car, on top
    // with two), A/B control 1 (blue, below).
    void onPress(const std::string &n) {
        int ctl = -1, move = 0;
        if (n == "Up" || n == "Up/Left" || n == "Up/Right") {
            ctl = 0;
            move = -1;
        } else if (n == "Down" || n == "Down/Left" || n == "Down/Right") {
            ctl = 0;
            move = 1;
        } else if (n == "Left" || n == "Right") {
            ctl = 0;
        } else if (n == "B Button") {
            ctl = 1;
            move = -1;
        } else if (n == "A Button" || n == "Fire" || n == "Fire Button") {
            ctl = 1;
            move = 1;
        }
        if (ctl < 0) {
            return;
        }
        Road &r = roads[ctl];
        if (!r.joined) {
            join(ctl);
            return;
        }
        if (r.out || r.crashT > 0 || move == 0) {
            return;
        }
        const int to = std::clamp(r.lane + move, 0, r.lanes - 1);
        if (to == r.lane) {
            if (r.bumpT <= 0) {   // against the kerb
                r.bumpT = 0.25f;
                sound("Bump", 0.4f);
            }
            return;
        }
        r.lane = to;
        sound("Lane", 0.3f);
    }

    void join(int i) {
        Road &r = roads[i], &o = roads[1 - i];
        const bool first = !o.joined;
        r = Road();
        r.joined = true;
        r.lives = startLives;
        r.colour = i == 0 ? RED_CAR : BLUE_CAR;
        r.rng.seed(seed);   // both roads roll the same dice, for a fair race
        r.nextSpawn = 40;
        r.nextPickup = 110;
        r.nextOncoming = 0;
        if (first) {
            two = false;
            layout(r, 0, H, 4);
        } else {
            // Split the wall: red on top, blue below, a dark row between.
            two = true;
            const int half = (H - 1) / 2;
            layout(roads[0], 0, half, 3);
            layout(roads[1], H - half, half, 3);
            refit(o);
        }
        r.lane = r.lanes / 2;
        r.y = (float)laneY(r, r.lane);
        if (!first && o.countT > 0) {
            r.countT = o.countT;   // joined during the other's countdown: off together
            sound("Join");
        } else {
            r.countT = COUNTDOWN;
            if (first) {
                sfxStop("roadrace");   // the title jingle
            }
            sound("Start");
        }
    }

    // A road that was the whole wall, squeezed into half of it when the
    // second car joins: anything in a lane that's gone goes with it, and the
    // car has a moment to find its feet.
    void refit(Road &r) {
        r.lane = std::min(r.lane, r.lanes - 1);
        r.y = (float)laneY(r, r.lane);
        r.traffic.erase(std::remove_if(r.traffic.begin(), r.traffic.end(), [&](const Vehicle &v) { return v.lane >= r.lanes; }),
                        r.traffic.end());
        r.pickups.erase(std::remove_if(r.pickups.begin(), r.pickups.end(), [&](const Pickup &p) { return p.lane >= r.lanes; }),
                        r.pickups.end());
        if (r.driving()) {
            r.ghostT = std::max(r.ghostT, 1.5f);
        }
    }

    // The cheats move the difficulty, not the distance scored: the next band,
    // or the top one with lives topped up.
    void applyCheat(int cheat) {
        for (auto &r : roads) {
            if (!r.joined || r.out) {
                continue;
            }
            if (cheat == CHEAT_NEXT_LEVEL) {
                const int b = std::min(4, r.band + 1);
                r.diffD = std::max(r.diffD, (double)BAND_AT[b]);
                if (r.band == 4) {
                    r.diffD = std::max(r.diffD, (double)TOP_D);
                }
            } else if (cheat == CHEAT_LAST_LEVEL) {
                r.diffD = std::max(r.diffD, (double)TOP_D);
                r.lives = std::max(r.lives, 9);
            }
        }
    }

    // ---------------------------------------------------------------- speeds
    float cruise(const Road &r) const {
        const float t = (float)std::min(1.0, r.diffD / TOP_D);
        const float beyond = (float)std::max(0.0, r.diffD - TOP_D) * 0.008f;
        return std::min(MAX_SPEED, 17.0f + 21.0f * t + beyond);
    }
    int bandOf(const Road &r) const {
        int b = 0;
        while (b < 4 && r.diffD >= BAND_AT[b + 1]) {
            ++b;
        }
        return b;
    }
    // Oncoming traffic in the top lane, from band 2 - on a road of four lanes
    // or more. Half the wall each has three, and giving one of them up would
    // leave the traffic in single file.
    bool oncomingOn(const Road &r) const { return r.band >= 2 && r.lanes >= 4; }
    // Traffic speed as a share of the cruising speed: the top lane going
    // your way fastest (0.56), the bottom slowest (0.38); oncoming is -0.5.
    // Close together, so the gaps between lanes don't drift shut.
    float laneFactor(const Road &r, int lane, int dir) const {
        if (dir < 0) {
            return -0.5f;
        }
        const int first = oncomingOn(r) ? 1 : 0, n = r.lanes - first;
        const float up = n > 1 ? std::clamp((float)(r.lanes - 1 - lane) / (n - 1), 0.0f, 1.0f) : 0.5f;
        return 0.38f + 0.18f * up;
    }
    float vehicleSpeed(const Road &r, const Vehicle &v) const { return laneFactor(r, v.lane, v.dir) * cruise(r); }

    // ---------------------------------------------------------------- stepping
    void step(float dt) {
        stepSparks(dt);
        engine(dt);
        for (int i = 0; i < 2; ++i) {
            stepRoad(roads[i], dt);
        }
        if (!over) {
            const bool anyone = roads[0].joined || roads[1].joined;
            const bool allOut = (!roads[0].joined || roads[0].out) && (!roads[1].joined || roads[1].out);
            if (anyone && allOut) {
                over = true;
                overT = 0;
                sfxStop("roadrace");
            }
            return;
        }
        overT += dt;
        if (!decided && overT > 0.6f) {
            decided = true;
            gameOver();
        }
    }

    void stepRoad(Road &r, float dt) {
        if (!r.joined) {
            r.scroll += 9.0f * cols() * dt;   // the waiting screen's road drifts by
            return;
        }
        r.goT = std::max(0.0f, r.goT - dt);
        r.flashT = std::max(0.0f, r.flashT - dt);
        r.bumpT = std::max(0.0f, r.bumpT - dt);
        r.ghostT = std::max(0.0f, r.ghostT - dt);
        r.boostT = std::max(0.0f, r.boostT - dt);
        const float c = cruise(r), k = cols();

        // The car's speed: the cruising speed, pushed up by a turbo or (alone
        // on the D-pad) Right, held down by Left. Braking stops short of the
        // traffic's speed, so nothing ever catches you from behind.
        float want = c;
        if (r.out || r.countT > 0) {
            want = 0;
        } else if (r.crashT > 0) {
            want = 0.4f * c;
        } else if (r.boostT > 0) {
            want = 1.5f * c;
        } else if (!two && &r == &roads[0]) {
            const uint32_t h = held.load();
            if (h & HELD_RIGHT) {
                want = 1.3f * c;
            } else if (h & HELD_LEFT) {
                want = 0.72f * c;
            }
        }
        const float accel = r.crashT > 0 ? 40.0f : (want > r.speed ? 14.0f : 22.0f);
        r.speed += std::clamp(want - r.speed, -accel * dt, accel * dt);

        if (r.countT > 0) {
            r.countT -= dt;
            if (r.countT <= 0) {
                r.goT = COUNT_STEP;
            }
        }
        if (r.crashT > 0) {
            r.crashT -= dt;
            if (r.crashT <= 0 && r.lives <= 0) {
                r.out = true;
                r.outT = 0;
                const Road &o = &r == &roads[0] ? roads[1] : roads[0];
                if (o.joined && !o.out) {
                    sound("Out");
                }
            }
        }
        if (r.out) {
            r.outT += dt;
            // The wreck smokes.
            if (rnd() < dt * 8.0f) {
                sparks.push_back({playerX() + carLen(r) * 0.5f, r.y, (rnd() - 0.5f) * 2.0f, -3.0f - rnd() * 2.0f, 0, 0.8f,
                                  {0.35f, 0.35f, 0.38f}});
            }
        }

        const float ds = r.speed * dt;
        r.dist += r.out ? 0 : ds;
        r.diffD += r.out ? 0 : ds;
        r.scroll += ds * k;
        const int band = bandOf(r);
        if (band > r.band) {
            r.band = band;
            r.flashT = 0.6f;
            sound("Faster", 0.5f);
        }

        // Sliding across to the lane.
        const float ty = (float)laneY(r, r.lane), slide = r.pitch / 0.12f * dt;
        r.y += std::clamp(ty - r.y, -slide, slide);

        // Everything moves past at the difference in speed; presents lie still on the road.
        for (auto &v : r.traffic) {
            v.x -= (r.speed - vehicleSpeed(r, v)) * k * dt;
        }
        for (auto &p : r.pickups) {
            p.x -= r.speed * k * dt;
        }
        r.traffic.erase(std::remove_if(r.traffic.begin(), r.traffic.end(),
                                       [&](const Vehicle &v) { return v.x + lenOf(r, v.kind) < -2 || v.x > W + 120; }),
                        r.traffic.end());
        r.pickups.erase(std::remove_if(r.pickups.begin(), r.pickups.end(), [&](const Pickup &p) { return p.x < -4; }),
                        r.pickups.end());

        if (r.out || r.countT > 0) {
            return;
        }
        spawnTraffic(r);
        spawnOncoming(r);
        spawnPickup(r);
        collide(r);
    }

    // ---------------------------------------------------------------- traffic
    Kind pickKind(Road &r, int band) {
        const float p = rnd(r);
        switch (band) {
        case 0: return p < 0.85f ? Kind::Car : Kind::Van;
        case 1: return p < 0.6f ? Kind::Car : (p < 0.82f ? Kind::Van : Kind::Truck);
        default:
            if (p < 0.46f) return Kind::Car;
            if (p < 0.63f) return Kind::Van;
            if (p < 0.78f) return Kind::Truck;
            if (p < 0.91f) return Kind::Bus;
            return Kind::Plough;
        }
    }

    // A new vehicle at the right edge every so many metres - closer together
    // the further you go - in a lane where it leaves at least one lane going
    // your way clear around it, so there's always a way through. The
    // oncoming lane never counts as the way through.
    void spawnTraffic(Road &r) {
        if (r.dist < r.nextSpawn) {
            return;
        }
        const float t = (float)std::min((double)MAX_BUSY, r.diffD / TOP_D);
        const int first = oncomingOn(r) ? 1 : 0, n = r.lanes - first;
        // Metres between them, for each lane going your way: a little roomier
        // with fewer lanes (fewer ways round), and for the first few hundred
        // metres while you get the feel of it.
        const float gap = (30.0f - 14.0f * t) * 4.0f / std::max(1, n) * (n >= 4 ? 0.85f : 1.15f) * (r.diffD < 300 ? 1.3f : 1.0f);
        r.nextSpawn = r.dist + gap * (0.6f + 0.8f * rnd(r));
        Kind kind = pickKind(r, r.band);
        RGB col = TRAFFIC[(int)(rnd(r) * 7) % 7];
        if (kind == Kind::Bus) col = BUS;
        if (kind == Kind::Plough) col = PLOUGH;
        const int start = (int)(rnd(r) * n) % n;
        const float x = W + 1 + rnd(r) * 4.0f;
        for (int j = 0; j < n; ++j) {
            const int lane = first + (start + j) % n;
            // The snowplough keeps to the slow lane.
            if (kind == Kind::Plough && lane != r.lanes - 1) {
                continue;
            }
            if (canPlace(r, lane, x, lenOf(r, kind))) {
                r.traffic.push_back({kind, lane, x, 1, col, false});
                return;
            }
        }
        r.nextSpawn = r.dist + 4;   // no room: try again in a moment
    }

    // Whether a vehicle can go in at x0: not too close behind the last one in
    // its lane, and never - all the way from here until it's past you - part
    // of a wall across every lane going your way. Lanes move at different
    // speeds, so two that were well apart when they came in can drift into
    // line later on; looking ahead catches those too. (Their speeds relative
    // to each other don't depend on yours, so it can be worked out now.)
    bool canPlace(const Road &r, int lane, float x0, int len) const {
        const float k = cols(), m = 10.0f * k, c = cruise(r);
        const float vA = laneFactor(r, lane, 1) * c;
        uint32_t always = 1u << lane;
        if (oncomingOn(r)) {
            always |= 1u;   // never counted on as the way through
        }
        for (const auto &v : r.traffic) {
            if (v.dir > 0 && v.lane == lane && v.x + lenOf(r, v.kind) > x0 - 4 && v.x < x0 + len + 4) {
                return false;   // too close behind the one already in the lane
            }
        }
        const float closing = std::max(1.0f, (std::max(r.speed, c) - vA) * k);
        const float until = std::min(8.0f, (x0 + len - playerX()) / closing);
        for (float t = 0; t <= until; t += 0.1f) {
            uint32_t blocked = always;
            for (const auto &v : r.traffic) {
                if (v.dir < 0) {
                    continue;
                }
                const float dx = v.x - x0 + (vehicleSpeed(r, v) - vA) * k * t;   // where it is, from the new one
                if (dx + lenOf(r, v.kind) > -m && dx < len + m) {
                    blocked |= 1u << v.lane;
                }
            }
            int count = 0;
            for (int l = 0; l < r.lanes; ++l) {
                count += (blocked >> l) & 1;
            }
            if (count >= r.lanes) {
                return false;
            }
        }
        return true;
    }

    // Oncoming cars in the top lane, now and then, each warned of by a
    // blinking light at the right edge for most of a second before it's
    // on the wall. Only when the lane below is clear beside you for as long
    // as it takes to go by, so there's always somewhere to get out of its way.
    void spawnOncoming(Road &r) {
        if (!oncomingOn(r) || r.dist < r.nextOncoming) {
            return;
        }
        const float k = cols(), c = cruise(r);
        const float closing = (r.speed + 0.5f * c) * k;
        const float lead = 0.8f * closing;
        const float x0 = W + lead;
        const float passT = (x0 - playerX()) / std::max(1.0f, closing);
        const int px = playerX(), L = carLen(r);
        bool clear = true;
        for (const auto &v : r.traffic) {
            const float a = v.x, b = v.x + lenOf(r, v.kind);
            if (v.dir < 0 || (v.lane == 0 && b > px - 2)) {
                clear = false;   // one's already coming, or someone's still in the lane
            }
            if (v.dir > 0 && v.lane == 1) {
                const float drift = (r.speed - vehicleSpeed(r, v)) * k * (passT + 0.3f);
                if (b > px - 3 && a - drift < px + L + 3) {
                    clear = false;   // the lane below won't be free beside you
                }
            }
        }
        if (!clear) {
            r.nextOncoming = r.dist + 8;
            return;
        }
        const float t = (float)std::min((double)MAX_BUSY, r.diffD / TOP_D);
        r.nextOncoming = r.dist + std::max(35.0f, 140.0f - 80.0f * t) * (0.7f + 0.6f * rnd(r));
        const Kind kind = rnd(r) < 0.75f ? Kind::Car : Kind::Van;
        r.traffic.push_back({kind, 0, x0, -1, TRAFFIC[(int)(rnd(r) * 7) % 7], false});
        sound("Horn", 0.45f);
    }

    // Presents fallen on the road, and every fifth one a turbo star instead.
    void spawnPickup(Road &r) {
        if (r.dist < r.nextPickup) {
            return;
        }
        r.nextPickup = r.dist + 110 + 90 * rnd(r);
        const bool boost = ++r.pickupsMade % 5 == 0;
        const int lane = (int)(rnd(r) * r.lanes) % r.lanes;
        if (boost && lane == 0 && oncomingOn(r)) {
            return;
        }
        const float x = (float)W + 1;
        for (const auto &v : r.traffic) {
            if (v.lane == lane && v.x + lenOf(r, v.kind) > x - 6 && v.x < x + 8) {
                return;   // something's on it: this one's missed
            }
        }
        r.pickups.push_back({lane, x, boost});
    }

    int pickupSize(const Road &r) const { return std::max(2, r.carH - 1); }

    // Overlaps by most of an LED count, so grazing a corner doesn't.
    void collide(Road &r) {
        const int px = playerX(), L = carLen(r);
        for (size_t i = 0; i < r.traffic.size();) {
            Vehicle &v = r.traffic[i];
            const int len = lenOf(r, v.kind);
            const float vy = (float)laneY(r, v.lane);
            if (v.dir > 0 && !v.passed && v.x + len <= px) {
                v.passed = true;
                r.bonus += 10;
                if (clock - passSfxAt > 0.15f) {
                    passSfxAt = clock;
                    sound("Pass", 0.3f);
                }
            } else if (v.dir < 0 && !v.passed && v.x + len <= px) {
                v.passed = true;
                sound("Pass", 0.35f);
            }
            const float ox = std::min((float)(px + L), v.x + len) - std::max((float)px, v.x);
            const float oy = std::min(r.y + r.carH, vy + r.carH) - std::max(r.y, vy);
            // Only something you run into counts. After a crash you're slower
            // than the traffic for a moment, and a car coming up from behind
            // then just goes through.
            const bool intoIt = v.dir < 0 || r.speed >= vehicleSpeed(r, v);
            if (ox > 0.6f && oy > 0.6f && intoIt && r.crashT <= 0 && r.ghostT <= 0) {
                const float hx = std::max((float)px, v.x) + 0.5f, hy = std::max(r.y, vy) + oy * 0.5f;
                burst(hx, hy, v.col, 10);
                if (r.boostT > 0) {
                    // Turbo: it's knocked out of the way.
                    burst(hx, hy, {1, 1, 1}, 6);
                    r.bonus += 25;
                    sound("Smash");
                } else {
                    burst(hx, hy, {1.0f, 0.6f, 0.1f}, 14);
                    crash(r);
                }
                r.traffic.erase(r.traffic.begin() + i);
                continue;
            }
            ++i;
        }
        const int ps = pickupSize(r);
        for (size_t i = 0; i < r.pickups.size();) {
            const Pickup &p = r.pickups[i];
            const float py = laneY(r, p.lane) + (r.carH - ps) * 0.5f;
            const float ox = std::min((float)(px + L), p.x + ps) - std::max((float)px, p.x);
            const float oy = std::min(r.y + r.carH, py + ps) - std::max(r.y, py);
            if (ox > 0.3f && oy > 0.3f && r.crashT <= 0) {
                if (p.boost) {
                    r.boostT = BOOST_T;
                    sound("Boost");
                    burst(p.x + 1, py + 1, {0.3f, 1.0f, 1.0f}, 14);
                } else {
                    r.bonus += 50;
                    sound("Pickup");
                    burst(p.x + 1, py + 1, GOLD, 12);
                }
                r.pickups.erase(r.pickups.begin() + i);
                continue;
            }
            ++i;
        }
    }

    void crash(Road &r) {
        --r.lives;
        r.crashT = CRASH_T;
        r.ghostT = CRASH_T + GHOST_T;
        r.boostT = 0;
        sound("Crash");
    }

    // The end: the best kept is the score (the better one with two), and a
    // good game drives goodGame metres.
    void gameOver() {
        const bool duo = roads[0].joined && roads[1].joined;
        const int s0 = roads[0].score(), s1 = roads[1].score();
        const int score = duo ? std::max(s0, s1) : (roads[0].joined ? s0 : s1);
        const int metres = (int)std::max(roads[0].dist, roads[1].dist);
        int best = 0;
        const bool newBest = arcadeRecordBest(model->getName(), "Road Race", score, &best);
        winner = !duo ? 0 : (s0 == s1 ? 0 : (s0 > s1 ? 1 : 2));
        if (arcadeWantShow(endShow, metres >= goodGame, newBest)) {
            sfxEndShow("roadrace");
            if (duo) {
                show.start(model, RoadRaceShow::config2(s0, s1, winner, newBest, best),
                           std::make_unique<RoadRaceShow::Story>(2, winner));
            } else {
                const int who = roads[0].joined ? 1 : 2;
                show.start(model, RoadRaceShow::config(score, metres, newBest, best, who),
                           std::make_unique<RoadRaceShow::Story>(1, who));
            }
            return;
        }
        sound("GameOver");
    }

    // ---------------------------------------------------------------- sounds
    void sound(const char *n, float gain = 1.0f) { sfx("roadrace", n, gain); }

    // A soft engine hum, a short clip again and again, pitched by the speed
    // of the fastest car on the wall.
    void engine(float dt) {
        engineT -= dt;
        if (engineT > 0 || over) {
            return;
        }
        float v = 0;
        for (const auto &r : roads) {
            if (r.joined && !r.out) {
                v = std::max(v, r.speed);
            }
        }
        if (v < 2.0f) {
            return;
        }
        engineT = 0.3f;
        const int note = std::clamp((int)((v - 6.0f) / 8.0f), 0, 5);
        static const char *ENGINE[6] = {"Engine1", "Engine2", "Engine3", "Engine4", "Engine5", "Engine6"};
        sound(ENGINE[note], 0.25f);
    }

    // ---------------------------------------------------------------- sparks
    void burst(float x, float y, const RGB &col, int n) {
        for (int i = 0; i < n; ++i) {
            const float a = rnd() * 6.2832f, sp = 4.0f + rnd() * 9.0f;
            sparks.push_back({x, y, std::cos(a) * sp - 6.0f, std::sin(a) * sp * 0.6f, 0, 0.3f + rnd() * 0.4f, col});
        }
    }
    void stepSparks(float dt) {
        for (auto &s : sparks) {
            s.age += dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark &s) { return s.age >= s.life; }), sparks.end());
    }

    // ---------------------------------------------------------------- drawing
    void render() {
        canvas.clear();
        if (!roads[0].joined && !roads[1].joined) {
            drawRoad(roads[0], 1.0f);   // waiting for the first press
            canvas.writeTo(model);
            textBox({"ROAD", "RACE"}, 255, 255, 255);
            return;
        }
        for (int i = 0; i < 2; ++i) {
            const Road &r = roads[i];
            if (!r.joined) {
                continue;
            }
            const float dim = r.out ? std::max(0.35f, 1.0f - r.outT) : 1.0f;
            drawRoad(r, dim);
            drawScore(r);
            drawPickups(r, dim);
            drawTraffic(r, dim);
            drawPlayer(r);
        }
        for (auto &s : sparks) {
            canvas.add((int)std::lround(s.x), (int)std::lround(s.y), s.col, 1.0f - s.age / s.life);
        }
        canvas.writeTo(model);

        if (over && overT > 0.6f) {
            if (winner == 0 && roads[0].joined && roads[1].joined) {
                textBox({"TIE", "GAME", scoresText()}, 255, 255, 255);
            } else if (winner) {
                textBox({winner == 1 ? "RED" : "BLUE", "WINS", scoresText()}, 255, 220, 0);
            } else {
                textBox({"GAME", "OVER", std::to_string(roads[0].joined ? roads[0].score() : roads[1].score())}, 255, 255, 255);
            }
            return;
        }
        for (int i = 0; i < 2; ++i) {
            const Road &r = roads[i];
            if (!r.joined) {
                continue;
            }
            if (r.countT > 0) {
                bandText(r, std::to_string((int)std::ceil(r.countT / COUNT_STEP)), 255, 255, 255);
            } else if (r.goT > 0) {
                bandText(r, "GO!", 80, 255, 80);
            } else if (r.out && two) {
                // Small and dim: the other road is still racing.
                bandText(r, "OUT", (int)(r.colour.r * 150), (int)(r.colour.g * 150), (int)(r.colour.b * 150), 1);
            }
        }
    }

    std::string scoresText() const { return std::to_string(roads[0].score()) + ":" + std::to_string(roads[1].score()); }

    // The kerbs, the lane markings (amber beside the oncoming lane) and the
    // snowy verges with their trees, lamp posts and lights, all scrolling.
    // The road itself stays black, and everything on it but the traffic is
    // sparse and dim: on the real wall any filled background washes out the
    // cars, which have to stand out.
    void drawRoad(const Road &r, float dim) {
        const int base = (int)std::floor(r.scroll);
        const bool oncoming = r.joined && oncomingOn(r);
        for (int x = 0; x < W; ++x) {
            const int wc = base + x;
            if (wc % 2 == 0) {   // the kerbs, dotted
                canvas.set(x, r.kerbTop, SNOW, 0.1f * dim);
                canvas.set(x, r.kerbBot, SNOW, 0.1f * dim);
            }
            for (int l = 1; l < r.lanes; ++l) {
                const int y = laneY(r, l) - 1;
                if (l == 1 && oncoming) {
                    if (wc % 2 == 0) {
                        canvas.set(x, y, AMBER, 0.2f * dim);
                    }
                } else if (wc % 6 < 2) {
                    canvas.set(x, y, SNOW, (r.flashT > 0 ? 0.35f : 0.14f) * dim);
                }
            }
        }
        drawTopVerge(r, base, dim);
        drawBottomVerge(r, base, dim);
    }

    float twinkle(int wc) const { return 0.55f + 0.45f * std::sin(clock * 3.0f + wc * 1.7f); }

    void drawTopVerge(const Road &r, int base, float dim) {
        const int v0 = r.top, v1 = r.kerbTop - 1, rows = v1 - v0 + 1;
        if (rows <= 0) {
            return;
        }
        for (int x = 0; x < W; ++x) {
            const int wc = base + x;
            if (hash32(wc, 3) % 3 == 0) {
                canvas.set(x, v1, SNOW, 0.06f * dim);   // a sprinkle of snow
            }
            if (rows == 1) {
                // A string of lights along the edge.
                if (wc % 4 == 0) {
                    canvas.set(x, v1, BULBS[(wc / 4) % 4], 0.35f * twinkle(wc) * dim);
                }
                continue;
            }
            // Stars, far away, so they stay put.
            for (int y = v0; y <= v1 - 2; ++y) {
                if (hash32(x * 53 + y * 7, 11) % 37 == 0) {
                    canvas.add(x, y, {0.8f, 0.85f, 1.0f}, (0.06f + 0.06f * std::sin(clock * 2.0f + x)) * dim);
                }
            }
        }
        if (rows < 3) {
            return;
        }
        // Trees, lamp posts and snowmen, one or none in each 8-column slot.
        const int SLOT = 8;
        for (int s = base / SLOT - 1; s <= (base + W) / SLOT + 1; ++s) {
            const uint32_t h = hash32(s, 1);
            const int type = h % 8;
            const int sx = s * SLOT + (int)((h >> 8) % 3) - base;
            if (type <= 2) {
                drawTree(sx, v1, std::min(rows, 5), s, dim);
            } else if (type == 3 || type == 4) {
                drawLamp(sx, v0, v1, dim);
            } else if (type == 5 && rows >= 4) {
                drawSnowman(sx, v1, dim);
            }
        }
    }

    void drawTree(int sx, int bottom, int ht, int s, float dim) {
        static const char *TALL[5] = {"..*..", "..g..", ".ggg.", "ggggg", "..t.."};
        static const char *SHORT[3] = {".g.", "ggg", ".t."};
        const char *const *shape = ht >= 5 ? TALL : SHORT;
        const int n = ht >= 5 ? 5 : 3, w = ht >= 5 ? 5 : 3;
        for (int row = 0; row < n; ++row) {
            for (int i = 0; i < w; ++i) {
                const char ch = shape[row][i];
                const int x = sx + i, y = bottom - (n - 1) + row;
                if (ch == '*') {
                    canvas.set(x, y, GOLD, 0.5f * dim);
                } else if (ch == 't') {
                    canvas.set(x, y, {0.35f, 0.18f, 0.05f}, 0.3f * dim);
                } else if (ch == 'g') {
                    const uint32_t b = hash32(s * 31 + row * 7 + i, 5);
                    if (b % 3 == 0) {
                        canvas.set(x, y, BULBS[(b >> 4) % 4], 0.4f * twinkle(s * 5 + row + i) * dim);
                    } else {
                        canvas.set(x, y, PINE, 0.25f * dim);
                    }
                }
            }
        }
    }

    // A lamp post: a dim pole and the lamp, leaning out over the road.
    void drawLamp(int sx, int v0, int v1, float dim) {
        for (int y = v0; y <= v1; ++y) {
            canvas.set(sx, y, {0.3f, 0.3f, 0.36f}, 0.2f * dim);
        }
        canvas.set(sx + 1, v0, WARM, 0.6f * dim);
        canvas.set(sx + 2, v0, WARM, 0.3f * dim);
    }

    void drawSnowman(int sx, int bottom, float dim) {
        for (int i = 0; i < 3; ++i) {
            canvas.set(sx + i, bottom, {1, 1, 1}, 0.25f * dim);
        }
        canvas.set(sx + 1, bottom - 1, {1, 1, 1}, 0.25f * dim);
        canvas.set(sx + 1, bottom - 2, {1, 1, 1}, 0.25f * dim);
        canvas.set(sx + 2, bottom - 2, {1, 0.45f, 0.05f}, 0.45f * dim);   // the carrot
        canvas.set(sx + 1, bottom - 3, {1, 0.05f, 0.05f}, 0.4f * dim);    // a Santa hat
    }

    void drawBottomVerge(const Road &r, int base, float dim) {
        const int v0 = r.kerbBot + 1, v1 = r.top + r.h - 1, rows = v1 - v0 + 1;
        if (rows <= 0) {
            return;
        }
        for (int x = 0; x < W; ++x) {
            const int wc = base + x;
            if (hash32(wc, 9) % 3 == 0) {
                canvas.set(x, v1, SNOW, 0.06f * dim);
            }
            if (rows == 1) {
                // Reflector posts.
                if (wc % 6 == 0) {
                    canvas.set(x, v1, {1, 1, 1}, 0.25f * dim);
                }
                continue;
            }
            // A string of lights along the verge.
            const int ly = rows >= 3 ? v0 + 1 : v0;
            if (wc % 3 == 0) {
                canvas.set(x, ly, BULBS[(wc / 3) % 4], 0.35f * twinkle(wc) * dim);
            }
        }
        if (rows < 3) {
            return;
        }
        // Snowy bushes now and then, in front of the lights.
        const int SLOT = 11;
        for (int s = base / SLOT - 1; s <= (base + W) / SLOT + 1; ++s) {
            const uint32_t h = hash32(s, 13);
            if (h % 3 != 0) {
                continue;
            }
            const int sx = s * SLOT + (int)((h >> 8) % 5) - base;
            for (int i = 0; i < 3; ++i) {
                canvas.set(sx + i, v1 - 1, SNOW, 0.15f * dim);
                canvas.set(sx + i, v1, PINE, 0.25f * dim);
            }
        }
    }

    // The score, dim: top right in the verge when it's tall enough, else on
    // the road behind your car, under the traffic. Lives as dots top left.
    void drawScore(const Road &r) {
        const std::string s = std::to_string(r.score());
        const int tw = (int)s.size() * 4 - 1;
        const RGB col = ArcadeCanvas::mix(r.colour, {1, 1, 1}, 0.55f);
        if (r.kerbTop - r.top >= 5) {
            const int x0 = W - tw;
            for (int y = r.top; y < r.top + 5; ++y) {
                for (int x = x0 - 1; x < W; ++x) {
                    canvas.set(x, y, {0, 0, 0});
                }
            }
            text(s, x0, r.top, col, 0.4f);
        } else {
            text(s, 0, r.y0, col, 0.3f);
        }
        if (r.kerbTop > r.top && !r.out) {
            for (int i = 0; i < std::min(r.lives - 1, 8); ++i) {   // the spare ones
                canvas.set(2 * i, r.top, {0, 0, 0});
                canvas.set(2 * i, r.top, r.colour, 0.5f);
            }
        }
    }

    void text(const std::string &s, int x, int y, const RGB &col, float k) {
        for (size_t i = 0; i < s.size(); ++i) {
            for (int gy = 0; gy < 5; ++gy) {
                for (int gx = 0; gx < 3; ++gx) {
                    if (arcadeGlyph(s[i], gx, gy)) {
                        canvas.set(x + (int)i * 4 + gx, y + gy, col, k);
                    }
                }
            }
        }
    }

    // A present (red and green, with a gold ribbon) or a turbo star (cyan and
    // white, blinking).
    void drawPickups(const Road &r, float dim) {
        const int ps = pickupSize(r);
        for (const auto &p : r.pickups) {
            const int x0 = (int)std::lround(p.x), y0 = laneY(r, p.lane) + (r.carH - ps) / 2;
            const bool flash = std::fmod(clock + p.x * 0.05f, 0.4f) < 0.2f;
            for (int dy = 0; dy < ps; ++dy) {
                for (int dx = 0; dx < ps; ++dx) {
                    RGB c;
                    if (p.boost) {
                        const bool arm = dx == ps / 2 || dy == ps / 2 || ps == 2;
                        if (!arm) {
                            continue;
                        }
                        c = flash ? RGB{1, 1, 1} : RGB{0.2f, 1.0f, 1.0f};
                    } else if (ps == 2) {
                        c = (dx == 1 && dy == 0) ? GOLD : (p.lane % 2 ? RGB{0.1f, 0.9f, 0.2f} : RGB{1, 0.1f, 0.1f});
                    } else {
                        const bool ribbon = dx == ps / 2 || dy == ps / 2;
                        c = ribbon ? GOLD : (p.lane % 2 ? RGB{0.1f, 0.9f, 0.2f} : RGB{1, 0.1f, 0.1f});
                    }
                    canvas.set(x0 + dx, y0 + dy, c, (flash && !p.boost && dx == 0 && dy == 0 ? 1.0f : 0.85f) * dim);
                }
            }
        }
    }

    void drawTraffic(const Road &r, float dim) {
        for (const auto &v : r.traffic) {
            const int len = lenOf(r, v.kind), y = laneY(r, v.lane);
            if (v.dir < 0 && v.x >= W) {
                // Oncoming, not on the wall yet: a blinking warning at the edge.
                if (std::fmod(clock, 0.24f) < 0.14f) {
                    for (int row = 0; row < r.carH; ++row) {
                        canvas.set(W - 1, y + row, AMBER, 0.9f * dim);
                    }
                }
                continue;
            }
            drawVehicle(v.kind, (int)std::floor(v.x), y, r.carH, len, v.dir, v.col, 0.8f * dim);
            if (v.dir < 0) {
                // Its headlights shine ahead of it, towards you.
                const int hy = y + std::max(0, r.carH / 2);
                for (int d = 1; d <= 4; ++d) {
                    canvas.add((int)std::floor(v.x) - d, hy, HEADLIGHT, 0.2f * (5 - d) / 4.0f * dim);
                }
            }
        }
    }

    void drawPlayer(const Road &r) {
        const int px = playerX(), L = carLen(r), y = (int)std::lround(r.y);
        if (r.out) {
            // The wreck, dark and still where it stopped.
            drawVehicle(Kind::Car, px, y, r.carH, L, 1, {0.3f, 0.3f, 0.3f}, std::max(0.25f, 0.7f - r.outT * 0.3f), false);
            return;
        }
        int dir = 1;
        RGB col = r.colour;
        if (r.crashT > 0) {
            // Spinning: facing back and forth, flashing white.
            dir = (int)(r.crashT * 12) % 2 ? -1 : 1;
            if ((int)(r.crashT * 16) % 2) {
                col = {1, 1, 1};
            }
        } else if (r.ghostT > 0 && std::fmod(r.ghostT, 0.24f) < 0.1f) {
            return;   // blinking: can't be hit yet
        }
        if (r.boostT > 0) {
            // Turbo: shimmering, with flames out of the back.
            col = ArcadeCanvas::mix(col, {1, 1, 1}, 0.25f + 0.25f * std::sin(clock * 30.0f));
            const int fy = y + r.carH / 2;
            for (int d = 1; d <= 3; ++d) {
                const float f = 0.5f + 0.5f * std::sin(clock * 40.0f + d * 2.0f);
                canvas.set(px - d, fy, d == 1 ? RGB{1, 0.9f, 0.4f} : RGB{1, 0.4f, 0.05f}, (0.9f - d * 0.22f) * f);
            }
        }
        if (r.crashT <= 0) {
            // The headlights' beam on the road ahead.
            const int hy = y + std::max(0, r.carH / 2);
            for (int d = 1; d <= 4; ++d) {
                canvas.add(px + L - 1 + d, hy, HEADLIGHT, 0.15f * (4 - d) / 3.0f);
            }
        }
        drawVehicle(Kind::Car, px, y, r.carH, L, dir, col, 1.0f, true);
    }

    // Vehicles LED by LED, as Frogger draws them, from the side: the body
    // along the middle with a headlight at the front and a tail light at
    // the back, the cab, windows or trailer above, and the wheels below on
    // anything three rows tall. A car has a cab over its middle, a van a long
    // box, a bus a row of windows, a truck a cab, a gap and a trailer, and the
    // snowplough a flashing beacon and a yellow blade.
    void drawVehicle(Kind kind, int x0, int y0, int h, int len, int dir, const RGB &body, float k, bool player = false) {
        const RGB dark = ArcadeCanvas::mix(body, {0, 0, 0}, 0.45f), light = ArcadeCanvas::mix(body, {1, 1, 1}, 0.3f);
        // i counts from the back of the vehicle to its front; row 0 is its top.
        auto put = [&](int i, int row, const RGB &c, float kk = 1.0f) {
            if (i < 0 || i >= len) {
                return;
            }
            canvas.set(dir > 0 ? x0 + i : x0 + len - 1 - i, y0 + row, c, k * kk);
        };
        const bool wheels = h >= 3;
        const int topRows = std::max(1, (h - (wheels ? 1 : 0)) / 2);
        const int bodyEnd = wheels ? h - 2 : h - 1;   // the last body row
        const int cabLen = std::max(2, len / 4);
        for (int i = 0; i < len; ++i) {
            const bool front = i == len - 1, back = i == 0;
            for (int row = 0; row < h; ++row) {
                if (row < topRows) {
                    switch (kind) {
                    case Kind::Car: {
                        const int c0 = len / 3, c1 = len - 2;
                        if (i >= c0 && i <= c1) {
                            put(i, row, i == c1 ? GLASS : light);
                        }
                        break;
                    }
                    case Kind::Van:
                        if (i <= len - 2) {
                            put(i, row, i == len - 2 ? GLASS : light);
                        }
                        break;
                    case Kind::Bus:
                        put(i, row, front ? GLASS : (back ? dark : (i % 2 ? GLASS : body)));
                        break;
                    case Kind::Truck:
                        if (i >= len - cabLen) {
                            put(i, row, front ? GLASS : body);
                        } else if (i < len - cabLen - 1) {
                            put(i, row, light);
                        }
                        break;
                    case Kind::Plough: {
                        const int c0 = len / 3, c1 = len - 3;
                        if (i == c0 && row == 0) {
                            const bool on = std::fmod(clock * 3.0f + x0 * 0.1f, 1.0f) < 0.5f;
                            put(i, row, AMBER, on ? 1.0f : 0.25f);   // the beacon
                        } else if (i > c0 && i <= c1) {
                            put(i, row, i == c1 ? GLASS : light);
                        } else if (front) {
                            put(i, row, {0.9f, 0.8f, 0.3f});   // the top of the blade
                        }
                        break;
                    }
                    }
                } else if (row <= bodyEnd) {
                    if (kind == Kind::Plough && front) {
                        put(i, row, {0.9f, 0.8f, 0.3f});
                    } else if (front) {
                        put(i, row, HEADLIGHT, player ? 1.0f : 0.9f);
                    } else if (back) {
                        put(i, row, TAIL);
                    } else if (kind == Kind::Truck && i < len - cabLen) {
                        put(i, row, i == len - cabLen - 1 ? dark : ArcadeCanvas::mix(light, dark, 0.4f));
                    } else {
                        put(i, row, body);
                    }
                } else {
                    // The wheels, under a dark sill.
                    bool wheel = i == 1 || i == len - 2;
                    if (kind == Kind::Truck) {
                        wheel = i == 1 || i == 2 || i == len - cabLen - 2 || i == len - 2;
                    } else if (kind == Kind::Bus) {
                        wheel = i == 1 || i == len - 3;
                    }
                    if (kind == Kind::Plough && front) {
                        put(i, row, {0.9f, 0.8f, 0.3f});
                    } else if (wheel) {
                        put(i, row, WHEEL, 1.6f);
                    } else if (!back && !front) {
                        put(i, row, dark, 0.6f);
                    }
                }
            }
        }
        // The plough throws snow up off its blade.
        if (kind == Kind::Plough && rnd() < 0.3f) {
            const float fx = dir > 0 ? x0 + len : x0 - 1;
            sparks.push_back({fx, (float)(y0 + h - 1), 2.0f + rnd() * 3.0f, -3.0f - rnd() * 4.0f, 0, 0.35f, SNOW});
        }
    }

    // Words centred on a road's part of the wall, over a black box: twice
    // the size where there's room, unless scl says otherwise.
    void bandText(const Road &r, const std::string &s, int cr, int cg, int cb, int scl = 0) {
        if (scl <= 0) {
            scl = std::clamp((r.h - 2) / 5, 1, 2);
        }
        const int tw = (int)s.size() * 4 * scl - scl, th = 5 * scl;
        const int x0 = std::max(0, (W - tw) / 2 / scl * scl), y0 = r.top + std::max(0, (r.h - th) / 2);
        for (int y = y0 - 1; y <= y0 + th; ++y) {
            for (int x = x0 - 1; x <= x0 + tw; ++x) {
                if (x >= 0 && y >= 0 && x < W && y < H) {
                    model->setOverlayPixelValue(x, y, 0, 0, 0);
                }
            }
        }
        PanelTransform pt(this);
        for (size_t i = 0; i < s.size(); ++i) {
            if (scl == 1) {
                outputLetter(x0 + (int)i * 4, y0, s[i], cr, cg, cb, 1);
            } else {
                // outputLetter() places a big letter at x * scl: draw it here
                // by hand so it can sit on any row.
                const int lx = x0 + (int)i * 4 * scl, w = arcadeGlyphWidth(s[i], scl);
                for (int py = 0; py < 5 * scl; ++py) {
                    for (int pxx = 0; pxx < w; ++pxx) {
                        if (arcadeGlyphScaled(s[i], pxx, py, scl)) {
                            model->setOverlayPixelValue(lx + pxx, y0 + py, cr, cg, cb);
                        }
                    }
                }
            }
        }
    }

    // Handing the model back, after GAME OVER or the end show.
    int32_t finish() {
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();
        finished = true;
        return EFFECT_AFTER_NEXT_OUTPUT;
    }

    int endShow = 1, goodGame = 2000;   // the End Show and Good Game (metres driven) settings
    ArcadeShowPlayer show;

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 60, H = 25;
    int startLives = 3;
    uint32_t seed = 1;

    Road roads[2];   // the D-pad's (red) and A/B's (blue)
    bool two = false;
    bool over = false, decided = false;
    float overT = 0, clock = 0, engineT = 0, passSfxAt = -1;
    int winner = 0;   // with two: 0 a tie, 1 red, 2 blue
    std::vector<Spark> sparks;

    std::mutex pressLock;
    std::vector<std::string> presses;
    std::atomic<uint32_t> held{0};
    bool finished = false;
};


const std::string &FPPRoadRace::getName() {
    static const std::string name = "Road Race";
    return name;
}

void FPPRoadRace::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    RoadRaceEffect *effect = dynamic_cast<RoadRaceEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new RoadRaceEffect(m, findIntOption("Lives", 3), endShowOption(), goodGameOption(2000));
        m->setRunningEffect(effect, 25);
    }
    // The press that starts the game also picks its car (Start just starts it,
    // waiting for one).
    if (!starting || button.compare(0, 5, "Start") != 0) {
        effect->button(button);
    }
}
