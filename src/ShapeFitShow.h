#pragma once
// Shape Fit's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the
// total (and the best, when this wasn't it), then the five poses just played
// dance across the wall in a conga line - step, step, step, kick - each one
// filled green, as if fitted perfectly. Then a big STAR outline appears, fills
// up green from its feet, flashes like a scored round and turns gold, with
// sparks from its hands, feet and head - the credits, and WELL DONE.
//
// The poses live here rather than in FPPShapeFit.cpp, so the show dances the
// very outlines the game used, and tools/shows/preview.sh (which only sees
// this header) can draw them too.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace ShapeFitShow {

using RGB = ArcadeCanvas::RGB;

// Poses are drawn from circles and thick line segments in "person units": a
// standing person is 1 tall, x = 0 is their middle and y = 0 the top of their
// head. On the wall they are stretched sideways to match how the Kinect's
// picture is stretched onto the matrix: the Kinect sees 57 x 43 degrees, and
// the model is 60 x 25 LEDs, so a person comes out about 1.8 times wider than
// their own proportions.
struct Prim {
    float x1, y1, x2, y2, r;   // a segment of radius r; a circle when the ends match
};
struct Pose {
    const char *name;
    std::vector<Prim> parts;
};

inline const std::vector<Pose> POSES = {
    {"STAR", {{0, .09f, 0, .09f, .08f}, {0, .18f, 0, .55f, .09f}, {0, .26f, -.38f, .08f, .05f}, {0, .26f, .38f, .08f, .05f},
              {0, .55f, -.26f, 1, .06f}, {0, .55f, .26f, 1, .06f}}},
    {"ARMS UP", {{0, .2f, 0, .2f, .08f}, {0, .29f, 0, .62f, .09f}, {-.06f, .32f, -.22f, 0, .05f}, {.06f, .32f, .22f, 0, .05f},
                 {-.05f, .62f, -.07f, 1, .06f}, {.05f, .62f, .07f, 1, .06f}}},
    {"T", {{0, .09f, 0, .09f, .08f}, {0, .18f, 0, .55f, .09f}, {-.46f, .27f, .46f, .27f, .05f},
           {-.05f, .55f, -.07f, 1, .06f}, {.05f, .55f, .07f, 1, .06f}}},
    {"CROUCH", {{0, .42f, 0, .42f, .08f}, {0, .7f, 0, .7f, .22f}, {-.18f, .6f, -.3f, .8f, .05f}, {.18f, .6f, .3f, .8f, .05f},
                {-.12f, .85f, -.18f, 1, .07f}, {.12f, .85f, .18f, 1, .07f}}},
    {"LEAN", {{.24f, .12f, .24f, .12f, .08f}, {.18f, .2f, 0, .55f, .09f}, {.16f, .28f, .5f, .2f, .05f}, {.1f, .3f, -.18f, .5f, .05f},
              {0, .55f, -.12f, 1, .06f}, {0, .55f, .14f, 1, .06f}}},
    {"FLAMINGO", {{0, .09f, 0, .09f, .08f}, {0, .18f, 0, .55f, .09f}, {0, .26f, -.3f, .4f, .05f}, {0, .26f, .3f, .4f, .05f},
                  {0, .55f, 0, 1, .06f}, {0, .58f, .2f, .72f, .06f}, {.2f, .72f, .05f, .8f, .05f}}},
    // Two people side by side, holding hands: one for groups.
    {"PAIR", {{-.34f, .12f, -.34f, .12f, .07f}, {-.34f, .2f, -.34f, .58f, .08f}, {-.34f, .3f, 0, .42f, .045f},
              {-.34f, .3f, -.6f, .48f, .045f}, {-.37f, .58f, -.42f, 1, .055f}, {-.31f, .58f, -.26f, 1, .055f},
              {.34f, .12f, .34f, .12f, .07f}, {.34f, .2f, .34f, .58f, .08f}, {.34f, .3f, 0, .42f, .045f},
              {.34f, .3f, .6f, .48f, .045f}, {.37f, .58f, .42f, 1, .055f}, {.31f, .58f, .26f, 1, .055f}}},
};

constexpr float KINECT_STRETCH = 1.8f;

// The game's colours.
const RGB OUTLINE = {0.1f, 0.9f, 1.0f};
const RGB INSIDE = {0.2f, 1.0f, 0.3f};
const RGB OUTSIDE = {1.0f, 0.25f, 0.1f};
const RGB CLOCK = {1.0f, 0.8f, 0.2f};

inline float segDist(float px, float py, const Prim &p) {
    const float dx = p.x2 - p.x1, dy = p.y2 - p.y1, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((px - p.x1) * dx + (py - p.y1) * dy) / l2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float ex = p.x1 + t * dx - px, ey = p.y1 + t * dy - py;
    return std::sqrt(ex * ex + ey * ey);
}

inline bool inPose(const Pose &p, float u, float v) {
    for (const auto &pr : p.parts) {
        if (segDist(u, v, pr) <= pr.r * 1.15f) {
            return true;
        }
    }
    return false;
}

class Story : public ShowStory {
public:
    // played: indexes into POSES of the poses the game used, in order.
    explicit Story(std::vector<int> played) : poses(std::move(played)) {}

    void begin(ArcadeShow &s) override {
        mask.assign(s.W * s.H, 0);
        if (poses.empty()) {
            poses = {0, 1, 2, 3, 4};
        }
        // Small enough that a pose or two is on the wall at once.
        tall = std::max(8.0f, std::min(s.H * 0.56f, s.W * 0.42f));
        spacing = tall * KINECT_STRETCH * 0.95f;
        // The finale's STAR as tall as fits the height and, stretched, the width.
        bigTall = std::min(s.H - 1.0f, (s.W - 2) / (0.9f * KINECT_STRETCH));
        burst = false;
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const float W = (float)s.W, H = (float)s.H;
        const float sec = (float)(ms / 1000.0);
        const float congaEnd = (float)(len / 1000.0) * 0.62f;

        // A dim floor along the bottom row, for them to dance on.
        for (int x = 0; x < s.W; ++x) {
            c.add(x, s.H - 1, OUTLINE, 0.08f * k);
        }

        if (sec < congaEnd) {
            // The conga: four beats to a bar, a step on each of the first three
            // and a kick (a hop and a tilt) on the fourth. The line moves only
            // on the steps, so between them the outlines stand still to be read.
            const float beat = 0.42f;
            const float lineLen = spacing * (poses.size() - 1);
            const float travel = W + lineLen + spacing * 0.7f;   // from off the right until the last is almost off the left
            const int steps = std::max(1, (int)(congaEnd / beat * 0.75f));
            const float b = sec / beat;
            const int bar = (int)(b / 4), inBar = (int)b % 4;
            const float within = b - std::floor(b);
            float done = bar * 3 + std::min(inBar, 3);
            if (inBar < 3) {
                done += within * within * (3 - 2 * within);
            }
            const float headX = W + spacing * 0.5f - travel * std::min(1.0f, done / steps);
            const bool kick = inBar == 3;
            const float hop = kick ? std::sin(within * 3.1416f) * std::max(1.0f, H * 0.08f) : 0.0f;
            for (size_t i = 0; i < poses.size(); ++i) {
                const float x = headX + spacing * i;
                if (x < -spacing || x > W + spacing) {
                    continue;
                }
                // Alternate ones lean the other way on the kick.
                const float tilt = kick ? std::sin(within * 3.1416f) * 0.22f * ((i & 1) ? -1 : 1) : 0.0f;
                figure(s, c, POSES[poses[i] % POSES.size()], x, H - 1 - hop, tall, tilt, 1.0f,
                       0.6f + 0.25f * std::sin(sec * 6.0f), INSIDE, 0.35f, OUTLINE, k);
            }
        } else {
            // The finale: the STAR outline pulses in, fills from its feet,
            // then flashes and turns gold.
            const float f = sec - congaEnd;
            const float appear = std::clamp(f / 0.4f, 0.0f, 1.0f);
            const float fill = std::clamp((f - 0.6f) / 1.0f, 0.0f, 1.0f);
            const bool scored = f >= 1.7f;
            const float pulse = 0.75f + 0.25f * std::sin(sec * 6.0f);
            const RGB edge = scored ? CLOCK : OUTLINE;
            figure(s, c, POSES[0], W / 2.0f, H - 1, bigTall, 0, fill, (scored ? 0.95f : 0.6f * pulse) * appear, INSIDE,
                   scored ? 0.55f : 0.45f, edge, k);
            if (scored && !burst) {
                burst = true;
                // Head, hands and feet of the STAR, in person units.
                const float pts[5][2] = {{0, .09f}, {-.38f, .08f}, {.38f, .08f}, {-.26f, 1}, {.26f, 1}};
                const float wide = bigTall * KINECT_STRETCH;
                for (int p = 0; p < 5; ++p) {
                    s.burst(W / 2.0f + pts[p][0] * wide, H - 1 - bigTall + pts[p][1] * bigTall,
                            p == 0 ? CLOCK : INSIDE, 16, p == 0);
                }
            }
            // A scored round flickers brighter, as in the game.
            if (scored && f < 2.5f && std::fmod(f, 0.4f) < 0.2f) {
                c.scale(1.3f);
            }
        }
        s.drawFireworks(c, k);
    }

    // Behind the credits: the gold STAR, faint, still pulsing.
    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float, float k) override {
        const float pulse = 0.75f + 0.25f * std::sin((float)(s.t / 1000.0) * 4.0f);
        figure(s, c, POSES[0], s.W / 2.0f, s.H - 1, bigTall, 0, 1.0f, 0.5f * pulse, INSIDE, 0.15f, CLOCK, k);
    }

private:
    // Draws a pose standing with its feet at (cx, feetY), `tall` LEDs high and
    // tilted by `tilt` radians about its feet: the outline in `edgeCol`, and
    // the inside filled with `fillCol` from the feet up to `fill` of its
    // height (1 = all of it), as the game shows a player who fits.
    void figure(ArcadeShow &s, ArcadeCanvas::Canvas &c, const Pose &p, float cx, float feetY, float tallPx, float tilt,
                float fill, float edgeK, const RGB &fillCol, float fillK, const RGB &edgeCol, float k) {
        const int W = s.W, H = s.H;
        const float wide = tallPx * KINECT_STRETCH;
        const int x0 = std::max(0, (int)(cx - wide * 0.7f)), x1 = std::min(W - 1, (int)(cx + wide * 0.7f) + 1);
        const int y0 = std::max(0, (int)(feetY - tallPx * 1.15f)), y1 = std::min(H - 1, (int)feetY + 1);
        if (x0 > x1 || y0 > y1) {
            return;
        }
        const float cs = std::cos(tilt), sn = std::sin(tilt);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f - cx, py = y + 0.5f - feetY;
                const float rx = px * cs + py * sn, ry = -px * sn + py * cs;
                mask[y * W + x] = inPose(p, rx / wide, 1.0f + ry / tallPx) ? 1 : 0;
            }
        }
        const float fillTop = feetY - tallPx * fill;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (!mask[y * W + x]) {
                    continue;
                }
                bool edge = false;
                static const int ex[4] = {-1, 1, 0, 0}, ey[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4 && !edge; ++d) {
                    const int nx = x + ex[d], ny = y + ey[d];
                    edge = nx < x0 || ny < y0 || nx > x1 || ny > y1 || !mask[ny * W + nx];
                }
                if (edge) {
                    c.add(x, y, edgeCol, edgeK * k);
                } else if (y + 0.5f >= fillTop) {
                    c.add(x, y, fillCol, fillK * k);
                } else {
                    c.add(x, y, edgeCol, 0.06f * k);   // the game's faint inside
                }
            }
        }
        // Clear what was used, so the next figure starts clean.
        for (int y = y0; y <= y1; ++y) {
            std::fill(mask.begin() + y * W + x0, mask.begin() + y * W + x1 + 1, 0);
        }
    }

    std::vector<int> poses;
    std::vector<uint8_t> mask;
    float tall = 14, spacing = 24, bigTall = 24;
    bool burst = false;
};

// total: the five rounds' total; previousBest the best before it.
inline ShowConfig config(int total, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"TOTAL", total, false, CLOCK});
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"SHAPE FIT", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"YOU", INSIDE}, {"AND", HEAD},
                {"FIVE SHAPES", OUTLINE}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "WELL DONE";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(342, true, 298);
    story = std::make_unique<Story>(std::vector<int>{2, 5, 0, 3, 4});
}

}  // namespace ShapeFitShow
