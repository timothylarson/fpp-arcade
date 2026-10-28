#pragma once
// Light Memory's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the
// rounds repeated (and the best, when this wasn't it), then the four
// triangles put on a light show of their own: they play the start of Jingle
// Bells, one triangle a note, then the light spins round them faster and
// faster through every colour, they flash Christmas red and green, and the
// dark lines between them light up gold as the rays of a starburst. Slowly
// turning colours stay behind the credits and GAME OVER.
//
// At Halloween they chime midnight instead - the Westminster quarters, which
// use the game's own four notes, and the hour tolling - and flash orange and
// purple; on an everyday show they play a cheerful run and flash rainbow.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace LightMemoryShow {

class Story : public ShowStory {
public:
    using RGB = ArcadeCanvas::RGB;

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        theme = s.theme();
        // Which triangle each LED belongs to, exactly as the game lays them
        // out: up, right, down, left - or -1 on the dark lines between them.
        // ray is how near an LED is to those lines, for the starburst.
        region.assign(W * H, -1);
        ray.assign(W * H, 0.0f);
        radius.assign(W * H, 0.0f);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const float nx = (x + 0.5f - W / 2.0f) / (W / 2.0f), ny = (y + 0.5f - H / 2.0f) / (H / 2.0f);
                const int i = y * W + x;
                radius[i] = std::sqrt(nx * nx + ny * ny);
                const float line = std::fabs(std::fabs(nx) - std::fabs(ny));
                ray[i] = ArcadeCanvas::clamp01(1.0f - line / 0.16f);
                if (line < 0.08f || radius[i] < 0.2f) {
                    continue;
                }
                region[i] = std::fabs(nx) > std::fabs(ny) ? (nx > 0 ? 1 : 3) : (ny < 0 ? 0 : 2);
            }
        }
        lastNote = -1;
        lastSpin = -1;
        burstDone = false;
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float, float k) override {
        // The triangles in slowly turning colours, dim.
        float lit[4];
        RGB col[4];
        for (int r = 0; r < 4; ++r) {
            col[r] = ArcadeShow::hue((float)(s.t / 6000.0) + r * 0.25f);
            lit[r] = 0.22f;
        }
        paint(s, c, lit, col, k);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        const double song = len * 0.42, spin = len * 0.72, flash = len * 0.86;
        float lit[4] = {0.07f, 0.07f, 0.07f, 0.07f};   // the game's dim, unlit triangles
        RGB col[4] = {COLOURS[0], COLOURS[1], COLOURS[2], COLOURS[3]};
        // The spin's trail fades quicker as it speeds up, so it still reads as going round.
        const float fade = 4.0f + 14.0f * std::clamp((float)((ms - song) / (spin - song)), 0.0f, 1.0f);
        for (int r = 0; r < 4; ++r) {
            glow[r] = std::max(0.0f, glow[r] - dt * fade);
        }

        if (ms < song) {
            // Jingle Bells, one triangle a note: E E E, E E E, E G C D E, with
            // C up, D right, E down and G left - the game's notes go up that way.
            static const int NOTES[] = {2, 2, 2, 2, 2, 2, 2, 3, 0, 1, 2};
            static const float BEATS[] = {1, 1, 2, 1, 1, 2, 1, 1, 1.5f, 0.5f, 4};
            // Midnight: E C D G, G D E C, then the hour tolling (all four at once).
            static const int CHIME[] = {2, 0, 1, 3, 3, 1, 2, 0, 4, 4, 4};
            static const float CHIME_BEATS[] = {1, 1, 1, 2, 1, 1, 1, 2, 2, 2, 2};
            // A cheerful run: C E G E, C E G C, D E G C.
            static const int RUN[] = {0, 2, 3, 2, 0, 2, 3, 0, 1, 2, 3};
            static const float RUN_BEATS[] = {1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 4};
            const int *notes = theme == ShowTheme::Halloween ? CHIME : theme == ShowTheme::Everyday ? RUN : NOTES;
            const float *beats = theme == ShowTheme::Halloween ? CHIME_BEATS : theme == ShowTheme::Everyday ? RUN_BEATS : BEATS;
            const double beat = song / 16.0;
            double at = 0;
            for (int i = 0; i < 11; ++i) {
                const double d = beats[i] * beat;
                if (ms >= at && ms < at + d) {
                    if (ms < at + d * 0.8) {
                        for (int r = 0; r < 4; ++r) {
                            if (notes[i] == r || notes[i] == 4) {
                                lit[r] = 0.95f;
                            }
                        }
                    }
                    if (lastNote != i) {
                        lastNote = i;
                        const int r = notes[i] == 4 ? i % 4 : notes[i];
                        pop(s, r, col[r], notes[i] == 4 ? 10 : 5);
                    }
                }
                at += d;
            }
        } else if (ms < spin) {
            // Round and round, clockwise from the top, faster and faster,
            // the colours turning as it goes.
            const float f = (float)((ms - song) / (spin - song));
            const float turns = 1.5f * f + 5.0f * f * f;   // quarter-turns: speeding up
            const int step = (int)(turns * 4.0f);
            const int r = step % 4;
            if (step != lastSpin) {
                lastSpin = step;
                glow[r] = 1.0f;
            }
            for (int i = 0; i < 4; ++i) {
                col[i] = ArcadeCanvas::mix(COLOURS[i], ArcadeShow::hue((float)(ms / 1500.0) + i * 0.25f), f);
                lit[i] = std::max(lit[i], 0.95f * glow[i]);
            }
        } else if (ms < flash) {
            // Christmas colours: red and green, swapping on the beat - or
            // orange and purple, or the rainbow going round.
            const bool swap = std::fmod(ms - spin, 400.0) < 200.0;
            for (int i = 0; i < 4; ++i) {
                const bool a = (i % 2 == 0) == swap;
                if (theme == ShowTheme::Halloween) {
                    col[i] = a ? ShowArt::ORANGE : RGB{0.65f, 0.2f, 1.0f};
                } else if (theme == ShowTheme::Everyday) {
                    col[i] = ArcadeShow::hue((float)((ms - spin) / 800.0) + i * 0.25f);
                } else {
                    col[i] = a ? RGB{1, 0.1f, 0.08f} : RGB{0.1f, 1, 0.2f};
                }
                lit[i] = 0.85f;
            }
        } else {
            // The starburst: everything bright and gold from the middle out,
            // then fading back as the rays shine on.
            const float f = (float)((ms - flash) / (len - flash));
            if (!burstDone) {
                burstDone = true;
                s.burst(W / 2.0f, H / 2.0f, {1, 0.85f, 0.3f}, 60, true);
                const bool hallow = theme == ShowTheme::Halloween;
                s.burst(W * 0.25f, H * 0.3f, hallow ? ShowArt::ORANGE : RGB{1, 0.1f, 0.1f}, 24, theme == ShowTheme::Everyday);
                s.burst(W * 0.75f, H * 0.3f, hallow ? RGB{0.65f, 0.2f, 1.0f} : RGB{0.2f, 1, 0.3f}, 24, theme == ShowTheme::Everyday);
            }
            for (int i = 0; i < 4; ++i) {
                col[i] = ArcadeCanvas::mix({1, 0.85f, 0.35f}, COLOURS[i], std::min(1.0f, f * 1.5f));
                lit[i] = 0.9f - 0.5f * f;
            }
            paint(s, c, lit, col, k);
            starburst(s, c, f, k);
            s.drawFireworks(c, k);
            return;
        }
        paint(s, c, lit, col, k);
        // The game's soft white dot in the middle, keeping time.
        c.add(W / 2, H / 2, {1, 1, 1}, (0.5f + 0.3f * std::sin((float)ms * 0.012f)) * k);
        s.drawFireworks(c, k);
    }

private:
    // Up green, right red, down yellow, left blue, as in the game.
    static constexpr RGB COLOURS[4] = {{0.1f, 1.0f, 0.2f}, {1.0f, 0.1f, 0.1f}, {1.0f, 0.85f, 0.0f}, {0.1f, 0.3f, 1.0f}};

    void paint(ArcadeShow &s, ArcadeCanvas::Canvas &c, const float lit[4], const RGB col[4], float k) {
        for (int i = 0; i < s.W * s.H; ++i) {
            const int r = region[i];
            if (r >= 0) {
                c.add(i % s.W, i / s.W, col[r], lit[r] * k);
            }
        }
    }

    // A few sparks flying out of the middle of a triangle as its note plays.
    void pop(ArcadeShow &s, int r, const RGB &col, int n) {
        static const float DX[4] = {0, 0.6f, 0, -0.6f}, DY[4] = {-0.55f, 0, 0.55f, 0};
        s.burst(s.W / 2.0f * (1 + DX[r]), s.H / 2.0f * (1 + DY[r]), col, n, false);
    }

    // A ring of gold light spreading from the middle, and the dark lines
    // between the triangles lit as the star's rays, twinkling.
    void starburst(ArcadeShow &s, ArcadeCanvas::Canvas &c, float f, float k) {
        const float ring = f * 2.2f;
        const float tw = 0.75f + 0.25f * std::sin(f * 40.0f);
        for (int i = 0; i < s.W * s.H; ++i) {
            const float d = std::fabs(radius[i] - ring);
            const float onRing = ArcadeCanvas::clamp01(1.0f - d / 0.12f) * (1.0f - f);
            const float onRay = region[i] < 0 ? std::min(1.0f, f * 4.0f) * tw * (radius[i] < f * 3.0f) : 0.0f;
            const float a = std::max(onRing, onRay * ray[i]);
            if (a > 0) {
                c.add(i % s.W, i / s.W, {1, 0.9f, 0.5f}, a * k);
            }
        }
        c.set(s.W / 2, s.H / 2, {1, 1, 0.9f}, k);
        c.set(s.W / 2 - 1, s.H / 2, {1, 1, 0.9f}, k);
        c.set(s.W / 2, s.H / 2 - 1, {1, 1, 0.9f}, k);
        c.set(s.W / 2 - 1, s.H / 2 - 1, {1, 1, 0.9f}, k);
    }

    std::vector<int> region;
    std::vector<float> ray, radius;
    float glow[4] = {0, 0, 0, 0};   // the spin's trail
    ShowTheme theme = ShowTheme::Christmas;
    int lastNote = -1, lastSpin = -1;
    bool burstDone = false;
};

// rounds: rounds repeated; previousBest the best before this game.
inline ShowConfig config(int rounds, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"ROUNDS", rounds, false, GREEN});
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"LIGHT MEMORY", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"FOUR LIGHTS", GREEN}, {"AND", HEAD},
                {"YOUR MEMORY", RED}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(14, false, 18);
    story = std::make_unique<Story>();
}

}  // namespace LightMemoryShow
