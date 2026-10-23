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
// To see a show without the wall, tools/shows/ renders one to images.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
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
    // Where each part starts, as a share of the whole: headline from 0.
    double pStats = 0.14, pStory = 0.30, pRoll = 0.72, pEnd = 0.90;
    // Fireworks a second in each part.
    float rateHeadline = 2.4f, rateStats = 1.0f, rateStory = 0.0f, rateRoll = 0.35f, rateEnd = 4.0f;
};

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

    // A winter night: the faintest blue at the top of the sky, twinkling
    // stars, the moon and falling snow. For stories that want it (Breakout's
    // does). The sky is kept to 2% at most: a coloured fill washes out on the
    // real wall.
    void drawNight(Canvas &c, float dt, float k) {
        for (int y = 0; y < H; ++y) {
            const float g = 0.02f * (1.0f - (float)y / H);
            for (int x = 0; x < W; ++x) {
                c.add(x, y, {0.0f, 0.02f, 1.0f}, g * k);
            }
        }
        for (auto &s : stars) {
            c.add(s.x, s.y, {0.9f, 0.9f, 1.0f}, (0.12f + 0.12f * std::sin((float)(t / 400.0) + s.phase)) * k);
        }
        for (int y = (int)(moonY - moonR - 1); y <= (int)(moonY + moonR + 1); ++y) {
            for (int x = (int)(moonX - moonR - 1); x <= (int)(moonX + moonR + 1); ++x) {
                const float d = std::hypot(x + 0.5f - moonX, y + 0.5f - moonY);
                const float in = ArcadeCanvas::clamp01(moonR - d + 0.5f);
                // A darker patch, so it reads as the moon and not a ball.
                const float spot = (std::hypot(x + 0.5f - (moonX - moonR * 0.3f), y + 0.5f - (moonY - moonR * 0.2f)) < moonR * 0.3f) ? 0.75f : 1.0f;
                c.add(x, y, {1.0f, 0.93f, 0.7f}, 0.5f * in * spot * k);
            }
        }
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
    float moonX = 0, moonY = 0, moonR = 3;

private:
    struct Rocket {
        float x, y, vy, burstY;
        RGB col;
    };
    struct Flake {
        float x, y, vy, phase;
    };
    struct Star {
        int x, y;
        float phase;
    };
    struct Line {
        std::string text;
        RGB col;
    };

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
            rockets.push_back({W * (0.1f + 0.8f * rnd()), (float)H, -(H * 1.3f + rnd() * H * 0.5f),
                               H * (0.12f + 0.35f * rnd()), COLS[(int)(rnd() * 6) % 6]});
        }
        for (size_t i = 0; i < rockets.size();) {
            Rocket &r = rockets[i];
            r.y += r.vy * dt;
            r.vy += H * 0.6f * dt;   // slows toward the top
            if (r.y <= r.burstY || r.vy >= 0) {
                burst(r.x, r.y, r.col, 18 + (int)(rnd() * 16), rnd() < 0.3f);
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
                letter(c, s[i], xx, yy, scl, hue((float)(ms / 2500.0) + idx * 0.13f), k);
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
        const RGB col = {1.0f, 0.85f, 0.4f};
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
