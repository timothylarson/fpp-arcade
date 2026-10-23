#pragma once
// Star Defenders' end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the
// score and the wave reached, then the war is over - the formation stops
// marching, one alien waves a white flag and they all fly off; the saucer
// crosses one last time and drops a present, which the cannon slides over to
// catch; the cannon fires a spray of fireworks, the last of which becomes a
// Christmas star, and the aliens come back to dance under it - the credits,
// and GAME OVER.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace StarDefendersShow {

using RGB = ArcadeCanvas::RGB;

// The game's look, as in FPPStarDefenders.cpp: 3x2 aliens in three kinds with
// two frames each, the cannon ".#." over "###", arched shields, the red saucer.
const char *const ALIEN[3][2][2] = {
    {{".#.", "#.#"}, {".#.", ".#."}},
    {{"#.#", "###"}, {"###", "#.#"}},
    {{"###", "#.#"}, {"###", ".#."}},
};
const RGB KIND[3] = {{1.0f, 0.2f, 0.9f}, {0.1f, 0.9f, 1.0f}, {0.3f, 1.0f, 0.2f}};
const RGB CANNON = {0.2f, 1.0f, 0.4f};
const RGB SHIELD = {0.1f, 0.85f, 0.2f};
const RGB SHOT = {1.0f, 1.0f, 0.8f};
const RGB SAUCER = {1.0f, 0.1f, 0.1f};
const RGB GOLD = {1.0f, 0.8f, 0.2f};
const RGB PRESENT = {1.0f, 0.12f, 0.1f};

// When each part happens, as a share of the story's length (about 8 s).
constexpr float FLAG_UP = 0.06f;       // the white flag goes up
constexpr float LEAVE_FROM = 0.16f;    // the first alien flies off
constexpr float LEAVE_LAST = 0.30f;    // the flag bearer goes last
constexpr float FLY = 0.09f;           // how long each takes to leave
constexpr float SAUCER_IN = 0.26f;     // the saucer comes on at the left
constexpr float SAUCER_CROSS = 0.20f;  // and takes this long to cross

class Story : public ShowStory {
public:
    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        cols = std::clamp((W - 6) / 5, 3, 8);
        bearer = cols / 2;   // the middle of the top row
        rows = H >= 20 ? 3 : 2;
        fx = (W - (cols * 5 - 2)) / 2;
        fy = std::clamp(H / 5, 1, 5);   // room above for the flag
        aliens.clear();
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int kind = r == 0 ? 0 : (r <= (rows - 1) / 2 ? 1 : 2);
                // The columns peel away from the outside in, the bottom of
                // each first, swinging out to their own side so they don't
                // cross; the flag bearer, top middle, goes last.
                const float out = (float)std::abs(c - bearer) / std::max(1, std::max(bearer, cols - 1 - bearer));
                const float leave = LEAVE_FROM + (LEAVE_LAST - 0.04f - LEAVE_FROM) * (1.0f - out) +
                                    0.012f * (rows - 1 - r) + 0.01f * s.rnd();
                aliens.push_back({c, r, kind, leave, c < bearer ? -1.0f : 1.0f});
            }
        }
        aliens[bearer].leave = LEAVE_LAST;

        stars.clear();
        const int n = std::max(8, W * H / 45);
        for (int i = 0; i < n; ++i) {
            stars.push_back({(int)(s.rnd() * W), (int)(s.rnd() * H * 0.75f), s.rnd() * 6.28f});
        }
        shields.clear();
        const int ns = std::max(2, W / 15);
        for (int k = 0; k < ns; ++k) {
            shields.push_back((int)((k + 0.5f) * W / ns));
        }

        home = W / 2 - 1;
        cannonX = (float)home;
        giftX = W / 2 + 3;   // a little to one side, so the cannon has to go and get it
        giftY = 0;
        gift = 0;
        caughtMs = 0;
        shots.clear();
        fired = 0;
        starOn = false;
        starMs = 0;
        starX = W / 2;
        starY = std::max(3, std::min(5, H / 5));
        starR = std::max(2, std::min(4, H / 6));
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        const float u = (float)(ms / len);
        sky(s, c, k);

        // ---- the formation: marching, then stopped by the flag, then gone.
        // It marches a step every 0.4 s until the flag goes up, then just
        // fidgets on the spot.
        const int steps = (int)(std::min((double)FLAG_UP * len, ms) / 400.0);
        const int march = (steps % 4 < 2 ? steps % 4 : 4 - steps % 4) - 1;
        const int frame = (int)(ms / (u < FLAG_UP ? 400.0 : 200.0)) & 1;
        for (size_t i = 0; i < aliens.size(); ++i) {
            const Alien &a = aliens[i];
            float x = fx + march + a.c * 5.0f, y = fy + a.r * 3.0f;
            if (u > a.leave) {
                // Up and away, swinging out to its side as it goes.
                const float p = (u - a.leave) / FLY;
                if (p >= 1.0f) {
                    continue;
                }
                x += a.side * (std::sin(p * 3.1416f) * 4.0f + p * 3.0f);
                y -= (y + 6.0f) * p * p;
                if (s.rnd() < dt * 5.0f) {
                    s.sparks.push_back({x + 1, y + 2, 0, 2.0f, 0, 0.35f, KIND[a.kind], 0, 0});
                }
            }
            sprite(c, (int)std::lround(x), (int)std::lround(y), a.kind, frame, 1.0f, k);
            if ((int)i == bearer && u > FLAG_UP) {
                flag(c, (int)std::lround(x), (int)std::lround(y), ms, std::min(1.0f, (u - FLAG_UP) / 0.04f), k);
            }
        }

        // ---- the saucer, one last time, dropping a present.
        const float sp = (u - SAUCER_IN) / SAUCER_CROSS;
        if (sp > 0 && sp < 1) {
            const float sx = -5.0f + sp * (W + 6);
            saucer(c, (int)std::floor(sx), ms, k);
            if (gift == 0 && sx + 2 >= giftX) {
                gift = 1;
                giftY = 3;
            }
        }
        if (gift == 1) {
            giftY += 14.0f * dt;
            cannonX += std::clamp(giftX - 1 - cannonX, -14.0f * dt, 14.0f * dt);   // slides under it
            if (giftY + 2 >= H - 3) {
                gift = 2;
                caughtMs = ms;
                s.burst(giftX, H - 4.0f, GOLD, 26, true);
            }
            present(c, giftX, (int)std::lround(giftY), k);
        }

        // ---- fireworks from the cannon, back in the middle: red, green,
        // then one that climbs to the top and becomes the star.
        if (gift == 2) {
            if (ms - caughtMs > 250) {
                cannonX += std::clamp(home - cannonX, -14.0f * dt, 14.0f * dt);
            }
            const double first = caughtMs + 450;
            while (fired < 3 && ms >= first + fired * 220.0) {
                static const float VX[3] = {-10.0f, 10.0f, 0.0f};
                const float to[3] = {H * 0.42f, H * 0.3f, (float)starY};
                shots.push_back({cannonX + 1, H - 3.0f, VX[fired], to[fired], fired});
                ++fired;
            }
        }
        for (size_t i = 0; i < shots.size();) {
            Shot &p = shots[i];
            p.y -= 26.0f * dt;
            p.x += p.vx * dt;
            if (p.y <= p.burstY) {
                static const RGB COLS[2] = {{1, 0.15f, 0.1f}, {0.2f, 1, 0.3f}};
                if (p.which < 2) {
                    s.burst(p.x, p.y, COLS[p.which], 26, false);
                } else {
                    starOn = true;
                    starMs = ms;
                    s.burst((float)starX, (float)starY, GOLD, 20, false);
                }
                shots.erase(shots.begin() + i);
                continue;
            }
            c.set((int)std::lround(p.x), (int)std::lround(p.y), SHOT, k);
            c.add((int)std::lround(p.x - p.vx * 0.04f), (int)std::lround(p.y) + 1, GOLD, 0.4f * k);
            ++i;
        }

        // ---- the aliens come back as friends and dance under the star.
        if (starOn) {
            dance(c, W, H, ms - starMs, k);
        }

        star(s, c, dt, ms, k);
        cannon(c, H, k);
        s.drawFireworks(c, k);   // the trails, the bursts and the star's glitter
    }

    // Behind the credits: the night sky, the shields, the cannon and the star.
    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        sky(s, c, k);
        star(s, c, dt, s.t, k);
        cannon(c, s.H, k);
    }

private:
    struct Alien {
        int c, r, kind;
        float leave;   // when it flies off, as a share of the story
        float side;    // -1 swings out left, 1 right
    };
    struct Shot {
        float x, y, vx, burstY;
        int which;   // 0 red, 1 green, 2 the star
    };
    struct SkyStar {
        int x, y;
        float phase;
    };

    void sprite(ArcadeCanvas::Canvas &c, int x, int y, int kind, int frame, float b, float k) {
        for (int sy = 0; sy < 2; ++sy) {
            for (int sx = 0; sx < 3; ++sx) {
                if (ALIEN[kind][frame][sy][sx] == '#') {
                    c.set(x + sx, y + sy, KIND[kind], b * k);
                }
            }
        }
    }

    // A white flag on a pole above an alien, waving. up (0..1) raises it.
    void flag(ArcadeCanvas::Canvas &c, int x, int y, double ms, float up, float k) {
        const int pole = 1 + (int)std::lround(up * 2);
        for (int d = 1; d <= pole; ++d) {
            c.set(x + 1, y - d, {0.75f, 0.6f, 0.45f}, 0.6f * k);
        }
        if (up < 1.0f) {
            return;
        }
        // Three columns of cloth, two high, a ripple running along it.
        const int top = y - pole;
        for (int j = 0; j < 3; ++j) {
            const int dip = std::sin((float)ms * 0.012f - j * 1.6f) > 0.4f ? 1 : 0;
            for (int r = 0; r < 2; ++r) {
                c.set(x + 2 + j, top + r + (j > 0 ? dip : 0), {1, 1, 1}, (r == 0 ? 1.0f : 0.8f) * k);
            }
        }
    }

    // Bigger than the game's three LEDs, so it reads as the saucer: a dome
    // over a body with its lights blinking in turn.
    void saucer(ArcadeCanvas::Canvas &c, int x, double ms, float k) {
        for (int d = 1; d <= 3; ++d) {
            c.set(x + d, 1, SAUCER, 0.55f * k);
        }
        const int blink = (int)(ms / 150.0) % 3;
        for (int d = 0; d < 5; ++d) {
            const bool light = d % 2 == 1 || d == 2;
            c.set(x + d, 2, light && (d / 2 + blink) % 2 == 0 ? ArcadeCanvas::WHITE : SAUCER, 0.95f * k);
        }
    }

    // A red box with a gold ribbon and a bow; (x, y) is the top of the box.
    void present(ArcadeCanvas::Canvas &c, int x, int y, float k) {
        c.set(x - 1, y - 1, GOLD, 0.9f * k);
        c.set(x + 1, y - 1, GOLD, 0.9f * k);
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                c.set(x + dx, y + dy, dx == 0 || dy == 1 ? GOLD : PRESENT, k);
            }
        }
    }

    // The aliens slide in from both sides into a line and bob in turn.
    void dance(ArcadeCanvas::Canvas &c, int W, int H, double ms, float k) {
        const int n = cols, x0 = (W - (n * 5 - 2)) / 2, y = starY + starR + 4;
        const float in = std::clamp((float)(ms / 700.0), 0.0f, 1.0f);
        const float e = 1.0f - (1.0f - in) * (1.0f - in);
        const int beat = (int)(ms / 280.0);
        for (int i = 0; i < n; ++i) {
            const int to = x0 + i * 5, from = i < n / 2 ? -4 - (n / 2 - i) * 5 : W + 1 + (i - n / 2) * 5;
            const int x = (int)std::lround(from + (to - from) * e);
            const int hop = in >= 1.0f && ((i + beat) & 1) ? 1 : 0;
            sprite(c, x, std::min(y, H - 9) - hop, i % 3, (beat + i) & 1, 1.0f, k);
        }
    }

    // Faint twinkling stars of space, and the shields along the bottom.
    void sky(ArcadeShow &s, ArcadeCanvas::Canvas &c, float k) {
        for (auto &st : stars) {
            c.add(st.x, st.y, {0.8f, 0.85f, 1.0f}, (0.1f + 0.1f * std::sin((float)(s.t / 350.0) + st.phase)) * k);
        }
        const int y = s.H - 6;
        for (int cx : shields) {
            for (int dx = -2; dx <= 2; ++dx) {
                c.set(cx + dx, y, SHIELD, 0.45f * k);
                if (dx != 0) {
                    c.set(cx + dx, y + 1, SHIELD, 0.45f * k);   // the arch, open in the middle
                }
            }
        }
    }

    // The cannon, gold once it has the present.
    void cannon(ArcadeCanvas::Canvas &c, int H, float k) {
        const int x = (int)std::lround(cannonX);
        const RGB col = gift == 2 ? GOLD : CANNON;
        c.set(x + 1, H - 2, col, k);
        for (int d = 0; d < 3; ++d) {
            c.set(x + d, H - 1, col, 0.9f * k);
        }
    }

    // The Christmas star: it opens out from the burst, four long points and
    // four short with a longer one down, the points shimmering in turn, and
    // now and then a glint of glitter falls from it.
    void star(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, float k) {
        if (!starOn) {
            return;
        }
        const float grow = std::clamp((float)((ms - starMs) / 400.0), 0.0f, 1.0f);
        const float r = std::max(1.0f, starR * grow);
        const float shimmer = (float)(ms / 180.0);
        static const int DIRS[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
        for (int d = 0; d < 8; ++d) {
            const bool diag = d & 1;
            const float reach = diag ? r * 0.5f : (d == 4 ? r + 2 : r);
            const float tw = 0.7f + 0.3f * std::sin(shimmer - d * 0.8f);
            for (int i = 1; i <= (int)std::lround(reach); ++i) {
                const float f = 1.0f - (i - 1) / (reach + 1.0f);
                c.add(starX + DIRS[d][0] * i, starY + DIRS[d][1] * i, GOLD, f * tw * k);
            }
        }
        c.set(starX, starY, {1, 1, 0.85f}, k);
        if (s.rnd() < dt * 6.0f * grow) {
            s.sparks.push_back({starX + (s.rnd() - 0.5f) * r * 2, starY + (s.rnd() - 0.5f) * r, (s.rnd() - 0.5f) * 2, 1.0f, 0,
                                1.2f + s.rnd(), GOLD, 20, 1.5f});
        }
    }

    std::vector<Alien> aliens;
    std::vector<SkyStar> stars;
    std::vector<int> shields;
    std::vector<Shot> shots;
    int cols = 8, rows = 3, fx = 0, fy = 3, bearer = 0;
    int home = 0, giftX = 0, gift = 0, fired = 0;   // gift: 0 not yet, 1 falling, 2 caught
    float cannonX = 0, giftY = 0;
    double caughtMs = 0, starMs = 0;
    bool starOn = false;
    int starX = 0, starY = 4, starR = 4;
};

// score and wave at the end of the game; width the model's, since on a
// narrow wall the roll can't fit INVADERS on a line and would split it badly.
inline ShowConfig config(int score, int wave, bool newBest, int width) {
    using namespace ShowColours;
    const RGB INVADERS = {1.0f, 0.2f, 0.9f};
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    cfg.stats.push_back({"WAVE", wave, false, {0.1f, 0.9f, 1.0f}});
    cfg.roll = {{"STAR DEFENDERS", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE CANNON", GREEN}};
    if (ArcadeShow::textW("INVADERS", 1) > width) {
        cfg.roll.push_back({"THE INVAD-", INVADERS});
        cfg.roll.push_back({"ERS", INVADERS});
    } else {
        cfg.roll.push_back({"THE INVADERS", INVADERS});
    }
    cfg.roll.push_back({"THE SAUCER", RED});
    cfg.roll.push_back({"", WHITE});
    cfg.roll.push_back({"THANKS FOR PLAYING", WHITE});
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(4870, 7, true, 60);
    story = std::make_unique<Story>();
}

}  // namespace StarDefendersShow
