#pragma once
// Barrel Climb's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the
// score and the level reached, then the rescue - the climber comes up the
// last ladder, hops a barrel and walks over to the friend; the rivets pop,
// the girders below fall away, the ape tumbles headfirst to the floor and
// lies there dazed while a heart appears between the two of them and snow
// starts to fall - the credits over that scene, and GAME OVER.
//
// It is drawn the way the game draws: girders five rows apart with a step in
// the middle, two-column ladders, people three LEDs tall, a 3x3 ape.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace BarrelClimbShow {

using RGB = ArcadeCanvas::RGB;

// The game's colours (FPPBarrelClimb.cpp).
const RGB GIRDER = {1.0f, 0.15f, 0.35f};
const RGB LADDER = {0.1f, 0.8f, 1.0f};
const RGB APE = {0.6f, 0.3f, 0.08f};
const RGB FACE = {1.0f, 0.7f, 0.45f};
const RGB HEAD = {1.0f, 0.72f, 0.5f};
const RGB SHIRT = {1.0f, 0.08f, 0.08f};
const RGB OVERALLS = {0.2f, 0.3f, 1.0f};
const RGB DRESS = {1.0f, 0.3f, 0.75f};
const RGB BARREL = {0.95f, 0.5f, 0.1f};
const RGB HEART = {1.0f, 0.1f, 0.3f};

class Story : public ShowStory {
public:
    void begin(ArcadeShow &s) override {
        W = s.W;
        H = s.H;
        theme = s.theme();
        base.clear();
        for (int y = H - 1; y >= 4; y -= 5) {
            base.push_back(y);
        }
        top = (int)base.size() - 1;
        popAt.assign(base.size(), -1.0);
        ladders.clear();
        barrels.clear();
        flakes.clear();
        if (top < 1) {
            return;   // too short for a level; draw() just shows fireworks
        }

        // The ape on the left of the top girder, the friend just past its
        // step (so the heart between them sits on level ground), and the
        // climber's last ladder near the right-hand end.
        apeX = 1;
        friendX = W / 2 + std::max(0, (W - 40) / 8);
        meetX = friendX + 8;
        ladderX = std::max(meetX, W - 6);
        cut = std::max(5, std::min(friendX - 3, W / 5));

        // Ladders between the lower girders, placed the way the game does:
        // apart from each other and from the ones below, so the scene looks
        // like a level. The last one is the climber's.
        const int perLevel = W >= 40 ? 2 : 1;
        std::vector<int> below;
        for (int k = 0; k < top; ++k) {
            std::vector<int> here;
            if (k == top - 1) {
                here.push_back(ladderX);
            } else {
                const int lo = std::max(spanLo(k), spanLo(k + 1)) + 2, hi = std::min(spanHi(k), spanHi(k + 1)) - 3;
                for (int tries = 0; tries < 60 && (int)here.size() < perLevel; ++tries) {
                    const int x = lo + (int)(s.rnd() * (hi - lo + 1));
                    bool ok = x != W / 2 - 1;
                    for (int o : here) ok = ok && std::abs(o - x) >= 9;
                    for (int o : below) ok = ok && std::abs(o - x) >= 5;
                    if (ok) {
                        here.push_back(x);
                    }
                }
                if (here.empty()) {
                    here.push_back(rollsRight(k + 1) ? hi : lo);
                }
            }
            for (int x : here) {
                ladders.push_back({x, k});
            }
            below = here;
        }

        // A barrel already on its way down a lower girder, for a level in
        // full swing.
        if (top >= 2) {
            const int k = 1;
            const int x = rollsRight(k) ? W / 4 : W * 3 / 4;
            barrels.push_back({x, surface(k, x) - 1, k, rollsRight(k) ? 1 : -1, 0, false, 0, 0});
        }
        thrown = false;
        speed = 0;
        jumped = apeLanded = heartShown = false;
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        if (top < 1) {
            return;
        }
        // The happy ending stays up behind the credits, with any barrel left
        // on the floor still rolling away.
        now += dt;
        moveBarrels(dt);
        drawScene(s, c, dt, now, k, true);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        if (top < 1) {
            s.drawFireworks(c, k);
            return;
        }
        const double sec = ms / 1000.0, L = len / 1000.0;
        if (speed == 0) {
            plan(L);
        }
        now = sec;

        // The rivets pop on each lower girder in turn, top down, and then
        // under the ape.
        for (int k2 = top - 1; k2 >= 1; --k2) {
            if (popAt[k2] < 0 && sec >= L * (0.43 + 0.035 * (top - 1 - k2))) {
                popAt[k2] = sec;
                popRivets(s, k2, spanLo(k2), spanHi(k2));
            }
        }
        if (popAt[top] < 0 && sec >= L * 0.56) {
            popAt[top] = sec;
            popRivets(s, top, 0, cut);
        }

        // The barrel the climber hops: thrown (or already rolling) so it
        // reaches him in the middle of his jump.
        if (!thrown && sec >= throwAt) {
            thrown = true;
            const int x = std::max(apeX + 3, (int)std::lround(climberX(jumpAt + JUMP / 2) - speed * (jumpAt + JUMP / 2 - sec)));
            barrels.push_back({x, surface(top, x) - 1, top, 1, 0, false, 0, 0});
        }
        moveBarrels(dt);

        drawScene(s, c, dt, sec, k, false);
        s.drawFireworks(c, k);   // rivet pops, the ape's landing and the heart
    }

private:
    struct Ladder {
        int x, k;   // columns x and x + 1, joining girder k to girder k + 1
    };
    struct Barrel {
        int x, y, k, dir;
        float acc;
        bool falling;
        int ride;    // >0: on girder ride - 1 as it falls away
        int rideY;
    };
    struct Flake {
        float x, y, vy, phase;
    };

    static constexpr double JUMP = 0.6;   // the game's jump time
    static constexpr float G = 60.0f;     // rows a second a second, for falling things

    // ------------------------------------------------------- the level, as the game has it
    bool rollsRight(int k) const { return ((top - k) % 2) == 0; }
    int spanLo(int k) const { return (k == 0 || rollsRight(k)) ? 0 : 3; }
    int spanHi(int k) const { return (k == 0 || !rollsRight(k)) ? W - 1 : W - 4; }
    bool onSpan(int k, int x) const { return x >= spanLo(k) && x <= spanHi(k); }
    int surface(int k, int x) const {
        if (k == 0) {
            return base[0];
        }
        const bool high = rollsRight(k) ? x < W / 2 : x >= W / 2;
        return base[k] - (high ? 1 : 0);
    }
    // How far girder k (or, on the top girder, the ape's end of it) has
    // fallen: a moment's shudder after its rivets pop, then down.
    float drop(int k) const {
        if (popAt[k] < 0) {
            return 0;
        }
        const float t = (float)(now - popAt[k]) - 0.15f;
        return t <= 0 ? 0 : 0.5f * G * t * t;
    }
    bool solid(int k, int x) const { return onSpan(k, x) && (k == 0 || popAt[k] < 0 || (k == top && x > cut)); }

    // ------------------------------------------------------- the timeline, in seconds
    // Climbing until 13% of the way through, then a walk over to the friend,
    // there by 40%, with a hop over the barrel on the way.
    void plan(double L) {
        climbFrom = L * 0.02;
        climbTo = L * 0.13;
        const int dist = ladderX - meetX;
        walkSpeed = std::max(7.0, dist / std::max(0.5, L * 0.40 - climbTo - 0.2));
        walkFrom = std::max(climbTo + 0.2, L * 0.40 - dist / walkSpeed);
        jumpAt = dist >= 6 ? walkFrom + dist / walkSpeed / 2 - JUMP / 2 : climbTo + 0.6;
        // A barrel a little quicker than the first levels', thrown at the
        // right moment - or, on a wide wall, already on its way at the start.
        speed = 12.0f;
        const double pass = jumpAt + JUMP / 2;
        throwAt = std::max(0.0, pass - (climberX(pass) - (apeX + 3)) / speed);
        apeFallAt = L * 0.56 + 0.15;
        heartAt = L * 0.69;
    }
    double climberX(double sec) const {
        if (sec <= walkFrom) {
            return ladderX;
        }
        return std::max((double)meetX, ladderX - walkSpeed * (sec - walkFrom));
    }

    // A rivet every nine LEDs or so, and one near the far end: each a quick
    // pop of short-lived sparks (fireworks' would hang about too long).
    void popRivets(ArcadeShow &s, int k, int x0, int x1) {
        auto pop = [&](int x) {
            for (int i = 0; i < 4; ++i) {
                const float a = 6.2832f * (i + s.rnd()) / 4, v = 4.0f + 4.0f * s.rnd();
                s.sparks.push_back({x + 0.5f, (float)surface(k, x), std::cos(a) * v, std::sin(a) * v - 2.0f, 0,
                                    0.3f + 0.3f * s.rnd(), {1, 0.85f, 0.6f}, 0, 12.0f});
            }
        };
        for (int x = x0 + 1; x <= x1; x += 9) {
            pop(x);
        }
        pop(x1 - 1);
    }

    void moveBarrels(float dt) {
        for (size_t j = 0; j < barrels.size();) {
            Barrel &b = barrels[j];
            bool gone = false;
            if (b.ride > 0) {
                gone = b.rideY + drop(b.ride - 1) >= H;
            } else if (!b.falling && b.k > 0 && popAt[b.k] >= 0 && !(b.k == top && b.x > cut)) {
                // Its girder has gone from under it: it goes down with it.
                b.ride = b.k + 1;
                b.rideY = b.y;
            } else {
                b.acc += (b.falling ? 14.0f : speed) * dt;
                while (b.acc >= 1.0f && !gone) {
                    b.acc -= 1.0f;
                    if (!b.falling) {
                        b.x += b.dir;
                        if (b.x < 0 || b.x >= W) {
                            gone = true;
                        } else if (!solid(b.k, b.x)) {
                            b.falling = true;
                        } else {
                            b.y = surface(b.k, b.x) - 1;
                        }
                    } else {
                        ++b.y;
                        if (b.y >= H) {
                            gone = true;
                        }
                        for (int k = b.k - 1; k >= 0 && !gone; --k) {
                            if (solid(k, b.x) && b.y == surface(k, b.x) - 1) {
                                b.k = k;
                                b.falling = false;
                                b.dir = rollsRight(k) ? 1 : -1;
                                break;
                            }
                        }
                    }
                }
            }
            if (gone) {
                barrels.erase(barrels.begin() + j);
            } else {
                ++j;
            }
        }
    }

    // ------------------------------------------------------- drawing
    void drawScene(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double sec, float k, bool ending) {
        // Girders, the falling ones shuddering then dropping away.
        for (int g = 0; g <= top; ++g) {
            const float d = drop(g);
            const bool shake = popAt[g] >= 0 && d == 0;
            for (int x = spanLo(g); x <= spanHi(g); ++x) {
                const bool loose = g > 0 && popAt[g] >= 0 && (g < top || x <= cut);
                const int y = surface(g, x) + (loose ? (int)d : 0) + (loose && shake && ((x + (int)(sec * 30)) & 1) ? 1 : 0);
                c.add(x, y, GIRDER, ((x % 3 == 0) ? 0.55f : 0.35f) * k);
            }
        }
        for (const auto &l : ladders) {
            // A ladder goes with the girder it stands on (the floor's go with
            // the girder they hold up).
            const int dy = (int)drop(l.k == 0 ? 1 : l.k);
            for (int y = surface(l.k + 1, l.x) + 1; y < surface(l.k, l.x); ++y) {
                const float b = (y & 1) ? 0.4f : 0.25f;
                c.add(l.x, y + dy, LADDER, b * k);
                c.add(l.x + 1, y + dy, LADDER, b * k);
            }
        }
        for (const auto &b : barrels) {
            const int y = b.ride > 0 ? b.rideY + (int)drop(b.ride - 1) : b.y;
            c.set(b.x, y, BARREL, ((b.x & 1) ? 0.9f : 0.6f) * k);
        }

        drawApe(s, c, sec, k);

        // The friend, waving for help until the climber gets there.
        const int fy = surface(top, friendX) - 1;
        const int hopF = sec > heartAt + 0.8 && std::fmod(sec - heartAt, 1.0) < 0.25 ? 1 : 0;
        person(c, friendX, fy - hopF, DRESS, DRESS, 0.9f * k);
        if (sec < walkTo() && std::fmod(sec, 1.0) < 0.5) {
            c.set(friendX - 1, fy - 3, DRESS, 0.6f * k);
        }

        // The climber: up the ladder (all blue, back to you), then across,
        // hopping the barrel; a little hop for joy once the heart is up.
        int cx = (int)std::lround(climberX(sec)), feet = surface(top, cx) - 1;
        bool climbing = false;
        if (sec < climbTo) {
            const int from = surface(top - 1, ladderX) - 1, to = surface(top, ladderX) - 1;
            const double f = std::clamp((sec - climbFrom) / (climbTo - climbFrom), 0.0, 1.0);
            feet = from - (int)std::lround(f * (from - to));
            climbing = true;
        } else if (sec >= jumpAt && sec < jumpAt + JUMP) {
            // The game's hop, kept below the top edge so his head stays on the wall.
            feet -= std::min(feet - 2, (int)std::lround(2.2 * std::sin(3.14159 * (sec - jumpAt) / JUMP)));
        } else if (sec > heartAt + 0.8 && std::fmod(sec - heartAt + 0.5, 1.0) < 0.25) {
            feet -= 1;
        }
        person(c, cx, feet, OVERALLS, climbing ? OVERALLS : SHIRT, k);
        if (!ending && !jumped && sec >= jumpAt + JUMP) {
            jumped = true;
            s.burst(cx, feet - 1, {1, 0.85f, 0.2f}, 8, false);   // the 100 for the hop
        }

        // The heart between them, popping in small then beating.
        if (sec >= heartAt) {
            const int hx = friendX + 2, hy = std::max(0, surface(top, hx + 2) - 4);
            if (!heartShown) {
                heartShown = true;
                s.burst(hx + 2, hy + 1.5f, {1, 0.3f, 0.6f}, 18, false);
            }
            const float beat = 0.7f + 0.3f * (float)std::pow(std::fabs(std::sin((sec - heartAt) * 3.0)), 3.0);
            if (sec - heartAt < 0.15) {
                c.set(hx + 2, hy + 2, HEART, k);
            } else if (sec - heartAt < 0.3) {
                static const char *SMALL[3] = {"X.X", "XXX", ".X."};
                for (int y = 0; y < 3; ++y)
                    for (int x = 0; x < 3; ++x)
                        if (SMALL[y][x] == 'X') c.set(hx + 1 + x, hy + 1 + y, HEART, k);
            } else {
                static const char *BIG[4] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 5; ++x)
                        if (BIG[y][x] == 'X') c.set(hx + x, hy + y, HEART, beat * k);
                c.set(hx + 1, hy + 1, {1, 0.75f, 0.8f}, beat * k);   // a glint
            }
            snow(s, c, dt, sec - heartAt, k);
        }
    }

    double walkTo() const { return walkFrom + (ladderX - meetX) / walkSpeed; }

    static void person(ArcadeCanvas::Canvas &c, int x, int feet, const RGB &legs, const RGB &body, float k) {
        c.set(x, feet, legs, k);
        c.set(x, feet - 1, body, k);
        c.set(x, feet - 2, HEAD, k);
    }

    // The ape pounds its chest (arms up in turn), throws, and then - when its
    // end of the girder goes - tumbles headfirst to the floor and lies there
    // with its feet in the air and stars going round.
    void drawApe(ArcadeShow &s, ArcadeCanvas::Canvas &c, double sec, float k) {
        const int standY = surface(top, apeX) - 3;
        const float a = 0.8f * k;
        if (sec < apeFallAt) {
            const bool shocked = popAt[top] >= 0;
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    c.set(apeX + x, standY + y + (shocked && ((int)(sec * 30) & 1)), (y == 0 && x == 1) ? FACE : APE, a);
            const bool throwing = throwAt > 0.35 && sec >= throwAt - 0.35 && sec < throwAt;
            const int beat = (int)(sec / 0.3) & 1;
            if (shocked || throwing || beat == 0) c.set(apeX - 1, standY, APE, a);
            if (shocked || throwing || beat == 1) c.set(apeX + 3, standY, APE, a);
            return;
        }
        // Falling: the same pull as the girder, drifting right a little, and
        // over onto its head straight away.
        const float t = (float)(sec - apeFallAt);
        const int floorY = surface(0, 0) - 3;   // where it lies, 3 rows tall
        const float fallY = standY + 0.5f * G * t * t;
        const bool down = fallY >= floorY;
        if (down && !apeLanded) {
            apeLanded = true;
            landAt = sec;
            s.burst(landX + 1, floorY + 2, {0.8f, 0.55f, 0.3f}, 14, false);
        }
        const int y0 = down ? floorY : (int)fallY;
        const int x0 = down ? landX : apeX + (int)std::lround(std::min(1.0f, t / 0.85f) * (landX - apeX));
        // Upside down: the face at the bottom.
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x)
                c.set(x0 + x, y0 + y, (y == 2 && x == 1) ? FACE : APE, a);
        if (!down) {
            // Arms flailing.
            const bool f = ((int)(t * 12)) & 1;
            c.set(x0 - 1, y0 + (f ? 1 : 2), APE, a);
            c.set(x0 + 3, y0 + (f ? 2 : 1), APE, a);
        } else {
            // Feet up, kicking now and then; dizzy stars going round.
            const bool kick = std::fmod(sec - landAt, 1.6) < 0.2;
            c.set(x0, y0 - 1 - (kick ? 1 : 0), APE, a);
            c.set(x0 + 2, y0 - 1, APE, a);
            for (int i = 0; i < 2; ++i) {
                const double ang = (sec - landAt) * 5.0 + i * 3.14159;
                c.set(x0 + 1 + (int)std::lround(std::cos(ang) * 2.5), y0 - 3 + (int)std::lround(std::sin(ang) * 1.0),
                      {1, 0.9f, 0.3f}, 0.8f * k);
            }
        }
    }

    // Soft snow once the two of them are together: falling leaves at
    // Halloween, confetti on an everyday show.
    void snow(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double since, float k) {
        if (flakes.empty()) {
            const int n = std::max(6, W * H / 70);
            for (int i = 0; i < n; ++i) {
                flakes.push_back({s.rnd() * W, -s.rnd() * H, 2.0f + s.rnd() * 3.0f, s.rnd() * 6.28f});
            }
        }
        const float in = std::min(1.0f, (float)since / 1.5f);
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x += std::sin((float)since * 1.4f + f.phase) * 1.2f * dt;
            if (f.y >= H) {
                f.y -= H + 1;
                f.x = s.rnd() * W;
            }
            static const RGB LEAF[3] = {{1.0f, 0.4f, 0.0f}, {0.85f, 0.12f, 0.05f}, {0.55f, 0.3f, 0.05f}};
            const RGB col = theme == ShowTheme::Halloween ? LEAF[(int)(f.phase * 10) % 3]
                          : theme == ShowTheme::Everyday ? ArcadeShow::hue(f.phase / 6.28f) : RGB{1, 1, 1};
            c.add((int)f.x, (int)f.y, col, (theme == ShowTheme::Christmas ? 0.35f : 0.5f) * in * k);
        }
    }

    ShowTheme theme = ShowTheme::Christmas;
    int W = 60, H = 25, top = 0;
    std::vector<int> base;
    std::vector<double> popAt;   // when each girder's rivets popped; -1 not yet
    std::vector<Ladder> ladders;
    std::vector<Barrel> barrels;
    std::vector<Flake> flakes;
    int apeX = 1, friendX = 30, meetX = 38, ladderX = 54, cut = 12, landX = 5;
    double now = 0, climbFrom = 0, climbTo = 0, walkFrom = 0, walkSpeed = 7, jumpAt = 0, throwAt = 0;
    double apeFallAt = 0, heartAt = 0, landAt = 0;
    float speed = 0;
    bool thrown = false, jumped = false, apeLanded = false, heartShown = false;
};

// score at the end, and the level reached.
inline ShowConfig config(int score, int level, bool newBest) {
    // ShowColours spelled out: this namespace has its own HEAD (the
    // climber's face).
    namespace SC = ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, SC::GOLD});
    cfg.stats.push_back({"LEVEL", level, false, SC::GREEN});
    cfg.roll = {{"BARREL CLIMB", SC::HEAD}, {"", SC::WHITE},       {"CAST", SC::HEAD},
                {"THE CLIMBER", SC::RED},   {"THE APE", {0.9f, 0.55f, 0.25f}}, {"THE BARRELS", {1.0f, 0.6f, 0.15f}},
                {"", SC::WHITE},            {"THANKS FOR PLAYING", SC::WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(12400, 4, true);
    story = std::make_unique<Story>();
}

}  // namespace BarrelClimbShow
