#pragma once
// Flap's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the pipes
// passed (and the best, when this wasn't it), then a victory lap on a snowy
// night - the bird, in a Santa hat, flaps through a row of pipes and each one
// it gets through turns into a candy cane; then the pipes slow to a stop and
// it zooms ahead through the last of them and off the edge in a shower of
// sparkles. The candy canes stand behind the credits and GAME OVER.
//
// After a two-player game the headline names the winner, both scores count
// up in their birds' colours, and the blue bird flies the lap just behind the
// yellow one.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace FlapShow {

class Story : public ShowStory {
public:
    using RGB = ArcadeCanvas::RGB;

    explicit Story(bool twoBirds = false) : two(twoBirds) {}

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        birdX = W / 4.0f;
        lead = Flyer{H / 2.0f};
        wing = Flyer{H / 2.0f + 2.0f};
        scroll = 0;
        gone = false;
        pipes.clear();
        // The gaps as the game makes them, anywhere up and down the middle,
        // but each within a few rows of the last so the lap is a smooth one.
        gap = std::max(5, std::min(H / 3, 8));
        spacing = std::max(14, W / 3);
        const float lo = gap / 2.0f + 2.0f, hi = H - gap / 2.0f - 2.0f;
        float gapMid = H / 2.0f;
        for (float x = W * 0.75f; x < W * 6.0f; x += spacing) {
            const float want = lo + s.rnd() * std::max(0.0f, hi - lo);
            gapMid = std::clamp(want, gapMid - 5.0f, gapMid + 5.0f);
            pipes.push_back({x, (int)std::lround(gapMid - gap / 2.0f), false, 0.0});
        }
        s.moonR = std::min(s.moonR, 3.0f);   // a small moon, so it doesn't crowd the pipes
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        s.drawNight(c, dt, k);
        drawPipes(s, c, s.t + 1e6, k);   // long since passed: all candy canes
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        const float u = (float)(ms / len);
        s.drawNight(c, dt, k);

        // The pipes go by at a lively game speed, then slow to a stop from
        // 65% while the bird speeds on ahead through the last of them.
        const float v = std::max(12.0f, W / 4.0f);
        const float slow = std::clamp((u - 0.65f) / 0.15f, 0.0f, 1.0f);
        scroll += v * (1.0f - slow) * dt;
        if (u > 0.68f) {
            birdX += (W * 0.9f) * std::clamp((u - 0.68f) / 0.12f, 0.0f, 1.0f) * dt;
        }

        fly(lead, birdX, dt, H);
        if (two) {
            fly(wing, birdX - TRAIL, dt, H);
        }

        // Through a gap: the pipe turns to a candy cane, with a puff of red and white.
        for (auto &p : pipes) {
            if (!p.passed && p.x - scroll + 1 < birdX) {
                p.passed = true;
                p.passMs = ms;
                const float gy = p.gapTop + gap / 2.0f;
                s.burst(p.x - scroll + 0.5f, gy, {1, 0.1f, 0.1f}, 8, false);
                s.burst(p.x - scroll + 0.5f, gy, {1, 1, 1}, 8, false);
            }
        }
        if (!gone && birdX - (two ? TRAIL : 0) >= W + 1) {
            gone = true;
            s.burst(W * 0.75f, H * 0.3f, {1, 0.85f, 0.3f}, 40, true);
        }

        drawPipes(s, c, ms, k);
        if (!gone) {
            // A trail of gold sparkles once it's off on its own.
            if (birdX > W / 4.0f + 1 && s.rnd() < dt * 30.0f) {
                s.sparks.push_back({birdX - 1, lead.y + s.rnd() - 0.5f, -4.0f, s.rnd() * 2 - 1, 0, 0.4f + s.rnd() * 0.4f,
                                    {1, 0.8f, 0.3f}, 30, 2.0f});
            }
            drawBird(c, (int)std::lround(birdX), (int)lead.y, lead.flapT, YELLOW, YELLOW_WING, k);
            if (two) {
                drawBird(c, (int)std::lround(birdX - TRAIL), (int)wing.y, wing.flapT, BLUE, BLUE_WING, k);
            }
        }
        s.drawFireworks(c, k);
    }

private:
    struct Pipe {
        float x;      // in the world; on screen at x - scroll
        int gapTop;
        bool passed;
        double passMs;
    };

    // One bird's flying: the game's gravity and flap, flapping whenever it
    // drops below the middle of the next gap ahead of it.
    struct Flyer {
        float y = 0, vy = 0, flapT = 0, sinceFlap = 0;
    };
    void fly(Flyer &f, float x, float dt, int H) {
        float target = H / 2.0f;
        for (auto &p : pipes) {
            if (p.x - scroll + 2 >= x - 1) {
                target = p.gapTop + gap / 2.0f;
                break;
            }
        }
        f.sinceFlap += dt;
        f.flapT = std::max(0.0f, f.flapT - dt);
        if (f.y > target && f.vy > -3.0f && f.sinceFlap > 0.12f) {
            f.vy = -11.0f;
            f.flapT = 0.15f;
            f.sinceFlap = 0;
        }
        f.vy = std::min(18.0f, f.vy + 42.0f * dt);
        f.y = std::clamp(f.y + f.vy * dt, 1.0f, H - 1.0f);
    }

    static constexpr float TRAIL = 4.0f;   // how far the blue bird flies behind
    static constexpr RGB YELLOW = {1.0f, 0.85f, 0.1f}, YELLOW_WING = {1.0f, 0.45f, 0.05f};
    static constexpr RGB BLUE = {0.3f, 0.75f, 1.0f}, BLUE_WING = {0.1f, 0.35f, 1.0f};

    // The game's bird - its body, its wing up on a flap, a red beak -
    // wearing a Santa hat.
    void drawBird(ArcadeCanvas::Canvas &c, int bx, int by, float flapT, const RGB &body, const RGB &wingCol, float k) {
        c.set(bx, by, body, k);
        c.set(bx - 1, by + (flapT > 0 ? -1 : 0), wingCol, 0.8f * k);
        c.set(bx + 1, by, {1.0f, 0.3f, 0.1f}, 0.6f * k);
        c.set(bx, by - 1, {1.0f, 0.05f, 0.05f}, k);
        c.set(bx - 1, by - 2, {1, 1, 1}, 0.8f * k);   // the bobble
    }

    // Green pipes, two wide with brighter lips, as in the game; a passed one
    // becomes a red and white candy cane, the stripes spreading out from its
    // gap, and the bottom one grows a crook.
    void drawPipes(ArcadeShow &s, ArcadeCanvas::Canvas &c, double ms, float k) {
        const int H = s.H;
        const RGB GREEN = {0.1f, 0.85f, 0.2f}, RED = {1, 0.08f, 0.06f}, WHITE = {0.9f, 0.9f, 0.9f};
        auto cane = [&](int x, int yy) { return ((x + yy) / 2 & 1) ? RED : WHITE; };
        for (const auto &p : pipes) {
            const int px = (int)std::floor(p.x - scroll);
            if (px < -6 || px > s.W) {
                continue;
            }
            const float spread = p.passed ? (float)((ms - p.passMs) / 450.0) * H : -1;
            const float mid = p.gapTop + gap / 2.0f;
            for (int yy = 0; yy < H; ++yy) {
                if (yy >= p.gapTop && yy < p.gapTop + gap) {
                    continue;
                }
                const bool lip = yy == p.gapTop - 1 || yy == p.gapTop + gap;
                for (int dx = 0; dx < 2; ++dx) {
                    if (std::fabs(yy - mid) < spread) {
                        c.set(px + dx, yy, cane(px + dx, yy), 0.7f * k);
                    } else {
                        c.set(px + dx, yy, GREEN, (lip ? 0.8f : (dx ? 0.25f : 0.4f)) * k);
                    }
                }
            }
            // The crook, once the stripes are all the way down.
            if (spread > H) {
                // Two LEDs wide down both sides, like the shaft.
                static const char *CROOK[3] = {".####.", "##..##", "##..##"};
                const int top = p.gapTop + gap - 3;
                for (int r = 0; r < 3; ++r) {
                    for (int x = 0; x < 6; ++x) {
                        if (CROOK[r][x] == '#') {
                            c.set(px + x, top + r, cane(px + x, top + r), 0.7f * k);
                        }
                    }
                }
            }
        }
    }

    std::vector<Pipe> pipes;
    int gap = 8, spacing = 20;
    float birdX = 15, scroll = 0;
    Flyer lead, wing;    // the yellow bird, and the blue one behind it
    bool two = false;
    bool gone = false;   // flown off the edge
};

// pipes: pipes passed; previousBest the best before this game.
inline ShowConfig config(int pipes, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"PIPES", pipes, false, GREEN});
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"FLAP", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE BIRD", GOLD}, {"AND", HEAD},
                {std::to_string(pipes) + " PIPES", GREEN}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// After two players: yellow and blue are each bird's pipes. The winner gets
// the headline; the better score is the one that counts as a best.
inline ShowConfig config2(int yellow, int blue, bool newBest, int previousBest) {
    using namespace ShowColours;
    const ArcadeCanvas::RGB YELLOW = {1.0f, 0.85f, 0.1f}, BLUE = {0.3f, 0.75f, 1.0f};
    ShowConfig cfg;
    if (yellow == blue) {
        cfg.headline = {"IT'S A", "TIE!"};
    } else {
        cfg.headline = {yellow > blue ? "YELLOW" : "BLUE", "WINS!"};
    }
    cfg.stats.push_back({"YELLOW", yellow, false, YELLOW});
    cfg.stats.push_back({"BLUE", blue, false, BLUE});
    if (newBest) {
        cfg.stats.push_back({"NEW BEST", std::max(yellow, blue), false, GOLD});
    } else if (previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"FLAP", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE YELLOW BIRD", YELLOW}, {"THE BLUE BIRD", BLUE},
                {"AND", HEAD}, {std::to_string(std::max(yellow, blue)) + " PIPES", GREEN}, {"", WHITE},
                {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config2(23, 17, true, 19);
    story = std::make_unique<Story>(true);
}

}  // namespace FlapShow
