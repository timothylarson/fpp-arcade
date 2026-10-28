#pragma once
// Orb Pop's end show (see ArcadeShow.h): NEW BEST! or GREAT ROUND!, the
// round's score (and the best, when this wasn't it), then the orbs' own
// curtain call - they drift in from every side over the game's flickering
// field, gather into one giant orb that swells and trembles, and it bursts in
// a shower of sparkles, setting free a flight of little orbs that float up
// like lanterns into the falling snow - the credits, and WELL DONE.
//
// Everything is made of the same flickering "characters" as the game: each
// LED takes a new random weight every so often, and every layer is
// multiplied by it.

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace OrbPopShow {

using RGB = ArcadeCanvas::RGB;

// The game's colours (its Palette setting), passed in by the game.
struct Look {
    RGB field;                  // the dim flickering background
    RGB energy;                 // ripples
    std::array<RGB, 4> orbs;    // the orbs' colours
};
const RGB GOLD = {1.0f, 0.85f, 0.3f};

class Story : public ShowStory {
public:
    explicit Story(const Look &l) : look(l) {}

    void begin(ArcadeShow &s) override {
        theme = s.theme();
        const int W = s.W, H = s.H;
        unit = std::min((float)W, H * 2.4f) / 60.0f;   // the game's sizes are tuned on a 60x25 wall
        glyph.assign(W * H, 0.0f);
        glyphNext.assign(W * H, 0.0);
        for (auto &g : glyph) {
            g = s.rnd();
        }
        // Seven orbs, one of them gold, spaced around the wall and arriving
        // at the middle one after another.
        gather.clear();
        const int n = 7;
        const float far = std::hypot((float)W, (float)H) * 0.5f + 4.0f;
        for (int i = 0; i < n; ++i) {
            Gatherer g;
            g.angle = 6.2832f * (i + 0.3f * s.rnd()) / n;
            g.far = far;
            g.arrive = 0.16f + 0.24f * i / (n - 1);
            g.rad = std::max(1.6f, (2.2f + s.rnd() * 1.2f) * unit);
            g.gold = i == 3;
            g.col = g.gold ? GOLD : look.orbs[i % 4];
            g.phase = s.rnd() * 6.0f;
            gather.push_back(g);
        }
        cx = W / 2.0f;
        cy = H / 2.0f;
        bigMax = std::min(H * 0.42f, W * 0.3f);
        bigR = 0;
        absorbed = 0;
        burst = false;
        burstMs = 0;
        lanterns.clear();
        flakes.clear();
        for (int i = 0; i < std::max(8, W * H / 55); ++i) {
            flakes.push_back({s.rnd() * W, s.rnd() * H, 2.0f + s.rnd() * 3.5f, s.rnd() * 6.28f});
        }
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const float f = (float)(ms / len);
        flicker(s);
        const float night = burst ? std::clamp((float)(ms - burstMs) / 1200.0f, 0.0f, 1.0f) : 0.0f;
        drawField(s, c, k);
        if (night > 0) {
            snow(s, c, dt, night * k);
        }

        // Light from a slowly circling direction, as in the game.
        float L[3];
        const double a = s.t * 0.0007;
        lightFrom(std::cos(a) * 0.6f, -0.55f, 0.6f, L);

        // 1. The orbs drift in, curling toward the middle, and are swallowed.
        for (auto &g : gather) {
            if (g.in) {
                continue;
            }
            const float e = std::clamp(f / g.arrive, 0.0f, 1.0f);
            const float ease = e * e * (3 - 2 * e);
            const float d = g.far * (1.0f - ease);
            const float ang = g.angle + 1.4f * ease;
            const float x = cx + std::cos(ang) * d * 1.6f, y = cy + std::sin(ang) * d * 0.8f;
            if (e >= 1.0f || (bigR > 0 && d < bigR * 0.6f)) {
                g.in = true;
                ++absorbed;
                s.burst(x, y, g.col, 8, false);
                continue;
            }
            const float rad = g.rad * (0.92f + 0.08f * std::sin((float)ms * 0.006f + g.phase));
            orb(s, c, x, y, rad, g.col, g.gold, L, k);
        }

        // 2. The giant orb: grows with each one swallowed, then swells and
        // trembles faster and faster until it bursts.
        const float tBurst = 0.56f;
        if (!burst && absorbed > 0) {
            const float want = bigMax * (0.45f + 0.45f * absorbed / (float)gather.size());
            bigR += (want - bigR) * std::min(1.0f, dt * 5.0f);
            const float tension = std::clamp((f - 0.42f) / (tBurst - 0.42f), 0.0f, 1.0f);
            const float rad = bigR * (1.0f + 0.12f * tension) * (0.95f + (0.05f + 0.05f * tension)
                              * std::sin((float)ms * (0.008f + 0.03f * tension * tension)));
            const float shake = tension * tension * 0.8f * unit;
            const float bx = cx + std::sin((float)ms * 0.07f) * shake, by = cy + std::cos((float)ms * 0.09f) * shake * 0.6f;
            // Its colour runs through the palette, whitening as it's about to go.
            const float hp = (float)(ms / 700.0);
            const int j = (int)hp;
            RGB col = ArcadeCanvas::mix(look.orbs[j % 4], look.orbs[(j + 1) % 4], hp - j);
            col = ArcadeCanvas::mix(col, ArcadeCanvas::WHITE, 0.5f * tension * tension);
            orb(s, c, bx, by, rad, col, true, L, k);
            if (f >= tBurst) {
                pop(s, ms);
            }
        }

        // 3. After the burst: a ring of ripple racing out, a flash, and the
        // lanterns floating up through the snow.
        if (burst) {
            const float since = (float)(ms - burstMs);
            const float flash = std::max(0.0f, 1.0f - since / 200.0f);
            if (flash > 0) {
                for (int y = 0; y < s.H; ++y) {
                    for (int x = 0; x < s.W; ++x) {
                        c.add(x, y, ArcadeCanvas::WHITE, 0.12f * flash * k);
                    }
                }
            }
            const float ringR = bigR + since * 0.03f * unit * 1.6f;
            const float ringK = std::max(0.0f, 1.0f - since / 1100.0f);
            if (ringK > 0) {
                ring(s, c, ringR, ringK * k);
            }
            stepLanterns(s, dt, false);
            drawLanterns(s, c, L, k);
        }
        s.drawFireworks(c, k);
    }

    // Behind the credits: the field, the snow and lanterns still rising.
    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        flicker(s);
        drawField(s, c, 0.7f * k);
        snow(s, c, dt, k);
        float L[3];
        const double a = s.t * 0.0007;
        lightFrom(std::cos(a) * 0.6f, -0.55f, 0.6f, L);
        stepLanterns(s, dt, true);
        drawLanterns(s, c, L, k);
    }

private:
    struct Gatherer {
        float angle, far, arrive, rad, phase;
        bool gold;
        RGB col;
        bool in = false;
    };
    struct Flake {
        float x, y, vy, phase;
    };
    struct Lantern {
        float x, y, vx, vy, rad, phase, age;
        RGB col;
        bool gold;
    };

    static void lightFrom(float x, float y, float z, float L[3]) {
        const float l = std::sqrt(x * x + y * y + z * z);
        L[0] = x / l;
        L[1] = y / l;
        L[2] = z / l;
    }

    // Each LED takes a new random weight now and then: the characters changing.
    void flicker(ArcadeShow &s) {
        for (size_t i = 0; i < glyph.size(); ++i) {
            if (s.t >= glyphNext[i]) {
                glyph[i] = std::pow(s.rnd(), 1.4f);
                glyphNext[i] = s.t + 90.0 + s.rnd() * 650.0;
            }
        }
    }
    float gl(const ArcadeShow &s, int x, int y) const { return glyph[y * s.W + x]; }

    // The game's background: dim characters with slow brighter clouds.
    void drawField(ArcadeShow &s, ArcadeCanvas::Canvas &c, float k) {
        const double now = s.t;
        for (int y = 0; y < s.H; ++y) {
            for (int x = 0; x < s.W; ++x) {
                const float cloud = (float)((0.5 + 0.5 * std::sin(x * 0.21 + now * 0.00031 + std::sin(y * 0.3 + now * 0.0002) * 1.7))
                                            * (0.5 + 0.5 * std::sin(y * 0.37 - now * 0.00023 + x * 0.05)));
                c.add(x, y, look.field, (0.05f + 0.1f * cloud * cloud) * (0.25f + 0.75f * gl(s, x, y)) * k);
            }
        }
    }

    // Snow drifting down. Not ArcadeShow's drawNight(): its moon is as big as
    // an orb here, and on a 30-wide half it sat among the lanterns like one.
    void snow(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) {
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x += std::sin((float)(s.t / 700.0) + f.phase) * 1.2f * dt;
            if (f.y >= s.H) {
                f.y -= s.H + 1;
                f.x = s.rnd() * s.W;
            }
            // Snow; leaves at Halloween; confetti on an everyday show.
            static const RGB LEAF[3] = {{1.0f, 0.4f, 0.0f}, {0.85f, 0.12f, 0.05f}, {0.55f, 0.3f, 0.05f}};
            const RGB col = theme == ShowTheme::Halloween ? LEAF[(int)(f.phase * 10) % 3]
                          : theme == ShowTheme::Everyday ? ArcadeShow::hue(f.phase / 6.28f) : ArcadeCanvas::WHITE;
            c.add((int)f.x, (int)f.y, col, (theme == ShowTheme::Christmas ? 0.4f : 0.5f) * k);
        }
    }

    // A shaded orb of flickering characters, lit from L, as the game draws them.
    void orb(ArcadeShow &s, ArcadeCanvas::Canvas &c, float ox, float oy, float rad, const RGB &col, bool sparkly,
             const float L[3], float k) {
        const int x0 = std::max(0, (int)std::floor(ox - rad)), x1 = std::min(s.W - 1, (int)std::ceil(ox + rad));
        const int y0 = std::max(0, (int)std::floor(oy - rad)), y1 = std::min(s.H - 1, (int)std::ceil(oy + rad));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float nx = (x + 0.5f - ox) / rad, ny = (y + 0.5f - oy) / rad, d2 = nx * nx + ny * ny;
                if (d2 >= 1.0f) {
                    continue;
                }
                const float nz = std::sqrt(1.0f - d2);
                const float edge = ArcadeCanvas::clamp01((1.0f - std::sqrt(d2)) * rad);
                const float lam = std::max(0.0f, nx * L[0] + ny * L[1] + nz * L[2]) * edge;
                const float kk = (0.3f + 1.0f * std::pow(lam, 1.2f)) * (0.7f + 0.3f * gl(s, x, y));
                c.add(x, y, ArcadeCanvas::mix(col, ArcadeCanvas::WHITE, lam * lam * 0.6f), kk * k);
                if (sparkly && s.rnd() < 0.05f) {
                    c.add(x, y, ArcadeCanvas::WHITE, 0.7f * k);
                }
            }
        }
    }

    void ring(ArcadeShow &s, ArcadeCanvas::Canvas &c, float r, float k) {
        const int n = std::max(24, (int)(r * 7));
        for (int i = 0; i < n; ++i) {
            const float a = 6.2832f * i / n;
            const int x = (int)std::lround(cx + std::cos(a) * r * 1.3f), y = (int)std::lround(cy + std::sin(a) * r);
            if (x >= 0 && x < s.W && y >= 0 && y < s.H) {
                c.add(x, y, look.energy, 0.9f * k * (0.5f + 0.5f * gl(s, x, y)));
            }
        }
    }

    // The giant orb bursts: sparkles everywhere, and the lanterns set free.
    void pop(ArcadeShow &s, double ms) {
        burst = true;
        burstMs = ms;
        s.burst(cx, cy, ArcadeCanvas::WHITE, 70, true);
        for (int i = 0; i < 4; ++i) {
            s.burst(cx, cy, look.orbs[i], 18, false);
        }
        s.burst(cx, cy, GOLD, 18, false);
        const int n = std::max(6, std::min(12, s.W / 5));
        for (int i = 0; i < n; ++i) {
            const float a = 6.2832f * (i + s.rnd() * 0.6f) / n;
            const float sp = (5.0f + 6.0f * s.rnd()) * unit;
            lanterns.push_back(lantern(s, cx, cy, std::cos(a) * sp * 2.2f, std::sin(a) * sp, i));
        }
    }

    Lantern lantern(ArcadeShow &s, float x, float y, float vx, float vy, int i) {
        Lantern l;
        l.x = x;
        l.y = y;
        l.vx = vx;
        l.vy = vy;
        l.rad = std::max(1.4f, (1.7f + s.rnd() * 0.8f) * unit);
        l.phase = s.rnd() * 6.0f;
        l.age = 0;
        l.gold = s.rnd() < 0.15f;
        l.col = l.gold ? GOLD : look.orbs[i % 4];
        if (theme == ShowTheme::Halloween) {
            // Orange, purple and green lanterns, and a candle-yellow gold one.
            static const RGB SPOOKY[3] = {{1, 0.45f, 0}, {0.65f, 0.2f, 1}, {0.35f, 1, 0.15f}};
            l.col = l.gold ? ShowArt::CANDLE : SPOOKY[i % 3];
        }
        return l;
    }

    // Thrown out by the burst, they slow, then rise and sway like lanterns.
    // Behind the credits new ones keep coming up from below.
    void stepLanterns(ArcadeShow &s, float dt, bool refill) {
        const float rise = -s.H / 8.0f;   // px/s: about eight seconds to cross the wall
        for (auto &l : lanterns) {
            l.age += dt;
            l.vx *= 1.0f - std::min(1.0f, 1.2f * dt);
            l.vy += (rise - l.vy) * std::min(1.0f, 1.2f * dt);
            l.x += (l.vx + std::sin(l.age * 1.6f + l.phase) * 1.2f * unit) * dt;
            l.y += l.vy * dt;
        }
        lanterns.erase(std::remove_if(lanterns.begin(), lanterns.end(),
                                      [](const Lantern &l) { return l.y < -l.rad - 1; }),
                       lanterns.end());
        const size_t want = (size_t)std::max(4, s.W / 8);
        if (refill && lanterns.size() < want && s.rnd() < dt * 2.0f) {
            Lantern l = lantern(s, s.W * (0.08f + 0.84f * s.rnd()), s.H + 2.0f, 0, rise, (int)(s.rnd() * 4));
            lanterns.push_back(l);
        }
    }

    void drawLanterns(ArcadeShow &s, ArcadeCanvas::Canvas &c, const float L[3], float k) {
        for (auto &l : lanterns) {
            // A soft warm glow flickering in each, like a candle inside.
            const float glow = 0.85f + 0.15f * std::sin(l.age * 9.0f + l.phase);
            orb(s, c, l.x, l.y, l.rad, l.col, l.gold, L, glow * k);
        }
    }

    Look look;
    ShowTheme theme = ShowTheme::Christmas;
    float unit = 1;
    std::vector<float> glyph;
    std::vector<double> glyphNext;
    std::vector<Gatherer> gather;
    std::vector<Lantern> lanterns;
    std::vector<Flake> flakes;
    float cx = 0, cy = 0, bigR = 0, bigMax = 8;
    int absorbed = 0;
    bool burst = false;
    double burstMs = 0;
};

// score: the round's score; previousBest the best before it.
inline ShowConfig config(int score, bool newBest, int previousBest, const Look &look) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "ROUND!"};
    cfg.stats.push_back({"SCORE", score, false, look.orbs[0]});
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"ORB POP", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"YOU", look.orbs[2]}, {"AND", HEAD},
                {"THE ORBS", look.orbs[1]}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "WELL DONE";
    return cfg;
}

// For tools/shows/preview.sh: the Frost palette.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    const Look frost = {{0.22f, 0.38f, 1.0f}, {0.55f, 0.9f, 1.0f},
                        {{{1.0f, 0.55f, 0.15f}, {1.0f, 0.2f, 0.45f}, {0.55f, 1.0f, 0.3f}, {0.75f, 0.35f, 1.0f}}}};
    cfg = config(52, false, 61, frost);
    story = std::make_unique<Story>(frost);
}

}  // namespace OrbPopShow
