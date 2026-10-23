#pragma once
// Stacker's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the score
// and the level reached, then the tower builds itself - a trunk, then rows
// sliding in from alternate sides and landing dead centre, each a little
// narrower - and turns into a Christmas tree: the rows go green from the
// bottom up, the night comes in, the lights come on and a star drops onto
// the top. The tree stays lit behind the credits and GAME OVER.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace StackerShow {

class Story : public ShowStory {
public:
    using RGB = ArcadeCanvas::RGB;

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        // Rows the game's height: two LEDs on a tall panel, else one.
        lh = H >= 20 ? 2 : 1;
        starH = lh == 2 ? 5 : 3;
        // The trunk on the bottom row, the star on top, and as many rows of
        // tree as fit between, three wide at the top and two wider each row.
        n = std::max(3, (H - lh - (starH - 1)) / lh);
        rows.clear();
        rows.push_back({std::max(3, (W / 12) | 1), 0});   // the trunk
        for (int i = n - 1; i >= 0; --i) {
            rows.push_back({std::min(W - 2, 3 + 2 * i), 0});
        }
        for (size_t j = 0; j < rows.size(); ++j) {
            rows[j].side = j % 2 ? 1 : -1;
        }
        // A few lights on every row of the tree, never two side by side.
        bulbs.clear();
        for (int j = 1; j <= n; ++j) {
            const int w = rows[j].w, per = std::max(1, w / 4);
            for (int b = 0; b < per; ++b) {
                const int x = (int)((b + 0.25f + 0.5f * s.rnd()) * w / per);
                bulbs.push_back({j, std::clamp(x, 0, w - 1), (int)(s.rnd() * 5) % 5, s.rnd() * 6.28f});
            }
        }
        // Keep the moon clear of the tree, and smaller, on a narrow panel.
        s.moonX = W * (W < 45 ? 0.88f : 0.8f);
        s.moonR = std::min(s.moonR, std::max(2.5f, W / 10.0f));
        landed = 0;
        starDown = false;
        nextBurst = 0;
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        s.drawNight(c, dt, k);
        drawTree(s, c, s.t, (int)rows.size(), 1.0f, 1.0f, k);
        drawStar(s, c, 0, s.t, k);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W;
        // The building takes the first 55%, a row every `each` ms.
        const double build = len * 0.55, each = build / rows.size(), slide = each * 0.7;
        const int done = std::min((int)rows.size(), (int)(ms / each + 0.3));
        // Then the tree: green from the bottom up, the night, the lights and the star.
        const float green = std::clamp((float)((ms - build) / 900.0), 0.0f, 1.0f);
        const float lights = std::clamp((float)((ms - build - 500.0) / 1200.0), 0.0f, 1.0f);
        const double starAt = build + 1300.0;

        s.drawNight(c, dt, green * k);

        // Rows land: a white flash, as for a perfect drop in the game, and a
        // puff of sparks at either end.
        int settled = 0;
        for (int j = 0; j < (int)rows.size(); ++j) {
            if (ms >= j * each + slide) {
                settled = j + 1;
            }
        }
        while (landed < settled) {
            const Row &r = rows[landed];
            const int y = rowY(s, landed);
            const int x0 = (W - r.w) / 2;
            s.burst(x0 - 0.5f, y + lh / 2.0f, {1, 1, 1}, 5, false);
            s.burst(x0 + r.w + 0.5f, y + lh / 2.0f, {1, 1, 1}, 5, false);
            ++landed;
        }
        drawTree(s, c, ms, settled, green, lights, k);
        for (int j = 0; j < settled; ++j) {
            const double since = ms - (j * each + slide);
            if (since < 250.0) {
                const Row &r = rows[j];
                const int x0 = (W - r.w) / 2, y = rowY(s, j);
                for (int x = x0; x < x0 + r.w; ++x) {
                    for (int dy = 0; dy < lh; ++dy) {
                        c.set(x, y + dy, {1, 1, 1}, (float)(1.0 - since / 250.0) * 0.9f * k);
                    }
                }
            }
        }

        // The row sliding in from its side, brighter, as in the game.
        if (settled < (int)rows.size() && done > settled) {
            const int j = settled;
            const Row &r = rows[j];
            const float f = std::clamp((float)((ms - j * each) / slide), 0.0f, 1.0f);
            const float to = (float)((W - r.w) / 2), from = r.side < 0 ? 0.0f : (float)(W - r.w);
            const int x0 = (int)std::lround(from + (to - from) * f), y = rowY(s, j);
            const RGB col = ArcadeCanvas::mix(rowColour(j), {1, 1, 1}, 0.3f);
            for (int x = x0; x < x0 + r.w; ++x) {
                for (int dy = 0; dy < lh; ++dy) {
                    c.set(x, y + dy, col, k);
                }
            }
        }

        // The goal line the tower is building to, gone once the star's there.
        if (ms < starAt) {
            const int gy = rowY(s, (int)rows.size() - 1) - 1;
            for (int x = 0; x < W; x += 2) {
                c.add(x, gy, {1.0f, 0.85f, 0.2f}, 0.3f * k * (1.0f - green));
            }
        }

        // The star drops onto the top and bounces, then bursts and shines.
        if (ms >= starAt) {
            const float f = std::clamp((float)((ms - starAt) / 600.0), 0.0f, 1.0f);
            const int drop = (int)std::lround((1.0f - ArcadeShow::easeOutBounce(f)) * (starH + 3));
            if (f >= 1.0f && !starDown) {
                starDown = true;
                s.burst(W / 2.0f, starH / 2.0f, {1, 0.85f, 0.3f}, 36, true);
                nextBurst = ms + 1400.0;
            }
            drawStar(s, c, -drop, ms, k);
        }
        // Now and then another burst from the star.
        if (starDown && ms >= nextBurst) {
            nextBurst = ms + 1300.0 + s.rnd() * 600.0;
            static const RGB COLS[] = {{1, 0.2f, 0.15f}, {0.3f, 1, 0.4f}, {1, 0.85f, 0.3f}, {0.5f, 0.6f, 1}};
            s.burst(W / 2.0f + (s.rnd() - 0.5f) * W * 0.6f, s.H * (0.15f + 0.2f * s.rnd()), COLS[(int)(s.rnd() * 4) % 4], 22, false);
        }
        s.drawFireworks(c, k);
    }

private:
    struct Row {
        int w;
        int side;   // slides in from the left (-1) or the right (1)
    };
    struct Bulb {
        int row, x, colour;
        float phase;
    };

    // Row j counted from the bottom (the trunk is 0).
    int rowY(const ArcadeShow &s, int j) const { return s.H - (j + 1) * lh; }
    // The game's rainbow, bottom to top.
    RGB rowColour(int j) const { return ArcadeShow::hue(j / (float)(rows.size() + 1) * 0.85f); }

    // The first `count` rows: the game's colours turning to tree green (and
    // the trunk to brown) by `green`, a wave from the bottom up; then the
    // lights coming on by `lights`, the bottom first.
    void drawTree(ArcadeShow &s, ArcadeCanvas::Canvas &c, double ms, int count, float green, float lights, float k) {
        const int W = s.W;
        const int total = (int)rows.size();
        for (int j = 0; j < count; ++j) {
            const Row &r = rows[j];
            const int x0 = (W - r.w) / 2, y = rowY(s, j);
            const float g = std::clamp(green * 1.6f - 0.6f * j / total, 0.0f, 1.0f);
            const RGB to = j == 0 ? RGB{0.55f, 0.25f, 0.08f} : RGB{0.05f, 0.75f, 0.15f};
            const RGB col = ArcadeCanvas::mix(rowColour(j), to, g);
            for (int x = x0; x < x0 + r.w; ++x) {
                for (int dy = 0; dy < lh; ++dy) {
                    // Snow along the top of each branch, at its tips.
                    const bool snow = j > 0 && dy == 0 && (x == x0 || x == x0 + r.w - 1);
                    c.set(x, y + dy, snow ? ArcadeCanvas::mix(col, {0.9f, 0.95f, 1}, 0.6f * g) : col,
                          (0.7f - 0.15f * g * (dy == lh - 1 && lh > 1)) * k);
                }
            }
        }
        static const RGB BULB[5] = {{1, 0.12f, 0.1f}, {1, 0.75f, 0.15f}, {0.25f, 0.45f, 1}, {1, 0.3f, 0.9f}, {1, 1, 1}};
        for (const auto &b : bulbs) {
            if (b.row >= count || lights * total * 1.1f < b.row) {
                continue;
            }
            const int x0 = (W - rows[b.row].w) / 2, y = rowY(s, b.row);
            const float tw = 0.7f + 0.3f * std::sin((float)(ms / 260.0) + b.phase);
            c.set(x0 + b.x, y + (lh > 1 ? (b.x & 1) : 0), BULB[b.colour], tw * k);
        }
    }

    // The star on top, raised by `up` LEDs (negative while it drops in),
    // twinkling, with a soft glow round it.
    void drawStar(ArcadeShow &s, ArcadeCanvas::Canvas &c, int up, double ms, float k) {
        static const char *BIG[5] = {"..#..", "..#..", "#####", ".###.", ".#.#."};
        static const char *SMALL[3] = {".#.", "###", ".#."};
        const int sz = starH, cx = s.W / 2, x0 = cx - sz / 2, y0 = up;
        const float tw = 0.8f + 0.2f * std::sin((float)(ms / 150.0));
        for (int y = -2; y < sz + 2; ++y) {
            for (int x = -2; x < sz + 2; ++x) {
                const float d = std::hypot(x - sz / 2.0f + 0.5f, y - sz / 2.0f + 0.5f);
                c.add(x0 + x, y0 + y, {1, 0.8f, 0.3f}, std::max(0.0f, 0.18f - 0.05f * d) * tw * k);
            }
        }
        for (int y = 0; y < sz; ++y) {
            for (int x = 0; x < sz; ++x) {
                if ((sz == 5 ? BIG[y][x] : SMALL[y][x]) == '#') {
                    c.set(x0 + x, y0 + y, {1, 0.85f, 0.3f}, tw * k);
                }
            }
        }
        c.set(cx, y0 + sz / 2, {1, 1, 0.85f}, k);   // a bright heart
    }

    std::vector<Row> rows;   // bottom (the trunk) to top
    std::vector<Bulb> bulbs;
    int lh = 2, starH = 5, n = 9, landed = 0;
    bool starDown = false;
    double nextBurst = 0;
};

// score and level: the final score and the level reached.
inline ShowConfig config(int score, int level, bool newBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    cfg.stats.push_back({"LEVEL", level, false, GREEN});
    cfg.roll = {{"STACKER", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE TOWER", GREEN}, {"AND", HEAD},
                {"THE STAR", GOLD}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(1840, 4, true);
    story = std::make_unique<Story>();
}

}  // namespace StackerShow
