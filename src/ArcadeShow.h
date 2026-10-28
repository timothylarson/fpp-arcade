#pragma once
// End-of-game shows: a short film on the wall after a game worth celebrating.
//
//   headline     big letters bouncing in over fireworks ("NEW BEST!", "YOU WON!")
//   stats        a number or two counting up (SCORE, LEVEL, TIME...)
//   story        the game's own little scene (a Story, below)
//   roll         credits rolling up the screen
//   ending       "GAME OVER" or "THE END" over a last burst of fireworks
//
// A game fills in a ShowConfig and a Story and hands them to an
// ArcadeShowPlayer, which runs it frame by frame in the game's update() and
// lets any button skip it. Everything draws into a canvas the size of the
// model; nothing here knows about any particular game.
//
// Shows come in three themes (ShowTheme.h): Christmas, Halloween and
// Everyday. The engine's own parts follow the theme by themselves - the sky
// (snow, or an orange moon with bats and falling leaves, or a clear night),
// the fireworks' colours, the headline's and the ending's - and ShowArt
// (below) has pictures for stories to put in theirs.
//
// To see a show without the wall, tools/shows/ renders one to images
// (THEME=halloween or THEME=everyday for the others).

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "ArcadeCanvas.h"
#include "FPPArcade.h"

class ArcadeShow;

namespace ShowColours {
const ArcadeCanvas::RGB HEAD = {1.0f, 0.8f, 0.2f};    // headings in the roll
const ArcadeCanvas::RGB WHITE = {1, 1, 1};
const ArcadeCanvas::RGB RED = {1, 0.3f, 0.25f};
const ArcadeCanvas::RGB GREEN = {0.35f, 1, 0.45f};
const ArcadeCanvas::RGB GOLD = {1.0f, 0.8f, 0.2f};
const ArcadeCanvas::RGB LABEL = {0.8f, 0.85f, 1.0f};
}  // namespace ShowColours

// A game's own scene, the middle of the show. draw() is called every frame of
// it with ms since it began, len its length and k its fade (0..1) - multiply
// everything drawn by k. backdrop() draws behind the roll and the ending
// (nothing by default, so they're on black).
class ShowStory {
public:
    virtual ~ShowStory() = default;
    virtual void begin(ArcadeShow &) {}
    virtual void draw(ArcadeShow &show, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) = 0;
    virtual void backdrop(ArcadeShow &, ArcadeCanvas::Canvas &, float /*dt*/, float /*k*/) {}
};

struct ShowStat {
    std::string label;          // "SCORE"
    int value = 0;              // counts up to this
    bool asTime = false;        // value is seconds, shown m:ss
    ArcadeCanvas::RGB colour = ShowColours::GOLD;
};

struct ShowConfig {
    std::vector<std::string> headline = {"NEW", "BEST!"};   // one or two lines
    std::vector<ShowStat> stats;                             // shown one after another
    std::vector<std::pair<std::string, ArcadeCanvas::RGB>> roll;   // credits lines; "" is a gap
    std::string ending = "GAME OVER";                       // split over two lines if too wide
    double lengthMs = 20000;
    ShowTheme theme = arcadeShowTheme();   // the setting when the show is made
    // Where each part starts, as a share of the whole: headline from 0.
    double pStats = 0.14, pStory = 0.30, pRoll = 0.72, pEnd = 0.90;
    // Fireworks a second in each part.
    float rateHeadline = 2.4f, rateStats = 1.0f, rateStory = 0.0f, rateRoll = 0.35f, rateEnd = 4.0f;
};

// Little pictures for the stories, mostly for Halloween and Everyday. x, y is
// the top-left corner; k the fade. On the wall black is just LEDs left off,
// so the coal eyes and the bats' and hats' dark parts are drawn in colour.
namespace ShowArt {
using RGB = ArcadeCanvas::RGB;
using Canvas = ArcadeCanvas::Canvas;
struct Ink {
    char key;
    RGB col;
};
// rows of characters, one a pixel, coloured by key; '.' and unknown keys are clear.
inline void sprite(Canvas &c, const char *const *rows, int n, int x, int y, std::initializer_list<Ink> inks, float k, bool flip = false) {
    for (int r = 0; r < n; ++r) {
        const int w = (int)std::char_traits<char>::length(rows[r]);
        for (int i = 0; i < w; ++i) {
            const char ch = rows[r][flip ? w - 1 - i : i];
            for (const Ink &ink : inks) {
                if (ink.key == ch) {
                    c.set(x + i, y + r, ink.col, k);
                    break;
                }
            }
        }
    }
}
const RGB ORANGE = {1.0f, 0.45f, 0.0f}, STEM = {0.25f, 0.6f, 0.1f}, CANDLE = {1.0f, 0.85f, 0.25f};
const RGB BAT = {0.55f, 0.2f, 0.75f}, GHOST = {0.85f, 0.9f, 1.0f}, BONE = {0.9f, 0.88f, 0.75f};
const RGB HAT = {0.45f, 0.12f, 0.6f}, STONE = {0.22f, 0.08f, 0.3f}, GOLDEN = {1.0f, 0.75f, 0.1f};

// A jack-o'-lantern, 5x5: lit (a candle-yellow face) or plain.
inline void pumpkin(Canvas &c, int x, int y, float k, bool lit = true) {
    static const char *const P[] = {"..s..", ".ooo.", "oyoyo", "oyyyo", ".ooo."};
    sprite(c, P, 5, x, y, {{'s', STEM}, {'o', ORANGE}, {'y', lit ? CANDLE : ORANGE}}, k);
}
// A bat, 5x2, wings up (frame 0) or down (1).
inline void bat(Canvas &c, int x, int y, int frame, bool flip, float k) {
    static const char *const UP[] = {"b...b", ".bbb."}, *const DOWN[] = {".bbb.", "b...b"};
    sprite(c, frame ? DOWN : UP, 2, x, y, {{'b', BAT}}, k, flip);
}
// A friendly ghost, 5x6: hollow eyes, its hem waving (frame 0 or 1).
inline void ghost(Canvas &c, int x, int y, int frame, float k) {
    static const char *const G0[] = {".www.", "wwwww", "w.w.w", "wwwww", "wwwww", "w.w.w"};
    static const char *const G1[] = {".www.", "wwwww", "w.w.w", "wwwww", "wwwww", ".w.w."};
    sprite(c, frame ? G1 : G0, 6, x, y, {{'w', GHOST}}, k);
}
// A dancing cartoon skeleton, 5x8: arms up (frame 0) or out (1).
inline void skeleton(Canvas &c, int x, int y, int frame, float k) {
    static const char *const S0[] = {".www.", ".w.w.", ".www.", "w.w.w", ".www.", "..w..", ".w.w.", "w...w"};
    static const char *const S1[] = {".www.", ".w.w.", ".www.", "..w..", "wwwww", "..w..", ".w.w.", ".w.w."};
    sprite(c, frame ? S1 : S0, 8, x, y, {{'w', BONE}}, k);
}
// A witch's hat, 7x4, with an orange band.
inline void witchHat(Canvas &c, int x, int y, float k) {
    static const char *const W[] = {"...h...", "..hh...", "..ooo..", "hhhhhhh"};
    sprite(c, W, 4, x, y, {{'h', HAT}, {'o', ORANGE}}, k);
}
// Candy corn, 3x3.
inline void candyCorn(Canvas &c, int x, int y, float k) {
    static const char *const C[] = {".w.", ".o.", "yyy"};
    sprite(c, C, 3, x, y, {{'w', {1, 1, 0.9f}}, {'o', ORANGE}, {'y', CANDLE}}, k);
}
// A castle on the skyline, w wide, its base on row ground: dim stone towers
// with battlements and lit windows.
inline void castle(Canvas &c, int x0, int ground, int w, float k) {
    const int h = std::max(5, w / 3);
    for (int x = 0; x < w; ++x) {
        const bool tower = x < w / 5 || x >= w - w / 5 || std::abs(x - w / 2) < w / 10 + 1;
        const int top = ground - (tower ? h + 3 : h);
        for (int y = top; y < ground; ++y) {
            const bool merlon = y == top && x % 2 == 1;   // battlements
            if (!merlon) {
                c.set(x0 + x, y, STONE, k);
            }
        }
        if (tower && x % 3 == 1) {
            c.set(x0 + x, top + 2, CANDLE, 0.7f * k);   // a lit window
        }
    }
}
// Everyday: a gold trophy cup, 5x5, and a balloon on a string, 3x5.
inline void trophy(Canvas &c, int x, int y, float k) {
    static const char *const T[] = {"ggggg", "ggggg", ".ggg.", "..g..", ".ggg."};
    sprite(c, T, 5, x, y, {{'g', GOLDEN}}, k);
}
inline void balloon(Canvas &c, int x, int y, const RGB &col, float k) {
    static const char *const B[] = {".b.", "bbb", ".b.", ".s.", "s.."};
    sprite(c, B, 5, x, y, {{'b', col}, {'s', {0.5f, 0.5f, 0.5f}}}, k);
}
}  // namespace ShowArt

class ArcadeShow {
public:
    using RGB = ArcadeCanvas::RGB;
    using Canvas = ArcadeCanvas::Canvas;

    void start(int w, int h, ShowConfig config, std::unique_ptr<ShowStory> s) {
        W = std::max(8, w);
        H = std::max(8, h);
        cfg = std::move(config);
        T = std::max(5000.0, cfg.lengthMs);
        t = 0;
        rng.seed(std::random_device{}());
        rockets.clear();
        sparks.clear();
        rocketDue = 0;
        statsBursts = 0;
        big = std::max(1, std::min((H - 3) / 11, W / 15));
        roll = std::max(1, H / 40);
        buildSky();
        themeRoll();
        lines.clear();
        for (const auto &l : cfg.roll) {
            addRollLine(l.first, l.second);
        }
        story = std::move(s);
        if (story) {
            story->begin(*this);
        }
    }

    double elapsedMs() const { return t; }
    ShowTheme theme() const { return cfg.theme; }
    bool halloween() const { return cfg.theme == ShowTheme::Halloween; }
    // A colour that shifts with h: the rainbow, or for Halloween orange,
    // purple and green.
    RGB accent(float h) const {
        if (!halloween()) {
            return hue(h);
        }
        static const RGB H3[3] = {{1.0f, 0.45f, 0.0f}, {0.65f, 0.2f, 1.0f}, {0.35f, 1.0f, 0.15f}};
        h = (h - std::floor(h)) * 3;
        const int i = (int)h % 3;
        return ArcadeCanvas::mix(H3[i], H3[(i + 1) % 3], h - std::floor(h));
    }
    bool over() const { return t >= T; }
    void finish() { t = T; }

    void step(double dtMs, Canvas &c) {
        const float dt = (float)std::min(dtMs, 100.0) / 1000.0f;
        t += dtMs;
        c.clear();
        const double s = t / T;

        float rate = cfg.rateHeadline;
        if (s >= cfg.pEnd) {
            rate = cfg.rateEnd;
        } else if (s >= cfg.pRoll) {
            rate = cfg.rateRoll;
        } else if (s >= cfg.pStory) {
            rate = cfg.rateStory;
        } else if (s >= cfg.pStats) {
            rate = cfg.rateStats;
        }
        stepFireworks(dt, rate);

        if (s < cfg.pStats) {
            drawFireworks(c, 0.55f);
            drawHeadline(c, t, phaseFade(0, cfg.pStats));
        } else if (s < cfg.pStory) {
            drawFireworks(c, 0.45f);
            drawStats(c, t - cfg.pStats * T, (cfg.pStory - cfg.pStats) * T, phaseFade(cfg.pStats, cfg.pStory));
        } else if (s < cfg.pRoll) {
            const float k = phaseFade(cfg.pStory, cfg.pRoll);
            if (story) {
                story->draw(*this, c, dt, t - cfg.pStory * T, (cfg.pRoll - cfg.pStory) * T, k);
            } else {
                drawFireworks(c, k);
            }
        } else if (s < cfg.pEnd) {
            const float k = phaseFade(cfg.pRoll, cfg.pEnd);
            if (story) {
                story->backdrop(*this, c, dt, k * 0.6f);
            }
            drawFireworks(c, 0.4f);
            drawRoll(c, (t - cfg.pRoll * T) / ((cfg.pEnd - cfg.pRoll) * T), k);
        } else {
            if (story) {
                story->backdrop(*this, c, dt, 0.5f);
            }
            drawFireworks(c, 0.8f);
            drawEnding(c, t - cfg.pEnd * T);
        }
        // Everything fades out over the last second.
        const float out = (float)std::clamp((T - t) / 1000.0, 0.0, 1.0);
        if (out < 1.0f) {
            c.scale(out);
        }
    }

    // ---------------------------------------------------------- for stories
    int W = 60, H = 25;
    int big = 2;    // the scale of the headline and ending letters
    int roll = 1;   // the scale of the roll's letters
    double t = 0, T = 20000;

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    static RGB hue(float h) {
        h -= std::floor(h);
        return {ArcadeCanvas::clamp01(std::fabs(h * 6 - 3) - 1), ArcadeCanvas::clamp01(2 - std::fabs(h * 6 - 2)),
                ArcadeCanvas::clamp01(2 - std::fabs(h * 6 - 4))};
    }
    static float easeOutBounce(float x) {
        const float n = 7.5625f, d = 2.75f;
        if (x < 1 / d) {
            return n * x * x;
        }
        if (x < 2 / d) {
            x -= 1.5f / d;
            return n * x * x + 0.75f;
        }
        if (x < 2.5f / d) {
            x -= 2.25f / d;
            return n * x * x + 0.9375f;
        }
        x -= 2.625f / d;
        return n * x * x + 0.984375f;
    }

    // Text in the arcade font: 3x5 letters scl times the size, 4scl apart.
    static int textW(const std::string &s, int scl) { return s.empty() ? 0 : (int)s.size() * 4 * scl - scl; }
    void text(Canvas &c, const std::string &s, int x, int y, int scl, const RGB &col, float k) {
        for (size_t i = 0; i < s.size(); ++i) {
            letter(c, s[i], x + (int)i * 4 * scl, y, scl, col, k);
        }
    }
    void letter(Canvas &c, char ch, int x, int y, int scl, const RGB &col, float k) {
        const int w = arcadeGlyphWidth(ch, scl);
        for (int py = 0; py < 5 * scl; ++py) {
            for (int px = 0; px < w; ++px) {
                if (arcadeGlyphScaled(ch, px, py, scl)) {
                    c.set(x + px, y + py, col, k);
                }
            }
        }
    }
    // The largest scale, no more than want, at which s fits the width.
    int fitScale(const std::string &s, int want) const {
        int scl = std::max(1, want);
        while (scl > 1 && textW(s, scl) > W - 2) {
            --scl;
        }
        return scl;
    }
    // Darkens a box so text reads over whatever is behind it.
    void shade(Canvas &c, int x, int y, int w, int h, float k) {
        for (int yy = y - 1; yy <= y + h; ++yy) {
            for (int xx = x - 1; xx <= x + w; ++xx) {
                c.add(xx, yy, {-1, -1, -1}, 0.7f * k);
            }
        }
    }

    // Fireworks and sparks. A story can burst() and add its own sparks; they
    // move with the fireworks, and drawFireworks() draws them all.
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
        float twinkle;   // 0 = steady
        float gravity;
    };
    std::vector<Spark> sparks;
    void burst(float x, float y, const RGB &col, int n, bool rainbow) {
        const float sp = (W + H) / 9.0f;
        for (int k = 0; k < n; ++k) {
            const float a = 6.2832f * (k + rnd() * 0.5f) / n, v = sp * (0.55f + 0.6f * rnd());
            sparks.push_back({x, y, std::cos(a) * v, std::sin(a) * v, 0, 0.9f + 0.8f * rnd(),
                              rainbow ? hue((float)k / n) : col, rnd() < 0.4f ? 25 + rnd() * 20 : 0, 7.0f});
        }
    }
    void drawFireworks(Canvas &c, float k) {
        for (auto &r : rockets) {
            c.add((int)r.x, (int)r.y, {1, 0.8f, 0.4f}, 0.8f * k);
            c.add((int)r.x, (int)r.y + 1, {1, 0.5f, 0.2f}, 0.35f * k);
            c.add((int)r.x, (int)r.y + 2, {1, 0.4f, 0.1f}, 0.15f * k);
        }
        for (auto &p : sparks) {
            float f = std::pow(1.0f - p.age / p.life, 1.5f);
            if (p.twinkle > 0) {
                f *= 0.4f + 0.6f * (0.5f + 0.5f * std::sin(p.age * p.twinkle + p.x));
            }
            c.add((int)std::floor(p.x), (int)std::floor(p.y), p.col, f * k);
        }
    }

    // The night for the theme, for stories that want one (Breakout's does).
    // Christmas: the faintest blue at the top of the sky, twinkling stars, the
    // moon and falling snow. Halloween: a purple sky, a big orange moon, bats
    // flitting across it and falling leaves. Everyday: a clear starry night.
    // The sky is kept to 2% at most: a coloured fill washes out on the real
    // wall.
    void drawNight(Canvas &c, float dt, float k) {
        const RGB sky = halloween() ? RGB{0.6f, 0.0f, 1.0f} : RGB{0.0f, 0.02f, 1.0f};
        for (int y = 0; y < H; ++y) {
            const float g = 0.02f * (1.0f - (float)y / H);
            for (int x = 0; x < W; ++x) {
                c.add(x, y, sky, g * k);
            }
        }
        for (auto &s : stars) {
            c.add(s.x, s.y, {0.9f, 0.9f, 1.0f}, (0.12f + 0.12f * std::sin((float)(t / 400.0) + s.phase)) * (halloween() ? 0.6f : 1.0f) * k);
        }
        drawMoon(c, k);
        if (halloween()) {
            drawBats(c, dt, k);
            drawLeaves(c, dt, k);
        } else if (cfg.theme == ShowTheme::Christmas) {
            drawSnow(c, dt, k);
        }
    }
    // The moon alone: pale, or a big orange harvest moon at Halloween.
    void drawMoon(Canvas &c, float k) {
        const float r = halloween() ? moonR * 1.3f : moonR;
        const RGB col = halloween() ? RGB{1.0f, 0.5f, 0.08f} : RGB{1.0f, 0.93f, 0.7f};
        for (int y = (int)(moonY - r - 1); y <= (int)(moonY + r + 1); ++y) {
            for (int x = (int)(moonX - r - 1); x <= (int)(moonX + r + 1); ++x) {
                const float d = std::hypot(x + 0.5f - moonX, y + 0.5f - moonY);
                const float in = ArcadeCanvas::clamp01(r - d + 0.5f);
                // A darker patch, so it reads as the moon and not a ball.
                const float spot = (std::hypot(x + 0.5f - (moonX - r * 0.3f), y + 0.5f - (moonY - r * 0.2f)) < r * 0.3f) ? 0.75f : 1.0f;
                c.add(x, y, col, (halloween() ? 0.75f : 0.5f) * in * spot * k);
            }
        }
    }
    void drawSnow(Canvas &c, float dt, float k) {
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x += std::sin((float)(t / 700.0) + f.phase) * 1.2f * dt;
            if (f.y >= H) {
                f.y -= H + 1;
                f.x = rnd() * W;
            }
            c.add((int)f.x, (int)f.y, {1, 1, 1}, 0.45f * k);
        }
    }
    // Bats flapping across the sky, one now and then past the moon.
    void drawBats(Canvas &c, float dt, float k) {
        for (auto &b : bats) {
            b.x += b.vx * dt;
            if (b.x > W + 6 || b.x < -6) {
                b.vx = -b.vx;
                b.y = H * (0.1f + 0.35f * rnd());
            }
            const float y = b.y + 1.5f * std::sin((float)(t / 300.0) + b.phase);
            ShowArt::bat(c, (int)b.x, (int)y, (int)(t / 120.0 + b.phase * 3) % 2, b.vx < 0, 0.8f * k);
        }
    }
    // Falling leaves: orange, red and brown, drifting as they fall.
    void drawLeaves(Canvas &c, float dt, float k) {
        static const RGB LEAF[3] = {{1.0f, 0.4f, 0.0f}, {0.85f, 0.12f, 0.05f}, {0.55f, 0.3f, 0.05f}};
        for (size_t i = 0; i < flakes.size(); i += 2) {   // half as many as snowflakes
            Flake &f = flakes[i];
            f.y += f.vy * 0.6f * dt;
            f.x += std::sin((float)(t / 500.0) + f.phase) * 3.0f * dt;
            if (f.y >= H) {
                f.y -= H + 1;
                f.x = rnd() * W;
            }
            c.add((int)f.x, (int)f.y, LEAF[i % 3], 0.5f * k);
        }
    }
    float moonX = 0, moonY = 0, moonR = 3;

private:
    struct Rocket {
        float x, y, vy, burstY;
        RGB col;
    };
    struct Flake {
        float x, y, vy, phase;
    };
    struct Bat {
        float x, y, vx, phase;
    };
    struct Star {
        int x, y;
        float phase;
    };
    struct Line {
        std::string text;
        RGB col;
    };

    // The roll's sign-off for the theme: MERRY CHRISTMAS becomes HAPPY
    // HALLOWEEN, or SEE YOU NEXT TIME on an everyday show, and a Halloween
    // roll without one gets HAPPY HALLOWEEN at the end.
    void themeRoll() {
        if (cfg.theme == ShowTheme::Christmas) {
            return;
        }
        const std::string greeting = halloween() ? "HAPPY HALLOWEEN" : "SEE YOU NEXT TIME";
        bool found = false;
        for (auto &l : cfg.roll) {
            if (l.first == "MERRY CHRISTMAS") {
                l.first = greeting;
                l.second = halloween() ? RGB{1.0f, 0.5f, 0.05f} : l.second;
                found = true;
            }
        }
        if (!found && halloween()) {
            cfg.roll.push_back({"", ShowColours::WHITE});
            cfg.roll.push_back({greeting, {1.0f, 0.5f, 0.05f}});
        }
    }

    // In over the first 300 ms of a part and out over its last 300.
    float phaseFade(double from, double to) const {
        const double a = t - from * T, b = to * T - t;
        return (float)std::clamp(std::min(a, b) / 300.0, 0.0, 1.0);
    }

    void stepFireworks(float dt, float rate) {
        rocketDue -= dt * rate;
        while (rate > 0 && rocketDue <= 0) {
            rocketDue += 0.5f + rnd();
            static const RGB COLS[] = {{1, 0.15f, 0.1f}, {0.2f, 1, 0.3f}, {1, 0.8f, 0.2f}, {0.3f, 0.5f, 1}, {0.9f, 0.3f, 1}, {1, 1, 1}};
            static const RGB SPOOKY[] = {{1, 0.45f, 0}, {0.65f, 0.2f, 1}, {0.35f, 1, 0.15f}, {1, 0.6f, 0.1f}, {0.8f, 0.35f, 1}, {0.95f, 0.9f, 0.7f}};
            const RGB col = (halloween() ? SPOOKY : COLS)[(int)(rnd() * 6) % 6];
            rockets.push_back({W * (0.1f + 0.8f * rnd()), (float)H, -(H * 1.3f + rnd() * H * 0.5f),
                               H * (0.12f + 0.35f * rnd()), col});
        }
        for (size_t i = 0; i < rockets.size();) {
            Rocket &r = rockets[i];
            r.y += r.vy * dt;
            r.vy += H * 0.6f * dt;   // slows toward the top
            if (r.y <= r.burstY || r.vy >= 0) {
                burst(r.x, r.y, r.col, 18 + (int)(rnd() * 16), !halloween() && rnd() < 0.3f);
                rockets.erase(rockets.begin() + i);
            } else {
                ++i;
            }
        }
        for (size_t i = 0; i < sparks.size();) {
            Spark &p = sparks[i];
            p.age += dt;
            p.vy += p.gravity * dt;
            p.vx *= 1.0f - 0.8f * dt;
            p.vy *= 1.0f - 0.8f * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if (p.age >= p.life) {
                sparks[i] = sparks.back();
                sparks.pop_back();
            } else {
                ++i;
            }
        }
    }

    // The headline, letter by letter dropping in and bouncing, in shifting colours.
    void drawHeadline(Canvas &c, double ms, float k) {
        const auto &hl = cfg.headline;
        if (hl.empty()) {
            return;
        }
        int scl = big;
        for (const auto &l : hl) {
            scl = std::min(scl, fitScale(l, big));
        }
        const int n = std::min<int>(2, (int)hl.size());
        const int gap = scl + 1, h = n * 5 * scl + (n - 1) * gap;
        const int y0 = (H - h) / 2;
        int idx = 0;
        for (int l = 0; l < n; ++l) {
            const std::string &s = hl[l];
            const int x0 = (W - textW(s, scl)) / 2, y = y0 + l * (5 * scl + gap);
            for (size_t i = 0; i < s.size(); ++i, ++idx) {
                const float e = easeOutBounce(std::clamp((float)(ms - 150.0 * idx) / 750.0f, 0.0f, 1.0f));
                if (e <= 0 || s[i] == ' ') {
                    continue;
                }
                const int yy = y - (int)std::lround((1.0f - e) * (H + 5 * scl));
                const int xx = x0 + (int)i * 4 * scl;
                shade(c, xx, yy, 3 * scl, 5 * scl, k);
                letter(c, s[i], xx, yy, scl, accent((float)(ms / 2500.0) + idx * 0.13f), k);
            }
        }
    }

    // Each stat in turn, counting up big and centred under its label.
    void drawStats(Canvas &c, double ms, double len, float k) {
        if (cfg.stats.empty()) {
            return;
        }
        const int count = (int)cfg.stats.size();
        const double each = len / count;
        const int which = std::min(count - 1, (int)(ms / each));
        const double local = ms - which * each;
        const ShowStat &st = cfg.stats[which];
        const float f = std::clamp((float)(local / (each * 0.65)), 0.0f, 1.0f);
        const float e = 1.0f - std::pow(1.0f - f, 3.0f);
        const int v = (int)std::lround(st.value * e);
        std::string value;
        if (st.asTime) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d:%02d", v / 60, v % 60);
            value = buf;
        } else {
            value = std::to_string(v);
        }
        const float kk = k * (float)std::clamp(std::min(local, each - local) / 250.0, 0.0, 1.0);
        const int num = fitScale(value, big);
        const int h = 5 + 2 + 5 * num, y = (H - h) / 2;
        const int w = std::max(textW(st.label, 1), textW(value, num));
        shade(c, (W - w) / 2, y, w, h, kk);
        text(c, st.label, (W - textW(st.label, 1)) / 2, y, 1, ShowColours::LABEL, 0.8f * kk);
        text(c, value, (W - textW(value, num)) / 2, y + 7, num, st.colour, kk);
        if (f >= 1.0f && statsBursts <= which) {
            statsBursts = which + 1;
            burst(W * 0.2f, y + h / 2.0f, st.colour, 20, false);
            burst(W * 0.8f, y + h / 2.0f, st.colour, 20, false);
        }
    }

    void buildSky() {
        stars.clear();
        const int n = std::max(6, W * H / 70);
        for (int i = 0; i < n; ++i) {
            stars.push_back({(int)(rnd() * W), (int)(rnd() * H * 0.6f), rnd() * 6.28f});
        }
        flakes.clear();
        const int f = std::max(8, W * H / 55);
        for (int i = 0; i < f; ++i) {
            flakes.push_back({rnd() * W, rnd() * H, 2.0f + rnd() * 3.5f, rnd() * 6.28f});
        }
        moonX = W * 0.74f;
        moonY = H * 0.3f;
        moonR = std::max(2.5f, H / 6.0f);
        bats.clear();
        for (int i = 0; i < std::max(2, W / 25); ++i) {
            bats.push_back({rnd() * W, H * (0.1f + 0.35f * rnd()), (rnd() < 0.5f ? -1.0f : 1.0f) * (5.0f + rnd() * 5.0f), rnd() * 6.28f});
        }
    }

    // Word-wraps a roll line to the width; a word too long for a line is hyphenated.
    void addRollLine(const std::string &s, const RGB &col) {
        const int maxChars = std::max(1, (W + roll) / (4 * roll));
        std::string up;
        for (char ch : s) {
            up += (char)std::toupper((unsigned char)ch);
        }
        std::string cur;
        size_t i = 0;
        while (i < up.size()) {
            size_t j = up.find(' ', i);
            std::string word = up.substr(i, j == std::string::npos ? std::string::npos : j - i);
            i = j == std::string::npos ? up.size() : j + 1;
            while ((int)word.size() > maxChars) {
                if (!cur.empty()) {
                    lines.push_back({cur, col});
                    cur.clear();
                }
                const int cut = std::max(1, maxChars - 1);
                lines.push_back({word.substr(0, cut) + (maxChars > 1 ? "-" : ""), col});
                word = word.substr(cut);
            }
            if (word.empty()) {
                continue;
            }
            if (!cur.empty() && (int)(cur.size() + 1 + word.size()) > maxChars) {
                lines.push_back({cur, col});
                cur.clear();
            }
            cur += (cur.empty() ? "" : " ") + word;
        }
        if (!cur.empty() || s.empty()) {
            lines.push_back({cur, col});
        }
    }
    void drawRoll(Canvas &c, double f, float k) {
        const int lh = 7 * roll;
        const int content = (int)lines.size() * lh;
        const int y0 = H - (int)std::lround(f * (H + content));
        for (size_t i = 0; i < lines.size(); ++i) {
            const int y = y0 + (int)i * lh;
            if (y < -5 * roll || y >= H || lines[i].text.empty()) {
                continue;
            }
            const int x = (W - textW(lines[i].text, roll)) / 2;
            shade(c, x, y, textW(lines[i].text, roll), 5 * roll, k);
            text(c, lines[i].text, x, y, roll, lines[i].col, k);
        }
    }

    // The ending on one line if it fits big, else split at its space.
    void drawEnding(Canvas &c, double ms) {
        const float k = std::clamp((float)(ms / 800.0), 0.0f, 1.0f);
        const RGB col = halloween() ? RGB{1.0f, 0.5f, 0.05f} : RGB{1.0f, 0.85f, 0.4f};
        const std::string &e = cfg.ending;
        std::vector<std::string> parts;
        const size_t sp = e.find(' ');
        if (textW(e, big) <= W - 2 || sp == std::string::npos) {
            parts.push_back(e);
        } else {
            parts.push_back(e.substr(0, sp));
            parts.push_back(e.substr(sp + 1));
        }
        int scl = big;
        for (const auto &p : parts) {
            scl = std::min(scl, fitScale(p, big));
        }
        const int gap = scl + 1, h = (int)parts.size() * 5 * scl + ((int)parts.size() - 1) * gap;
        const int y = (H - h) / 2;
        for (size_t l = 0; l < parts.size(); ++l) {
            const int x = (W - textW(parts[l], scl)) / 2, yy = y + (int)l * (5 * scl + gap);
            shade(c, x, yy, textW(parts[l], scl), 5 * scl, k);
            text(c, parts[l], x, yy, scl, col, k);
        }
    }

    ShowConfig cfg;
    std::unique_ptr<ShowStory> story;
    std::mt19937 rng;
    std::vector<Rocket> rockets;
    float rocketDue = 0;
    int statsBursts = 0;
    std::vector<Star> stars;
    std::vector<Flake> flakes;
    std::vector<Bat> bats;
    std::vector<Line> lines;
};

// Runs a show on a model from a game's update(), and lets the button thread
// skip it. A game keeps one of these, calls start() when its game ends well,
// and while on() calls step() from update() instead of its own frame, and
// skip() from button().
class ArcadeShowPlayer {
public:
    void start(PixelOverlayModel *m, ShowConfig cfg, std::unique_ptr<ShowStory> story) {
        canvas.resize(m->getWidth(), m->getHeight());
        show.start(m->getWidth(), m->getHeight(), std::move(cfg), std::move(story));
        skipping.store(false);
        active.store(true);
    }
    bool on() const { return active.load(); }
    // From button(): any press skips the rest, once it's had two seconds.
    void skip() {
        if (active.load()) {
            skipping.store(true);
        }
    }
    // Draws the next frame. False once the show is over.
    bool step(PixelOverlayModel *m, double dtMs) {
        if (!active.load()) {
            return false;
        }
        if (skipping.exchange(false) && show.elapsedMs() > 2000.0) {
            show.finish();
        }
        show.step(dtMs, canvas);
        canvas.writeTo(m);
        m->flushOverlayBuffer();
        if (show.over()) {
            active.store(false);
            return false;
        }
        return true;
    }
    ArcadeShow show;

private:
    ArcadeCanvas::Canvas canvas;
    std::atomic<bool> active{false};
    std::atomic<bool> skipping{false};
};
