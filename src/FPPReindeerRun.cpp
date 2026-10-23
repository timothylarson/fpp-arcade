#include <fpp-pch.h>

#include "FPPReindeerRun.h"
#include "ArcadeCanvas.h"
#include "ArcadeShow.h"
#include "ReindeerRunShow.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;
using namespace ReindeerRunShow;   // the reindeer sprites, shared with the end show

// An endless runner on a snowy night. The reindeer gallops along on the left
// while the course comes at it from the right: logs, presents, snowmen, ice
// and creeks to jump; low boughs, icicles and swooping owls to duck. It speeds
// up in six steps over the first 80 seconds, and the obstacles get trickier
// with it - pairs that need one long jump, a duck straight into a jump. One
// hit ends the run. The score is the distance (an LED is half a point) plus
// 10 for every candy cane.
//
// The directions run Rudolph and A/B run Dasher. Whoever presses first plays
// on the whole wall; the other control joins at any time, which splits the
// wall into two lanes on the same course at the same speed - Rudolph on top,
// Dasher below - and the longer run wins.
//
// Everything about the course is in LEDs and seconds of game time. On a narrow
// wall (or a lower Speed setting) game time runs slower, so the same jump over
// the same log works anywhere; only the warning you get differs.

namespace {

constexpr float START_SPEED = 18.0f, TOP_SPEED = 34.0f;   // LEDs a second
// Past top speed it still creeps up, this much a second, so a long run can't
// settle into a rhythm and go on for ever.
constexpr float CREEP = 0.15f, FASTEST = 60.0f;
constexpr int BANDS = 6;                                   // steps of speed on the way up
constexpr float LEDS_PER_POINT = 2.0f;
constexpr int CANDY_POINTS = 10;
// A tap's time in the air at the start. Faster, it's quicker - the square
// root of how much faster - or the jumps would carry further and further and
// get easier as the run sped up. Holding the button longer goes higher (and
// so further): see Feel.
constexpr float JUMP_TIME = 0.7f;
constexpr float JOIN_SAFE = 1.5f;   // a late joiner can't be hit for this long

// Held buttons, and the presses since the last frame, as bits.
enum : uint32_t { UP = 1, DOWN = 2, SIDE = 4, ABTN = 8, BBTN = 16 };
const uint32_t CONTROL[2] = {UP | DOWN | SIDE, ABTN | BBTN};   // each player's buttons
const uint32_t JUMP_BIT[2] = {UP, ABTN}, DUCK_BIT[2] = {DOWN, BBTN};

// What the reindeer hits, standing (or in the air) and ducking: the drawn
// sprite less its nose and tail, so a snowman's hat brushing the nose isn't a
// crash. {x, height above the feet}. The legs count as one solid row whatever
// frame they're in, so a hit never depends on the gallop.
struct Cell {
    int x, y;
};
const Cell STAND_MASK[] = {{3, 3}, {5, 3}, {4, 2}, {5, 2}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1},
                           {1, 0}, {2, 0}, {3, 0}, {4, 0}};
const Cell DUCK_MASK[] = {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {1, 0}, {2, 0}, {3, 0}, {4, 0}};
// The feet, for the flat hazards (ice, creeks): x from 1 to 4.
constexpr int FOOT_L = 1, FOOT_R = 4;
// Everything hanging comes down to this height: a ducking reindeer (two rows)
// fits under, a standing one doesn't.
constexpr int DUCK_CLEAR = 2;

// The obstacles that have shapes, top row first, the left (the side facing the
// reindeer) first. Lower-case letters are decoration and can't be hit.
//   e cut end, b bark, s snowy bark, d dark bark - logs
//   p box, y ribbon, q the box on top - presents
//   w snow, h hat, c carrot, k stick arm - snowmen
//   E eyes, B body, w wing - owls (each frame)
const char *const LOG3[] = {"eSB", "eBD"};
const char *const LOG4[] = {"eSBS", "eBDB"};
const char *const PRESENT[] = {".Y.", "PYP", "PYP"};
const char *const PRESENT_TALL[] = {".Y.", "QYQ", "QYQ", "PYP", "PYP"};   // one player only
const char *const SNOW3[] = {".H.", "cW.", "WWW"};
const char *const SNOW4[] = {".H.", "cW.", "WWW", "WWW"};
const char *const SNOW5[] = {".H.", "cW.", "kWk", "WWW", "WWW"};   // one player only
const char *const OWL[2][2] = {{"VEV", ".O."}, {".E.", "VOV"}};

enum class Kind { Log, Present, Snowman, Ice, Creek, Owl, Bough, Icicles, Candy };

}  // namespace

class ReindeerRunEffect : public FPPArcadeGameEffect {
public:
    // showMode and good: the End Show and Good Game settings; speedPct the
    // Speed setting (100 is normal), rampSecs how long it takes to get to top
    // speed.
    ReindeerRunEffect(PixelOverlayModel *m, int showMode, int good, int speedPct, int rampSecs)
        : FPPArcadeGameEffect(m), endShow(showMode), goodGame(good), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(20, W);
        H = std::max(12, H);
        canvas.resize(W, H);
        // A 30-wide half gives about 19 LEDs of warning to a 60-wide wall's 46,
        // so game time runs at 85% there: still quick, but you can react.
        timeScale = std::clamp(speedPct, 25, 300) / 100.0f * std::clamp(0.7f + 0.3f * W / 60.0f, 0.85f, 1.0f);
        ramp = (float)std::clamp(rampSecs, 10, 900);
        runX = std::max(2, W / 7);
        for (int i = 0; i < std::max(8, W * H / 80); ++i) {
            flakes.push_back({rnd() * W, rnd() * H, 2.0f + rnd() * 3.0f});
        }
        for (int i = 0; i < std::max(6, W / 4); ++i) {
            stars.push_back({rnd() * W, rnd() * 0.55f, rnd() * 6.28f});
        }
        reset();
    }

    const std::string &name() const override {
        static std::string NAME = "ReindeerRun";
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
            if (n == "Up" || n == "Up/Left" || n == "Up/Right") bits = UP;
            else if (n == "Down" || n == "Down/Left" || n == "Down/Right") bits = DOWN;
            else if (n == "Left" || n == "Right") bits = SIDE;
            else if (n == "A Button" || n == "Fire" || n == "Fire Button") bits = ABTN;
            else if (n == "B Button") bits = BBTN;
            // Releases are kept even on the pause menu, so a button let go
            // there isn't still held when the game resumes.
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
        if (pressed && bits) {
            presses.fetch_or(bits);
        }
    }

    void restart() override {
        reset();
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
            return finish();   // over: the game ends as it does after GAME OVER
        }
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return FRAME_MS;
        }
        const float dt = (float)(consumeElapsedMs(FRAME_MS, 100.0) / 1000.0);
        clock += dt;
        frameDt = dt;
        const int cheat = takeCheat();
        if (!over) {
            if (cheat == CHEAT_NEXT_LEVEL) {
                // Up a step of speed, as if that much more of the run had gone by.
                runT = std::min(ramp, (band() + 1) * ramp / BANDS);
            } else if (cheat == CHEAT_LAST_LEVEL) {
                runT = std::max(runT, ramp);   // top speed, every kind of obstacle
            }
            stepGame(dt);
        } else {
            overT += dt;
            stepSparks(dt);
            if (overT > 1.0f && overT - dt <= 1.0f) {
                sfxStop("reindeerrun");
                // A good game (or a new best) gets the end show where the
                // GAME OVER box would be, once the crash has had its second.
                if (showDue) {
                    showDue = false;
                    sfx("reindeerrun", "EndShow");
                    show.start(model, std::move(showConfig), std::move(showStory));
                    resetFrameTimer();
                    return FRAME_MS;
                }
                sfx("reindeerrun", "GameOver");
            }
            if (overT > 5.0f) {
                return finish();
            }
        }
        canvas.clear();
        render();
        beginFrame();   // the picture, then the GAME OVER box over it: see beginFrame()
        canvas.writeTo(model);
        if (over && overT > 1.0f) {
            textBox(overLines, 255, 255, 255);
        }
        endFrame();
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 25;

    struct Runner {
        bool joined = false, alive = false;
        float h = 0, vy = 0;          // the feet's height above the snow, and how fast it's rising
        bool air = false, holding = false, ducking = false;
        float buffer = 0;             // a jump pressed just before landing, taken on landing
        float safeT = 0, deadT = 0, legs = 0;
        double fromX = 0;             // where in the world this run began
        int candy = 0;
        int score(double worldX) const { return (int)((worldX - fromX) / LEDS_PER_POINT) + CANDY_POINTS * candy; }
    };
    struct Thing {
        Kind kind;
        double wx;          // the left edge, in the world
        int w, h;           // h: how tall a ground obstacle is
        float alt;          // candy: the height of its bottom row
        int variant;
        const char *const *rows = nullptr;   // the shape, for those that have one
        bool taken[2] = {false, false};      // candy, by each runner
        bool hooted = false;                 // an owl's call when it comes on
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
    };
    struct Flake {
        float x, y, vy;
    };
    struct Star {
        float x, y, phase;   // y as a share of the sky
    };
    // A lane: its first row, the row of snow the reindeer runs on, and the row
    // after its last. Heights count up from the snow: the feet are at 0.
    struct Lane {
        int top, ground, bottom;
        int air() const { return ground - top; }
        int row(int ht) const { return ground - 1 - ht; }
    };
    // How a jump goes: a tap rises to `tap` LEDs and lands jumpTime() later;
    // holding the button lightens gravity on the way up, so a full hold peaks
    // at `high`. Two lanes are half as tall, so their jumps are lower but take
    // the same time - the course plays the same either way.
    struct Feel {
        float g, v0, gHold;
        int maxFeet;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    int pick(int n) { return std::min(n - 1, (int)(rnd() * n)); }

    // ---------------------------------------------------------------- game flow
    void reset() {
        for (auto &r : runners) {
            r = Runner();
        }
        things.clear();
        sparks.clear();
        worldX = 0;
        runT = 0;
        lastBand = 0;
        nextWX = runX + RW + START_SPEED * 3.0;   // the first obstacle about three seconds out
        over = false;
        overT = 0;
        showDue = false;
        showStory.reset();
        goT = -1;
        flashT = 0;
        held.store(0);
        presses.store(0);
        sfxStop("reindeerrun");
    }

    bool anyJoined() const { return runners[0].joined || runners[1].joined; }
    bool split() const { return runners[0].joined && runners[1].joined; }
    // Before anyone has pressed, both reindeer stand in their lanes so each
    // player can see which is theirs.
    bool twoLanes() const { return split() || !anyJoined(); }

    Lane laneFor(int p) const {
        if (!twoLanes()) {
            const int g = H >= 20 ? H - 3 : H - 2;
            return {0, g, H};
        }
        const int L = (H - 1) / 2;
        if (p == 0) {
            return {0, L - 1, L};
        }
        return {L + 1, H - 1, H};
    }

    Feel feel() const {
        const float tap = twoLanes() ? 4.5f : 5.5f, high = twoLanes() ? 7.0f : 9.5f;
        Feel f;
        const float t = jumpTime();
        f.g = 8.0f * tap / (t * t);
        f.v0 = std::sqrt(2.0f * f.g * tap);
        f.gHold = f.v0 * f.v0 / (2.0f * high);
        f.maxFeet = laneFor(0).air() - RH;
        return f;
    }

    float speed() const {
        return std::min(FASTEST, START_SPEED + (TOP_SPEED - START_SPEED) * std::min(1.0f, runT / ramp) +
                                     CREEP * std::max(0.0f, runT - ramp));
    }
    float jumpTime() const { return JUMP_TIME * std::sqrt(START_SPEED / speed()); }

    // How long a jump keeps the feet at least ht up, a tap or held right
    // through, in the current lanes at the current speed: what the patterns
    // are spaced by.
    float airAbove(float ht, bool hold) const {
        const Feel f = feel();
        float h = 0, vy = f.v0, t = 0, above = 0;
        const float dt = 0.002f;
        while (h >= 0 && t < 3) {
            vy -= (hold && vy > 0 ? f.gHold : f.g) * dt;
            h += vy * dt;
            t += dt;
            above += h >= ht ? dt : 0;
        }
        return above;
    }
    int band() const { return std::min(BANDS, (int)(runT / (ramp / BANDS))); }

    // A press of a control nobody has yet: the first starts the run on the
    // whole wall, the second splits it.
    void join(int p) {
        Runner &r = runners[p];
        const bool first = !anyJoined();
        r = Runner();
        r.joined = r.alive = true;
        r.fromX = worldX;
        r.safeT = first ? 0.0f : JOIN_SAFE;
        sfx("reindeerrun", "Start");
        if (first) {
            goT = 0;
            return;
        }
        // Into two lanes: the tall one-player obstacles come down to size,
        // candy comes down into reach, and the other reindeer keeps its jump.
        for (auto &t : things) {
            if (t.rows == SNOW5) {
                t.rows = SNOW4;
                t.h = 4;
            } else if (t.rows == PRESENT_TALL) {
                t.rows = PRESENT;
                t.h = 3;
            }
            if (t.kind == Kind::Candy) {
                t.alt = std::min(t.alt, (float)(laneFor(0).air() - 3));
            }
        }
        Runner &o = runners[1 - p];
        o.h = std::min(o.h, (float)feel().maxFeet);
    }

    void stepGame(float realDt) {
        const float dt = realDt * timeScale;
        const uint32_t h = held.load();
        const uint32_t pr = presses.exchange(0);
        for (int p = 0; p < 2; ++p) {
            if ((pr & CONTROL[p]) && !runners[p].joined && !over) {
                // The press that joins only joins: it shouldn't also jump.
                join(p);
            } else if (runners[p].alive && (pr & JUMP_BIT[p])) {
                pressJump(runners[p]);
            }
        }
        stepSparks(realDt);
        if (!anyJoined()) {
            return;   // both stand waiting
        }
        if (goT >= 0) {
            goT += realDt;
        }
        flashT = std::max(0.0f, flashT - realDt);
        runT += dt;
        if (band() > lastBand) {
            lastBand = band();
            flashT = 0.8f;
            sfx("reindeerrun", "Faster");
        }

        // In steps of at most half an LED, so nothing is jumped through.
        const float v = speed();
        const int n = std::max(1, (int)std::ceil(v * dt / 0.5f));
        const float sub = dt / n;
        const Feel f = feel();
        for (int k = 0; k < n && !over; ++k) {
            worldX += v * sub;
            for (int p = 0; p < 2 && !over; ++p) {
                Runner &r = runners[p];
                if (!r.alive) {
                    continue;
                }
                const bool duckHeld = (h & DUCK_BIT[p]) || (pr & DUCK_BIT[p]);
                move(r, f, sub, (h & JUMP_BIT[p]) != 0, duckHeld);
                r.legs += v * sub / 2.5f;
                if (r.safeT <= 0) {
                    collide(p);
                }
                collect(p);
            }
        }
        for (auto &r : runners) {
            r.safeT = std::max(0.0f, r.safeT - realDt);
            r.deadT += realDt;
        }

        // Owls call as they come on.
        for (auto &t : things) {
            if (t.kind == Kind::Owl && !t.hooted && t.wx - worldX < W) {
                t.hooted = true;
                sfx("reindeerrun", "Owl", 0.6f);
            }
        }
        while (nextWX < worldX + W + 8) {
            spawn();
        }
        things.erase(std::remove_if(things.begin(), things.end(),
                                    [&](const Thing &t) { return t.wx + t.w < worldX - 4; }),
                     things.end());
    }

    void pressJump(Runner &r) {
        if (r.air) {
            r.buffer = 0.15f;   // early: it jumps as it lands
            return;
        }
        jump(r);
    }
    void jump(Runner &r) {
        r.air = true;
        r.holding = true;
        r.ducking = false;
        r.vy = feel().v0;
        r.buffer = 0;
        sfx("reindeerrun", "Jump", 0.8f);
    }

    void move(Runner &r, const Feel &f, float dt, bool jumpHeld, bool duckHeld) {
        r.buffer -= dt;
        if (r.air) {
            if (!jumpHeld) {
                r.holding = false;
            }
            float g = (r.holding && r.vy > 0) ? f.gHold : f.g;
            // Down in the air drops it fast, to get down in time to duck.
            if (duckHeld && !jumpHeld) {
                g = f.g * 3.0f;
                r.holding = false;
            }
            r.vy -= g * dt;
            r.h += r.vy * dt;
            if (r.h > f.maxFeet) {
                r.h = (float)f.maxFeet;
                r.vy = std::min(r.vy, 0.0f);
            }
            if (r.h <= 0) {
                r.h = 0;
                r.vy = 0;
                r.air = false;
                sfx("reindeerrun", "Land", 0.35f);
                if (r.buffer > 0) {
                    jump(r);
                    return;
                }
            }
        }
        const bool was = r.ducking;
        r.ducking = !r.air && duckHeld;
        if (r.ducking && !was) {
            sfx("reindeerrun", "Duck", 0.5f);
        }
    }

    // ---------------------------------------------------------------- the course
    int feetRow(const Runner &r) const { return (int)std::lround(r.h); }
    int screenX(const Thing &t) const { return (int)std::floor(t.wx - worldX); }

    // An owl swoops down from high up as it comes on, and is down at duck
    // height by the time it's 55% of the way across.
    float owlAlt(const Thing &t, int air) const {
        const float top = (float)(DUCK_CLEAR + std::min(6, air - 6));
        const float f = std::clamp((screenX(t) - W * 0.55f) / (W * 0.4f), 0.0f, 1.0f);
        return DUCK_CLEAR + (top - DUCK_CLEAR) * f * f * (3 - 2 * f);
    }

    // Whether a thing is solid at (dx from its left edge, height ht) in a lane
    // with `air` rows above the snow. The same test draws it, so what you see
    // is what you hit.
    bool solidAt(const Thing &t, int dx, int ht, int air) const {
        if (dx < 0 || dx >= t.w) {
            return false;
        }
        switch (t.kind) {
        case Kind::Owl: {
            const int a = (int)std::lround(owlAlt(t, air));
            return ht >= a && ht <= a + 1;   // the whole 3x2, flapping or not
        }
        case Kind::Bough:
            // The low skirt of a big fir: a curtain of branches from the top,
            // their tips drooping to duck height in a ragged edge.
            return ht < air && (ht > DUCK_CLEAR || (ht == DUCK_CLEAR && dx % 3 != 2));
        case Kind::Icicles:
            // A ledge of ice from the top, with icicles hanging off it.
            return ht < air && (ht > DUCK_CLEAR || (ht == DUCK_CLEAR && dx % 2 == 0));
        case Kind::Log:
        case Kind::Present:
        case Kind::Snowman: {
            if (ht < 0 || ht >= t.h) {
                return false;
            }
            const char ch = t.rows[t.h - 1 - ht][dx];
            return ch != '.' && !std::islower((unsigned char)ch);
        }
        default:
            return false;   // ice and creeks trip the feet instead; candy is collected
        }
    }

    void collide(int p) {
        Runner &r = runners[p];
        const int air = laneFor(p).air(), fh = feetRow(r);
        for (const auto &t : things) {
            const int sx = screenX(t);
            if (sx > runX + DW || sx + t.w < runX) {
                continue;
            }
            if (t.kind == Kind::Candy) {
                continue;
            }
            if (t.kind == Kind::Ice || t.kind == Kind::Creek) {
                // Feet down on it, past its first and last LED.
                if (fh == 0 && runX + FOOT_R >= sx + 1 && runX + FOOT_L <= sx + t.w - 2) {
                    crash(p, t.kind);
                    return;
                }
                continue;
            }
            const Cell *mask = r.ducking ? DUCK_MASK : STAND_MASK;
            const int cells = r.ducking ? (int)(sizeof(DUCK_MASK) / sizeof(Cell)) : (int)(sizeof(STAND_MASK) / sizeof(Cell));
            for (int i = 0; i < cells; ++i) {
                if (solidAt(t, runX + mask[i].x - sx, fh + mask[i].y, air)) {
                    crash(p, t.kind);
                    return;
                }
            }
        }
    }

    // Candy: any lit LED of the reindeer touching any of the cane's 3x3.
    void collect(int p) {
        Runner &r = runners[p];
        const int fh = feetRow(r);
        const char *const *rows = poseRows(r);
        const int rows_n = r.ducking ? DH : RH;
        for (auto &t : things) {
            if (t.kind != Kind::Candy || t.taken[p]) {
                continue;
            }
            const int sx = screenX(t), a = (int)std::lround(t.alt);
            if (sx > runX + DW || sx + 3 < runX) {
                continue;
            }
            bool touch = false;
            for (int rr = 0; rr < rows_n && !touch; ++rr) {
                const int ht = fh + rows_n - 1 - rr;
                for (int i = 0; rows[rr][i] && !touch; ++i) {
                    const int x = runX + i;
                    touch = rows[rr][i] != '.' && x >= sx && x < sx + 3 && ht >= a && ht < a + 3;
                }
            }
            if (touch) {
                t.taken[p] = true;
                ++r.candy;
                sfx("reindeerrun", "Candy", 0.5f);
                const Lane L = laneFor(p);
                burst(sx + 1.0f, (float)L.row(a + 1), {1.0f, 0.1f, 0.1f}, 5);
                burst(sx + 1.0f, (float)L.row(a + 1), {1, 1, 1}, 5);
            }
        }
    }

    void crash(int p, Kind what) {
        Runner &r = runners[p];
        r.alive = false;
        r.deadT = 0;
        lastOut = p;
        const Lane L = laneFor(p);
        burst(runX + 4.0f, (float)L.row(feetRow(r) + 1), {1, 1, 1}, 12);
        burst(runX + 4.0f, (float)L.row(feetRow(r) + 2), {1.0f, 0.85f, 0.2f}, 6);
        sfx("reindeerrun", what == Kind::Creek ? "Splash" : "Crash");
        if (runners[0].alive || runners[1].alive) {
            return;
        }
        // Everyone who joined is out.
        const double wx = worldX;
        const int s0 = runners[0].joined ? runners[0].score(wx) : 0, s1 = runners[1].joined ? runners[1].score(wx) : 0;
        const int top = std::max(s0, s1);
        int best = 0;
        const bool newBest = arcadeRecordBest(model->getName(), "Reindeer Run", top, &best);
        showDue = arcadeWantShow(endShow, top >= goodGame, newBest);
        if (split()) {
            // TOP WINS, BOTTOM WINS or TIE GAME, and both scores top first -
            // or just the winner's where both won't fit.
            std::string line = std::to_string(s0) + ":" + std::to_string(s1);
            if ((int)line.size() * 4 - 1 > W) {
                line = std::to_string(top);
            }
            overLines = {s0 == s1 ? "TIE" : (s0 > s1 ? "TOP" : "BOTTOM"), s0 == s1 ? "GAME" : "WINS", line};
            if (showDue) {
                showConfig = ReindeerRunShow::config2(s0, s1, newBest, best, W);
                showStory = std::make_unique<ReindeerRunShow::Story>(s1 > s0, true);
            }
        } else {
            const int who = runners[0].joined ? 0 : 1;
            overLines = {"GAME", "OVER", std::to_string(top)};
            if (showDue) {
                showConfig = ReindeerRunShow::config(top, runners[who].candy, who == 1, newBest, best, W);
                showStory = std::make_unique<ReindeerRunShow::Story>(who == 1, false);
            }
        }
        over = true;
        overT = 0;
    }

    // ---------------------------------------------------------------- spawning
    Thing &add(Kind kind, double wx, int w, int h, const char *const *rows = nullptr) {
        Thing t;
        t.kind = kind;
        t.wx = wx;
        t.w = w;
        t.h = h;
        t.alt = 0;
        t.variant = pick(4);
        t.rows = rows;
        things.push_back(t);
        return things.back();
    }
    void addCandy(double wx, float alt) { add(Kind::Candy, wx, 3, 3).alt = alt; }

    // Something to jump that's no taller than two-ish: the staples. Returns its width.
    int addLow(double x, bool arcCandy) {
        int w = 3, h = 2;
        switch (pick(4)) {
        case 0: add(Kind::Log, x, 3, 2, LOG3); break;
        case 1: w = 4; add(Kind::Log, x, 4, 2, LOG4); break;
        case 2: h = 3; add(Kind::Present, x, 3, 3, PRESENT); break;
        default:
            w = 3 + pick(2);
            h = 1;
            add(Kind::Ice, x, w, 1);
            break;
        }
        if (arcCandy) {
            addCandy(x + w / 2.0 - 1, (float)(h + 2));   // over it: any jump that clears it gets it
        }
        return w;
    }

    // One pattern of obstacles at nextWX, then the gap to the next. The
    // spacing inside a pattern comes from the jump itself, so every one can
    // be got through by someone quick enough.
    void spawn() {
        const float v = speed();
        const int b = band();
        const bool one = !twoLanes();
        const double x = nextWX;
        double end = x;
        const float tap2a = 0.08f;   // a tap is two LEDs up about 0.08 s after it leaves the ground
        enum { LOW, TALL, OWL_, HANG, CREEK, PAIR, DUCK_JUMP, JUMP_DUCK, DOUBLE };
        // How likely each is in each band (0 to 6).
        static const float WEIGHTS[7][9] = {
            {10, 0, 0, 0, 0, 0, 0, 0, 0},  {5, 2, 2, 1.5f, 0, 0, 0, 0, 0},  {3, 2, 2, 2, 1.5f, 1.5f, 0, 0, 0},
            {2, 1.5f, 1.5f, 1.5f, 1, 1, 1.2f, 1.2f, 1},  {1.5f, 1.5f, 1.5f, 1.5f, 1, 1.2f, 1.5f, 1.5f, 1.2f},
            {1.2f, 1.5f, 1.5f, 1.2f, 1.2f, 1.2f, 1.8f, 1.8f, 1.5f}, {1, 1.5f, 1.5f, 1.2f, 1.2f, 1.5f, 2, 2, 1.8f},
        };
        const float *wt = WEIGHTS[std::min(6, b)];
        float total = 0;
        for (int i = 0; i < 9; ++i) total += wt[i];
        float r = rnd() * total;
        int kind = 0;
        while (kind < 8 && r >= wt[kind]) {
            r -= wt[kind++];
        }
        switch (kind) {
        case LOW:
            end = x + addLow(x, rnd() < 0.3f);
            break;
        case TALL: {
            // Snowmen; from band 2 a taller one, which wants a held jump at
            // these speeds - and on the whole wall a tall stack of presents
            // or a big snowman.
            const int big = b >= 2 ? pick(one ? 3 : 2) : 0;
            const char *const *rows = big == 0 ? SNOW3 : (big == 1 ? SNOW4 : (rnd() < 0.5f ? SNOW5 : PRESENT_TALL));
            const int hgt = big == 0 ? 3 : (big == 1 ? 4 : 5);
            add(rows == PRESENT_TALL ? Kind::Present : Kind::Snowman, x, 3, hgt, rows);
            end = x + 3;
            break;
        }
        case OWL_:
            add(Kind::Owl, x, 3, 2);
            end = x + 3;
            break;
        case HANG: {
            const int w = 5 + pick(3);
            add(rnd() < 0.5f ? Kind::Bough : Kind::Icicles, x, w, 0);
            end = x + w;
            break;
        }
        case CREEK: {
            const int w = 5 + std::min(4, b) + pick(2);
            add(Kind::Creek, x, w, 0);
            if (rnd() < 0.5f) {
                addCandy(x + w / 2.0 - 1, 4.0f);
            }
            end = x + w;
            break;
        }
        case PAIR: {
            // Two logs close enough that one held jump clears both, and too
            // close for two taps.
            // The legs (4 wide) over both logs with a fifth of a second to
            // spare, at most. Two lanes jump lower, so on the whole wall
            // allow for a second player splitting it before they get here.
            const float held = twoLanes() ? airAbove(2, true) : 0.88f * airAbove(2, true);
            const float clear = v * (held - 0.2f) - 4 - 6;
            const int gap = (int)std::clamp(clear - rnd() * 2.0f, 2.0f, 5.0f);
            add(Kind::Log, x, 3, 2, LOG3);
            add(Kind::Log, x + 3 + gap, 3, 2, LOG3);
            addCandy(x + 3 + gap / 2.0 - 1, 6.0f);
            end = x + 6 + gap;
            break;
        }
        case DUCK_JUMP: {
            // Under a bough or an owl, then straight up over something low.
            const int w = 5 + pick(2);
            if (rnd() < 0.5f) {
                add(Kind::Bough, x, w, 0);
            } else {
                add(Kind::Icicles, x, w, 0);
            }
            const double s = 4 + v * (tap2a + 0.38f + 0.1f * rnd());
            end = x + w + s + addLow(x + w + s, false);
            break;
        }
        case JUMP_DUCK: {
            // Over something low, then down under an owl or a bough: a tap
            // lands in time (or Down drops a held jump).
            const int w = addLow(x, false);
            const double s = 6 + v * (jumpTime() + 0.08f + 0.1f * rnd());
            if (rnd() < 0.5f) {
                add(Kind::Owl, x + w + s, 3, 2);
                end = x + w + s + 3;
            } else {
                const int hw = 5 + pick(2);
                add(Kind::Bough, x + w + s, hw, 0);
                end = x + w + s + hw;
            }
            break;
        }
        case DOUBLE: {
            // Two to jump, one tap each.
            const int w = addLow(x, false);
            const double s = 4 + v * (jumpTime() + 0.3f + 0.15f * rnd());
            end = x + w + s + addLow(x + w + s, rnd() < 0.4f);
            break;
        }
        }
        // Then a breather, shorter as the run goes on (and a little shorter
        // still once it's been at top speed a while).
        const float d = std::min(1.0f, runT / ramp), d2 = std::clamp((runT - ramp) / 60.0f, 0.0f, 1.0f);
        const float gapT = (1.7f - 0.6f * d - 0.15f * d2) + rnd() * (1.2f - 0.6f * d);
        const double gap = v * gapT;
        // Candy in the gap now and then: a row to run through, or one up high
        // for a held jump.
        if (gap > v * 1.4f && rnd() < 0.3f) {
            if (rnd() < 0.6f) {
                for (int i = 0; i < 2; ++i) {
                    addCandy(end + gap / 2 + i * 5 - 3, (float)DUCK_CLEAR);
                }
            } else {
                addCandy(end + gap / 2 - 1, one ? 10.0f : 8.0f);
            }
        }
        nextWX = end + gap;
    }

    // ---------------------------------------------------------------- effects
    void burst(float x, float y, const RGB &col, int n) {
        for (int k = 0; k < n; ++k) {
            const float a = rnd() * 6.2831853f, sp = 3.0f + rnd() * 7.0f;
            sparks.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp - 2.0f, 0.0f, 0.35f + rnd() * 0.4f, col});
        }
    }
    void stepSparks(float dt) {
        for (auto &s : sparks) {
            s.age += dt;
            s.vy += 12.0f * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark &s) { return s.age >= s.life; }),
                     sparks.end());
    }

    // ---------------------------------------------------------------- drawing
    const char *const *poseRows(const Runner &r) const {
        if (r.ducking) {
            return DUCK[(int)r.legs % 2];
        }
        if (r.air) {
            return JUMP;
        }
        if (!anyJoined()) {
            return RUN[0];   // standing, waiting
        }
        return RUN[RUN_CYCLE[(int)r.legs % 4]];
    }

    static unsigned hash(int a, int b = 0) {
        unsigned h = (unsigned)a * 2654435761u ^ (unsigned)(b + 0x9e37) * 40503u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        return h;
    }
    // Smooth noise along x, 0..1, for the hills.
    static float noise(float x) {
        const int i = (int)std::floor(x);
        const float f = x - i, s = f * f * (3 - 2 * f);
        const float a = (hash(i, 7) & 1023) / 1023.0f, b = (hash(i + 1, 7) & 1023) / 1023.0f;
        return a + (b - a) * s;
    }

    // 3x5 text straight into the canvas, so it can sit behind the play.
    void text(const std::string &s, int x, int y, const RGB &col, float k) {
        for (size_t i = 0; i < s.size(); ++i) {
            for (int py = 0; py < 5; ++py) {
                for (int px = 0; px < 3; ++px) {
                    if (arcadeGlyph(s[i], px, py)) {
                        canvas.set(x + (int)i * 4 + px, y + py, col, k);
                    }
                }
            }
        }
    }
    static int textW(const std::string &s) { return (int)s.size() * 4 - 1; }

    // The night behind a lane. On the wall a coloured fill just reads as a
    // glow, so the sky is black: a few dim stars, a small dim moon (on the
    // whole wall), the far hills only as a dotted ridge and the fir trees as
    // dotted outlines going by slower than the snow - all sparse, so the
    // reindeer, the obstacles and the candy are what you see.
    void drawBackground(const Lane &L, float k, bool moon) {
        const int air = L.air();
        for (const auto &s : stars) {
            const int x = (int)std::fmod(s.x - worldX * 0.02 + W * 100.0, (double)W);
            canvas.add(x, L.top + (int)(s.y * air), {0.85f, 0.9f, 1.0f},
                       (0.06f + 0.04f * std::sin(clock * 1.7f + s.phase)) * k);
        }
        if (moon) {
            const float mx = W * 0.4f, my = L.top + 4.5f, mr = 1.8f;   // clear of the score
            for (int y = (int)(my - 2); y <= (int)(my + 2); ++y) {
                for (int x = (int)(mx - 2); x <= (int)(mx + 2); ++x) {
                    const float in = ArcadeCanvas::clamp01(mr - std::hypot(x + 0.5f - mx, y + 0.5f - my) + 0.5f);
                    canvas.add(x, y, {1.0f, 0.93f, 0.7f}, 0.1f * in * k);
                }
            }
        }
        // Far hills, a fifth of the lane's height or so, at an eighth of the pace.
        const double hillX = worldX * 0.12;
        const int hillBase = (int)std::floor(hillX);
        for (int x = 0; x < W; ++x) {
            if ((x + hillBase) % 2) {
                continue;
            }
            const int hh = (int)(air * (0.12f + 0.16f * noise((x + hillBase) / 13.0f)));
            canvas.add(x, L.row(hh), {0.45f, 0.55f, 0.9f}, 0.05f * k);
        }
        // Fir trees, at two fifths of the pace: the two sloping sides of a
        // triangle, every other row, in dark green.
        const double treeX = worldX * 0.4;
        const int slot0 = (int)std::floor((treeX - 6) / 8.0), slot1 = (int)std::floor((treeX + W + 6) / 8.0);
        for (int sl = slot0; sl <= slot1; ++sl) {
            const unsigned hs = hash(sl, 3);
            if (hs % 3 == 0) {
                continue;
            }
            const int cx = (int)std::floor(sl * 8 + (int)((hs >> 4) % 5) - treeX);   // signed: sl is negative at the start
            const int th = 3 + (int)((hs >> 9) % (unsigned)std::max(1, (int)(air * 0.35f)));
            for (int i = 0; i < th; i += 2) {
                const int half = (th - i) / 2;
                canvas.add(cx - half, L.row(i), {0.1f, 0.6f, 0.2f}, 0.07f * k);
                canvas.add(cx + half, L.row(i), {0.1f, 0.6f, 0.2f}, 0.07f * k);
            }
            canvas.add(cx, L.row(th), {0.8f, 0.85f, 1.0f}, 0.06f * k);   // snow on the tip
        }
        // The snow the reindeer runs on: a dim line along the top with glints
        // going by at the reindeer's pace, and below it all but black. The
        // creeks cut through it.
        const int base = (int)std::floor(worldX);
        for (int x = 0; x < W; ++x) {
            const Thing *creek = nullptr;
            for (const auto &t : things) {
                if (t.kind == Kind::Creek && x >= screenX(t) && x < screenX(t) + t.w) {
                    creek = &t;
                }
            }
            for (int y = L.ground; y < L.bottom; ++y) {
                if (creek) {
                    // Water where the snow should be, its banks bright white
                    // and glints sliding along it, so the gap can't be missed.
                    const int cx = x - screenX(*creek);
                    if (cx == 0 || cx == creek->w - 1) {
                        canvas.set(x, y, {0.85f, 0.9f, 1.0f}, (y == L.ground ? 0.6f : 0.25f) * k);
                        continue;
                    }
                    const bool glint = y == L.ground && (int)(clock * 6 + x * 2) % 7 == 0;
                    canvas.set(x, y, glint ? RGB{0.6f, 0.85f, 1.0f} : RGB{0.05f, 0.25f, 0.9f},
                               (glint ? 0.6f : (y == L.ground ? 0.3f : 0.12f)) * k);
                    continue;
                }
                const unsigned hs = hash(x + base, y - L.ground);
                const float glint = (hs >> 24) % 11 == 0 ? 0.2f : 0.0f;
                canvas.add(x, y, {0.75f, 0.82f, 1.0f}, ((y == L.ground ? 0.14f : 0.012f) + glint) * k);
            }
        }
    }

    // A thing's colour at a shape letter.
    RGB shapeColour(const Thing &t, char ch) const {
        static const RGB BOXES[4] = {{1.0f, 0.05f, 0.05f}, {0.1f, 0.8f, 0.15f}, {0.2f, 0.35f, 1.0f}, {0.8f, 0.2f, 1.0f}};
        switch (ch) {
        case 'e': return {0.85f, 0.6f, 0.3f};
        case 'S': return {0.65f, 0.6f, 0.6f};
        case 'B': return {0.45f, 0.2f, 0.05f};
        case 'D': return {0.32f, 0.14f, 0.03f};
        case 'P': return BOXES[t.variant];
        case 'Q': return BOXES[(t.variant + 1) % 4];
        case 'Y': return {1.0f, 0.8f, 0.1f};
        case 'W': return {0.8f, 0.85f, 1.0f};
        case 'H': return {0.9f, 0.05f, 0.05f};
        case 'c': return {1.0f, 0.45f, 0.0f};
        case 'k': return {0.4f, 0.2f, 0.05f};
        default: return {1, 1, 1};
        }
    }

    void drawThing(const Thing &t, const Lane &L, int p, float k) {
        const int sx = screenX(t), air = L.air();
        if (sx > W || sx + t.w < 0) {
            return;
        }
        switch (t.kind) {
        case Kind::Candy:
            if (!t.taken[p]) {
                drawCandy(canvas, sx, L.row((int)std::lround(t.alt) + 2), k);
            }
            return;
        case Kind::Creek:
            return;   // drawn with the snow
        case Kind::Ice:
            // A slick of pale blue on the snow, with a glint sliding across it.
            for (int dx = 0; dx < t.w; ++dx) {
                const bool glint = (int)(clock * 8 + t.wx) % (t.w + 4) == dx;
                canvas.set(sx + dx, L.row(0), glint ? RGB{1, 1, 1} : RGB{0.35f, 0.75f, 1.0f}, (glint ? 0.9f : 0.7f) * k);
                canvas.set(sx + dx, L.ground, {0.5f, 0.85f, 1.0f}, 0.45f * k);
            }
            return;
        case Kind::Owl: {
            const int a = (int)std::lround(owlAlt(t, air));
            const int frame = (int)(clock * 6) % 2;
            for (int r = 0; r < 2; ++r) {
                for (int dx = 0; dx < 3; ++dx) {
                    const char ch = OWL[frame][r][dx];
                    if (ch == '.') continue;
                    const RGB col = ch == 'E' ? RGB{1.0f, 0.85f, 0.1f} : (ch == 'O' ? RGB{0.6f, 0.45f, 0.3f} : RGB{0.85f, 0.75f, 0.6f});
                    canvas.set(sx + dx, L.row(a + 1 - r), col, k);
                }
            }
            return;
        }
        case Kind::Bough:
        case Kind::Icicles:
            // Bright where it matters, at the bottom, fading up to the top.
            for (int ht = DUCK_CLEAR; ht < air; ++ht) {
                const float f = ht <= DUCK_CLEAR + 1 ? 1.0f : std::max(0.1f, 0.6f - 0.12f * (ht - DUCK_CLEAR - 2));
                for (int dx = 0; dx < t.w; ++dx) {
                    if (!solidAt(t, dx, ht, air)) continue;
                    RGB col;
                    if (t.kind == Kind::Bough) {
                        // Snow lying along the branches, row by row.
                        const bool snow = (ht - DUCK_CLEAR) % 3 == 1 && (dx + ht) % 2 == 0;
                        col = snow ? RGB{0.8f, 0.85f, 0.9f} : RGB{0.05f, 0.6f, 0.15f};
                    } else {
                        col = ht == DUCK_CLEAR ? RGB{0.85f, 0.95f, 1.0f} : RGB{0.3f, 0.65f, 1.0f};
                    }
                    canvas.set(sx + dx, L.row(ht), col, f * k);
                }
            }
            return;
        default:
            for (int r = 0; r < t.h; ++r) {
                for (int dx = 0; dx < t.w; ++dx) {
                    const char ch = t.rows[r][dx];
                    if (ch != '.') {
                        canvas.set(sx + dx, L.row(t.h - 1 - r), shapeColour(t, ch), k);
                    }
                }
            }
            return;
        }
    }

    void drawRunner(int p, const Lane &L, float k) {
        const Runner &r = runners[p];
        const Look &look = p == 0 ? RUDOLPH : DASHER;
        // A late joiner blinks while it's safe; a crashed one blinks out (and,
        // at GAME OVER, only the last one out blinks, where it went down).
        bool show = anyJoined() ? r.joined : true;
        if (r.joined && !r.alive) {
            show = over ? (p == lastOut && std::fmod(overT, 0.3f) < 0.15f) : (r.deadT < 0.9f && std::fmod(r.deadT, 0.3f) < 0.15f);
        } else if (r.safeT > 0) {
            show = std::fmod(r.safeT, 0.24f) < 0.14f;
        }
        if (!show) {
            return;
        }
        const int n = r.ducking ? DH : RH;
        drawSprite(canvas, poseRows(r), n, runX, L.row(feetRow(r) + n - 1), look, k);
    }

    void render() {
        const bool lanes2 = twoLanes();
        for (int p = 0; p < 2; ++p) {
            if (!lanes2 && !runners[p].joined) {
                continue;
            }
            const Runner &r = runners[p];
            const Lane L = laneFor(p);
            // A lane whose reindeer is out (while the other runs on) dims.
            const float k = (split() && !r.alive && !over && r.deadT > 0.9f) ? 0.4f : 1.0f;
            drawBackground(L, k, !lanes2);
            // The score, dim, top right and behind everything; brighter for a
            // moment at each step of speed.
            if (r.joined) {
                const std::string s = std::to_string(r.score(worldX));
                const RGB col = lanes2 ? (p == 0 ? RUDOLPH_TEXT : DASHER_TEXT) : RGB{1.0f, 0.85f, 0.45f};
                const float bright = (r.alive ? 0.3f : 0.6f) + (flashT > 0 && std::fmod(flashT, 0.2f) < 0.1f ? 0.4f : 0.0f);
                text(s, W - textW(s) - 1, L.top + 1, col, bright);
            }
            for (const auto &t : things) {
                drawThing(t, L, p, k);
            }
            drawRunner(p, L, k);
            if (goT >= 0 && goT < 1.0f && r.joined && !split()) {
                text("GO!", (W - textW("GO!")) / 2, L.top + L.air() / 3, {1.0f, 0.85f, 0.2f}, 1.0f - goT * 0.6f);
            }
        }
        // Falling snow, drifting back past the reindeer, sparse and dim enough
        // not to be mistaken for anything.
        const float dt = frameDt;
        const float drift = anyJoined() && !over ? speed() * timeScale * 0.45f : 1.0f;
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x -= drift * dt;
            if (f.y >= H || f.x < 0) {
                f.y = f.y >= H ? 0 : f.y;
                f.x = f.x < 0 ? W - 0.01f : rnd() * W;
            }
            canvas.add((int)f.x, (int)f.y, {1, 1, 1}, 0.1f);
        }
        for (const auto &s : sparks) {
            canvas.add((int)std::lround(s.x), (int)std::lround(s.y), s.col, 1.0f - s.age / s.life);
        }
    }

    // Handing the model back, after GAME OVER or the end show.
    int32_t finish() {
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();
        finished = true;
        return EFFECT_AFTER_NEXT_OUTPUT;
    }

    int endShow = 1, goodGame = 600;   // the End Show and Good Game (score) settings
    ArcadeShowPlayer show;
    ShowConfig showConfig;
    std::unique_ptr<ShowStory> showStory;
    bool showDue = false;   // the run earned the show; it starts after the crash

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 60, H = 25, runX = 8;
    float timeScale = 1.0f, ramp = 80.0f;

    Runner runners[2];   // 0 Rudolph (the directions), 1 Dasher (A and B)
    std::vector<Thing> things;
    std::vector<Spark> sparks;
    std::vector<Flake> flakes;
    std::vector<Star> stars;
    double worldX = 0, nextWX = 0;
    float runT = 0, clock = 0, goT = -1, flashT = 0, frameDt = 0.025f;
    int lastBand = 0, lastOut = 0;

    bool over = false;
    float overT = 0;
    std::vector<std::string> overLines;

    std::atomic<uint32_t> held{0}, presses{0};
    bool finished = false;
};


const std::string &FPPReindeerRun::getName() {
    static const std::string name = "Reindeer Run";
    return name;
}

void FPPReindeerRun::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    ReindeerRunEffect *effect = dynamic_cast<ReindeerRunEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new ReindeerRunEffect(m, endShowOption(), goodGameOption(600), findIntOption("Speed", 100),
                                       findIntOption("Speed Up", 80));
        m->setRunningEffect(effect, 25);
    }
    // Start only brings the game up (both reindeer waiting); any other first
    // press also starts that player's run.
    if (!starting || button.compare(0, 5, "Start") != 0) {
        effect->button(button);
    }
}
