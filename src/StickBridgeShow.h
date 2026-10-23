#pragma once
// Stick Bridge's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the
// score, the perfect landings (and the best, when this wasn't it), then the
// elf's trip to the North Pole on a snowy night - candy cane after candy cane
// grows, swings down and lands dead on the bow of the next gift box with a
// sparkle, the elf trots over and the view slides along, until the last
// bridge reaches a snowy hill with the striped North Pole on it. The elf
// hops for joy under it as the star on top lights and the fireworks go up.
// The hill and the pole stand behind the credits and GAME OVER.
//
// After a two-player game the headline names the winner, both scores count
// up in their elves' colours, and the blue elf follows the green one over.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace StickBridgeShow {

class Story : public ShowStory {
public:
    using RGB = ArcadeCanvas::RGB;

    explicit Story(bool twoElves = false) : two(twoElves) {}

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        // The game's own sizes: boxes a quarter of the height, the elf's
        // edge where the game keeps it.
        boxH = std::clamp(H / 4, 3, 8);
        topY = H - boxH;
        const int tall = topY + 1;
        edgeX = std::max(3, (W - tall) / 2);
        const int reach = std::max(6, std::min(tall, W - 1 - edgeX));
        boxes.clear();
        boxes.push_back({edgeX - 7, 8, 0});
        // Gift boxes with gaps an easy reach apart, then the hill.
        for (int i = 0; i < CROSSINGS - 1; ++i) {
            const Box &p = boxes.back();
            const int w = 4 + (int)(s.rnd() * 3);
            const int gap = std::clamp(5 + (int)(s.rnd() * 6), 3, reach - w);
            boxes.push_back({p.x + p.w + gap, w, (i + 1) % 5});
        }
        const Box &p = boxes.back();
        const int hillW = std::max(12, W / 2);
        const int gap = std::clamp(6 + (int)(s.rnd() * 4), 3, reach - 4);
        boxes.push_back({p.x + p.w + gap, hillW, -1});
        // The pole stands a few LEDs onto the hill; the last bridge lands
        // just short of it and the elf stops beside it.
        poleX = boxes.back().x + 4;
        cam = 0;
        landedAt.assign(boxes.size(), -1.0);
        s.moonX = W * 0.18f;   // out of the way of the pole and the score corner
        s.moonR = std::min(s.moonR, 3.0f);
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        s.drawNight(c, dt, k);
        // Where it ended: the last bridges, the hill and the lit pole.
        const int last = (int)boxes.size() - 1;
        const float camEnd = (float)(poleX - (s.W * 2) / 3);
        drawWorld(s, c, camEnd, last, k, true);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        s.drawNight(c, dt, k);

        // Each crossing takes the same time - the stick grows, falls, the elf
        // walks, the view slides - and the hop and the fireworks the rest.
        const double each = len * 0.74 / CROSSINGS;
        const int k0 = std::min(CROSSINGS, (int)(ms / each));
        const float u = (float)((ms - k0 * each) / each);
        const int last = (int)boxes.size() - 1;

        // Which box the elf is on, and what it's doing on this crossing.
        int cur = std::min(k0, last);
        float stickLen = 0, angle = 0, elfX = (float)edgeOf(cur);
        if (k0 < CROSSINGS) {
            const int land = landOf(cur);
            const int full = land - edgeOf(cur);
            if (u < 0.32f) {
                stickLen = std::max(1.0f, full * std::min(1.0f, u / 0.28f));
            } else if (u < 0.45f) {
                stickLen = (float)full;
                const float f = (u - 0.32f) / 0.13f;
                angle = 90.0f * f * f;
            } else {
                stickLen = (float)full;
                angle = 90.0f;
                if (landedAt[cur + 1] < 0) {
                    landedAt[cur + 1] = ms;
                    sparkle(s, land - cam + 0.5f, topY + 0.5f);
                }
                const int to = cur + 1 == last ? poleX - 1 : edgeOf(cur + 1);
                const float f = std::clamp((u - 0.5f) / 0.32f, 0.0f, 1.0f);
                elfX = edgeOf(cur) + (to - edgeOf(cur)) * f;
            }
            // The view slides along once the elf is over, as in the game.
            const float camFrom = (float)(edgeOf(cur) - edgeX);
            const float camTo = cur + 1 == last ? (float)(poleX - (W * 2) / 3) : (float)(edgeOf(cur + 1) - edgeX);
            const float f = std::clamp((u - 0.8f) / 0.2f, 0.0f, 1.0f);
            cam = camFrom + (camTo - camFrom) * (1.0f - (1.0f - f) * (1.0f - f));
        } else {
            cur = last;
            elfX = (float)(poleX - 1);
            cam = (float)(poleX - (W * 2) / 3);
        }

        // Arrived: the star lights, the elf hops, and fireworks.
        const bool home = k0 >= CROSSINGS;
        const double homeMs = ms - CROSSINGS * each;
        float hop = 0;
        if (home) {
            hop = std::fabs(std::sin((float)homeMs / 1000.0f * 7.0f)) * 2.2f;
            if (homeMs > nextBurst) {
                nextBurst = homeMs + 450.0;
                s.burst(W * (0.2f + 0.6f * s.rnd()), H * (0.15f + 0.25f * s.rnd()), ArcadeShow::hue(s.rnd()), 22,
                        s.rnd() < 0.3f);
            }
        }

        drawWorld(s, c, cam, home ? last : cur, k, home);
        // The bridge being built, from the corner of the box.
        if (!home && stickLen > 0) {
            const float a = angle * 3.14159265f / 180.0f, px = edgeOf(cur) + 1.5f - cam, py = topY + 0.5f;
            for (float t = 0; t <= stickLen - 1 + 0.01f; t += 0.5f) {
                c.set((int)std::floor(px + std::sin(a) * t), (int)std::floor(py - std::cos(a) * t), cane((int)t), 0.9f * k);
            }
        }
        const bool walking = !home && u >= 0.5f && u < 0.82f;
        drawElf(c, elfX - cam, topY - 1 - (int)std::lround(hop), GREEN, walking, k);
        if (two) {
            // Following on behind, a step or two back - or still on the last
            // box, catching up.
            const float bx = std::max((float)(edgeOf(cur) - 3), elfX - 3.0f);
            const float hop2 = home ? std::fabs(std::sin((float)homeMs / 1000.0f * 7.0f + 1.2f)) * 2.2f : 0;
            drawElf(c, bx - cam, topY - 1 - (int)std::lround(hop2), BLUE, walking, k);
        }
        s.drawFireworks(c, k);
    }

private:
    struct Box {
        int x, w;
        int colour;   // -1 the hill
    };
    static constexpr int CROSSINGS = 4;   // three gift boxes and the hill
    static constexpr RGB GREEN = {0.1f, 0.9f, 0.2f}, BLUE = {0.2f, 0.5f, 1.0f};

    int edgeOf(int i) const { return boxes[i].x + boxes[i].w - 1; }
    // Where the bridge from box i lands: dead on the next bow, or on the hill
    // two short of the pole.
    int landOf(int i) const {
        const Box &n = boxes[i + 1];
        return n.colour < 0 ? poleX - 2 : n.x + (n.w - 1) / 2;
    }
    static RGB cane(int k) { return (k / 2) % 2 ? RGB{0.75f, 0.75f, 0.75f} : RGB{1.0f, 0.08f, 0.06f}; }

    void sparkle(ArcadeShow &s, float x, float y) {
        for (int k = 0; k < 16; ++k) {
            const float a = 6.2832f * (k + s.rnd() * 0.5f) / 16, v = 5.0f + s.rnd() * 7.0f;
            s.sparks.push_back({x, y, std::cos(a) * v, std::sin(a) * v - 3.0f, 0, 0.5f + 0.5f * s.rnd(),
                                k % 2 ? RGB{1.0f, 0.8f, 0.2f} : RGB{1, 1, 1}, 30, 12.0f});
        }
    }

    // The boxes, the bridges already crossed (up to box upTo), the hill and
    // the North Pole, seen from cam.
    void drawWorld(ArcadeShow &s, ArcadeCanvas::Canvas &c, float camX, int upTo, float k, bool lit) {
        static const RGB PAPER[] = {{0.1f, 0.75f, 0.2f}, {0.15f, 0.35f, 1.0f}, {0.6f, 0.2f, 0.9f}, {1.0f, 0.65f, 0.05f},
                                    {0.05f, 0.75f, 0.7f}};
        const int ci = (int)std::lround(camX), H = s.H;
        for (size_t i = 0; i < boxes.size(); ++i) {
            const Box &b = boxes[i];
            if (b.colour < 0) {
                // The hill: snow, a little brighter along the top.
                for (int x = b.x; x < b.x + b.w + s.W; ++x) {
                    for (int y = topY; y < H; ++y) {
                        c.set(x - ci, y, {0.75f, 0.8f, 1.0f}, (y == topY ? 0.45f : 0.22f) * k);
                    }
                }
                continue;
            }
            const int mid = b.x + (b.w - 1) / 2, band = topY + boxH / 2;
            const RGB ribbon = b.colour == 3 ? RGB{0.9f, 0.9f, 0.9f} : RGB{1.0f, 0.8f, 0.25f};
            for (int x = b.x; x < b.x + b.w; ++x) {
                for (int y = topY; y < H; ++y) {
                    if (x == mid && y == topY) {
                        c.set(x - ci, y, {1.0f, 0.03f, 0.03f}, k);
                    } else if (x == mid || y == band) {
                        c.set(x - ci, y, ribbon, (y == topY ? 0.7f : 0.45f) * k);
                    } else {
                        c.set(x - ci, y, PAPER[b.colour], (y == topY ? 0.6f : 0.3f) * k);
                    }
                }
            }
        }
        // Bridges behind the elf: each from a box's corner to the next bow.
        for (int i = 0; i < upTo; ++i) {
            const int land = landOf(i);
            for (int x = edgeOf(i) + 1; x <= land; ++x) {
                c.set(x - ci, topY, cane(x - edgeOf(i) - 1), 0.75f * k);
            }
        }
        // The North Pole: red and white stripes, two wide, up most of the
        // sky, with a star on top that lights when the elf gets there.
        const int top = std::max(4, topY / 4);
        for (int y = top; y < topY; ++y) {
            for (int dx = 0; dx < 2; ++dx) {
                c.set(poleX + dx - ci, y, ((y + dx) / 2) % 2 ? RGB{0.8f, 0.8f, 0.8f} : RGB{1.0f, 0.06f, 0.05f}, 0.75f * k);
            }
        }
        const float glow = lit ? 0.8f + 0.2f * std::sin((float)s.t / 150.0f) : 0.25f;
        const RGB star = {1.0f, 0.85f, 0.2f};
        const int sx = poleX - ci, sy = top - 2;
        c.set(sx, sy, star, glow * k);
        c.set(sx + 1, sy, star, glow * k);
        c.set(sx - 1, sy, star, 0.6f * glow * k);
        c.set(sx + 2, sy, star, 0.6f * glow * k);
        c.set(sx, sy - 1, star, 0.6f * glow * k);
        c.set(sx + 1, sy - 1, star, 0.6f * glow * k);
        c.set(sx, sy + 1, star, 0.6f * glow * k);
        c.set(sx + 1, sy + 1, star, 0.6f * glow * k);
    }

    // The game's elf: Santa hat and bobble, face, tunic, back foot on a step.
    void drawElf(ArcadeCanvas::Canvas &c, float x, int feetY, const RGB &tunic, bool walking, float k) {
        const int fx = (int)std::floor(x + 0.5f);
        c.set(fx, feetY, tunic, 0.9f * k);
        c.set(fx, feetY - 1, {0.85f, 0.5f, 0.32f}, 0.8f * k);
        c.set(fx, feetY - 2, {1.0f, 0.08f, 0.05f}, k);
        c.set(fx - 1, feetY - 2, {0.8f, 0.8f, 0.8f}, 0.7f * k);
        if (walking && (fx & 1)) {
            c.set(fx - 1, feetY, tunic, 0.5f * k);
        }
    }

    std::vector<Box> boxes;
    std::vector<double> landedAt;   // when each box's bridge landed, so it sparkles once
    int boxH = 6, topY = 19, edgeX = 20, poleX = 0;
    float cam = 0;
    double nextBurst = 0;
    bool two = false;
};

// score and perfects: the final score and the perfect landings in it;
// previousBest the best before this game.
inline ShowConfig config(int score, int perfects, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    if (perfects > 0) {
        cfg.stats.push_back({"PERFECT", perfects, false, RED});
    }
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GREEN});
    }
    cfg.roll = {{"STICK BRIDGE", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE ELF", GREEN}, {"AND", HEAD},
                {std::to_string(score) + " CANDY CANES", RED}, {"", WHITE}, {"NEXT STOP", HEAD},
                {"THE NORTH POLE", WHITE}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// After two players: left and right are each side's score. The winner gets
// the headline; the better score is the one that counts as a best.
inline ShowConfig config2(int left, int right, bool newBest, int previousBest) {
    using namespace ShowColours;
    const ArcadeCanvas::RGB LEFT = {0.1f, 0.9f, 0.2f}, RIGHT = {0.2f, 0.5f, 1.0f};
    ShowConfig cfg;
    if (left == right) {
        cfg.headline = {"IT'S A", "TIE!"};
    } else {
        cfg.headline = {left > right ? "LEFT" : "RIGHT", "WINS!"};
    }
    cfg.stats.push_back({"LEFT", left, false, LEFT});
    cfg.stats.push_back({"RIGHT", right, false, RIGHT});
    if (newBest) {
        cfg.stats.push_back({"NEW BEST", std::max(left, right), false, GOLD});
    } else if (previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"STICK BRIDGE", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE GREEN ELF", LEFT}, {"THE BLUE ELF", RIGHT},
                {"AND", HEAD}, {std::to_string(left + right) + " CANDY CANES", RED}, {"", WHITE}, {"NEXT STOP", HEAD},
                {"THE NORTH POLE", WHITE}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(14, 3, true, 9);
    story = std::make_unique<Story>();
}

}  // namespace StickBridgeShow
