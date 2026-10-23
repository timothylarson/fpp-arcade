#pragma once
// Maze Chase's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the
// score and the level reached, then the intermission from the arcade original
// - the hero runs left down a corridor eating dots with the red ghost (in a
// Santa hat) on its tail, swallows the power pellet and runs off; the ghost
// turns blue and flees back the other way, and the hero comes back huge,
// gobbles it, and wears its hat off while the eyes scurry home - the credits
// over a parade of the cast, and GAME OVER.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace MazeChaseShow {

using RGB = ArcadeCanvas::RGB;

// The game's own colours (as in FPPMazeChase.cpp).
const RGB WALL = {0.1f, 0.2f, 1.0f};
const RGB DOT = {1.0f, 0.75f, 0.6f};
const RGB PLAYER = {1.0f, 0.85f, 0.0f};
const RGB SCARED = {0.15f, 0.25f, 1.0f};
const RGB EYES = {0.7f, 0.8f, 1.0f};
const RGB GHOSTS[4] = {{1.0f, 0.0f, 0.0f}, {1.0f, 0.4f, 0.75f}, {0.0f, 1.0f, 1.0f}, {1.0f, 0.55f, 0.0f}};
const RGB HAT_RED = {1.0f, 0.05f, 0.05f};
const RGB PUPIL = {0.1f, 0.2f, 1.0f};
const RGB FACE = {1.0f, 0.75f, 0.6f};    // a scared ghost's eyes and wobbly mouth

// The cast, drawn bigger than in the game (where they are single LEDs) so
// they read as characters: n is the sprite's size, odd, and (cx, cy) its
// centre. face is +1 looking right, -1 left.

// The hero: a disc with a wedge of a mouth open by mouth radians.
inline void drawHero(ArcadeCanvas::Canvas &c, float cx, int cy, int n, int face, float mouth, const RGB &col, float k) {
    const float r = n / 2.0f, half = (n - 1) / 2.0f;
    const int x0 = (int)std::lround(cx - half), y0 = cy - (n - 1) / 2;
    const float t = std::tan(std::clamp(mouth, 0.0f, 1.2f));
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float dx = x - half, dy = y - half;
            if (dx * dx + dy * dy > r * r) {
                continue;
            }
            const float fx = dx * face;
            if (mouth > 0.05f && fx > 0 && std::fabs(dy) <= fx * t) {
                continue;
            }
            c.set(x0 + x, y0 + y, col, k);
        }
    }
}

// A Santa hat sitting on top of a ghost whose top row is y, flopping back
// from the way it faces with the bobble hanging off the tip.
inline void drawHat(ArcadeCanvas::Canvas &c, float cx, int y, int n, int face, float k) {
    const int x = (int)std::lround(cx);
    const int b = -face;   // back
    auto px = [&](int dx, int dy, const RGB &col, float kk) { c.set(x + dx * b, y + dy, col, kk * k); };
    if (n >= 7) {
        for (int dx = -2; dx <= 2; ++dx) {
            px(dx, -1, ArcadeCanvas::WHITE, 0.9f);   // the fur brim
        }
        for (int dx = -2; dx <= 1; ++dx) {
            px(dx, -2, HAT_RED, 1.0f);
        }
        for (int dx = -1; dx <= 1; ++dx) {
            px(dx, -3, HAT_RED, 1.0f);
        }
        px(1, -4, HAT_RED, 1.0f);
        px(2, -4, HAT_RED, 1.0f);
        px(3, -3, ArcadeCanvas::WHITE, 1.0f);    // the bobble
    } else {
        for (int dx = -1; dx <= 1; ++dx) {
            px(dx, -1, ArcadeCanvas::WHITE, 0.9f);
        }
        px(-1, -2, HAT_RED, 1.0f);
        px(0, -2, HAT_RED, 1.0f);
        px(1, -3, HAT_RED, 1.0f);
        px(2, -2, ArcadeCanvas::WHITE, 1.0f);
    }
}

// A ghost's eyes on their own (eaten, heading home) or on its body: whites
// with the pupils on the side it's looking.
inline void drawEyes(ArcadeCanvas::Canvas &c, int x0, int y0, int n, int face, float k) {
    if (n >= 7) {
        for (int e : {1, 4}) {
            for (int dy = 2; dy <= 3; ++dy) {
                for (int dx = 0; dx <= 1; ++dx) {
                    c.set(x0 + e + dx, y0 + dy, ArcadeCanvas::WHITE, 0.9f * k);
                }
            }
            c.set(x0 + e + (face > 0 ? 1 : 0), y0 + 3, PUPIL, k);
        }
    } else {
        for (int e : {1, 3}) {
            c.set(x0 + e, y0 + 1, ArcadeCanvas::WHITE, 0.9f * k);
            c.set(x0 + e + (face > 0 ? 1 : -1), y0 + 1, PUPIL, k);
        }
    }
}

// A ghost: a dome on a body with a wavy hem that ripples as it goes. Scared,
// it's blue (or white when flash) with a little face instead of eyes.
inline void drawGhost(ArcadeCanvas::Canvas &c, float cx, int cy, int n, int face, const RGB &col, bool scared,
                      bool flash, double ms, bool hat, float k) {
    const float r = n / 2.0f, half = (n - 1) / 2.0f;
    const int x0 = (int)std::lround(cx - half), y0 = cy - (n - 1) / 2;
    const int phase = (int)(ms / 140.0) & 1;
    const RGB body = scared ? (flash ? ArcadeCanvas::WHITE : SCARED) : col;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float dx = x - half, dy = y - half;
            if (dy < 0 && dx * dx + dy * dy > r * r) {
                continue;   // round on top
            }
            if (y == n - 1 && (n >= 7 ? (x + phase * 2) % 3 == 1 : (x + phase) % 2 == 1)) {
                continue;   // the hem
            }
            c.set(x0 + x, y0 + y, body, k);
        }
    }
    if (!scared) {
        drawEyes(c, x0, y0, n, face, k);
    } else {
        const RGB f = flash ? RGB{1, 0.1f, 0.1f} : FACE;
        if (n >= 7) {
            c.set(x0 + 2, y0 + 2, f, k);
            c.set(x0 + 4, y0 + 2, f, k);
            for (int x = 1; x <= 5; ++x) {
                c.set(x0 + x, y0 + 4 + (x & 1), f, 0.8f * k);
            }
        } else {
            c.set(x0 + 1, y0 + 1, f, k);
            c.set(x0 + 3, y0 + 1, f, k);
            c.set(x0 + 1, y0 + 3, f, 0.8f * k);
            c.set(x0 + 2, y0 + 2, f, 0.8f * k);
            c.set(x0 + 3, y0 + 3, f, 0.8f * k);
        }
    }
    if (hat) {
        drawHat(c, cx, y0, n, face, k);
    }
}

class Story : public ShowStory {
public:
    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        n = H >= 19 ? 7 : 5;
        big = std::max(n + 4, std::min((int)(H * 0.62f), (int)(W * 0.55f)) | 1);
        cy = H / 2;
        wallTop = std::max(0, cy - big / 2 - 2);
        wallBottom = std::min(H - 1, cy + big / 2 + 2);
        pelletX = 3;
        gap = n + (W >= 40 ? 3 : 2);
        catchX = W * 0.72f;
        paradeMs = 0;
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W;
        const float u = (float)(ms / len);
        const float sec = (float)len / 1000.0f;

        drawCorridor(c, W, k);

        // Act one: the hero comes in from the right and runs for the pellet
        // on the far left, the ghost a few steps behind.
        const float heroV = (W + n / 2.0f + 1 - pelletX) / ((EAT - IN) * sec);   // LEDs a second
        const float hx = W + n / 2.0f + 1 - heroV * (u - IN) * sec;
        // The dots go as the hero passes over them; the pellet when it's eaten.
        for (int x = pelletX + 4; x < W; x += 3) {
            if (u < IN || x < hx) {
                c.set(x, cy, DOT, 0.5f * k);
            }
        }
        if (u < EAT) {
            const float on = std::fmod(ms, 500.0) < 300.0 ? 1.0f : 0.3f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    c.set(pelletX + dx, cy + dy, DOT, (dx && dy ? 0.35f : 1.0f) * on * k);
                }
            }
        }
        if (u >= EAT && !eaten) {
            eaten = true;
            s.burst((float)pelletX, (float)cy, DOT, 24, false);
        }
        const float mouth = 0.75f * std::fabs(std::sin((float)ms * 0.013f));
        if (u >= IN && hx > -n) {
            drawHero(c, hx, cy, n, -1, mouth, PLAYER, k);
        }

        // The ghost: chasing left until the pellet goes, then blue, a moment
        // frozen in fright, and running back the way it came.
        const float gx0 = pelletX + gap;
        const float ghostV = (catchX - gx0) / ((CATCH - TURN) * sec);
        float gx;
        if (u < EAT) {
            gx = hx + gap;
        } else if (u < TURN) {
            gx = gx0;
        } else {
            gx = gx0 + ghostV * (u - TURN) * sec;
        }

        // Act two: the hero, huge now, comes back in from the left, faster
        // than the ghost, and catches it at catchX.
        const float bigV = (catchX + big / 2.0f + 1 - (big / 2.0f - 2)) / ((CATCH - BIG) * sec);
        float bx = -big / 2.0f - 1 + bigV * (u - BIG) * sec;
        if (u > CATCH) {
            // Slower once it's eaten, so the hat can be seen landing.
            const float at = -big / 2.0f - 1 + bigV * (CATCH - BIG) * sec;
            bx = at + bigV * 0.35f * (u - CATCH) * sec;
        }
        if (u >= CATCH && !caught) {
            caught = true;
            s.burst(catchX, (float)cy, ArcadeCanvas::WHITE, 30, true);
        }

        if (u >= IN && !caught) {
            const bool scared = u >= EAT;
            // Flashing white for the last stretch, as the fright wears off.
            const bool flash = scared && u > CATCH - 0.1f && std::fmod(ms, 300.0) < 150.0;
            drawGhost(c, gx, cy, n, scared ? 1 : -1, GHOSTS[0], scared, flash, ms, true, k);
        }
        if (u >= BIG && bx < W + big) {
            const bool white = caught && u < CATCH + 0.025f;
            drawHero(c, bx, cy, big, 1, 0.7f * std::fabs(std::sin((float)ms * 0.009f)), white ? ArcadeCanvas::WHITE : PLAYER, k);
        }
        if (caught) {
            // The eyes scurry home off the left edge.
            const float ex = catchX - 40.0f * (u - CATCH) * sec;
            if (ex > -n) {
                // On a dark patch, so they still show while crossing the hero.
                const int x0 = (int)std::lround(ex - (n - 1) / 2.0f), y0 = cy - (n - 1) / 2;
                s.shade(c, x0 + 1, y0 + (n >= 7 ? 2 : 1), n - 2, n >= 7 ? 2 : 1, k);
                drawEyes(c, x0, y0, n, -1, k);
            }
            // The hat flies up off the ghost and lands on the hero's head.
            const float f = std::clamp((u - CATCH) / 0.07f, 0.0f, 1.0f);
            const float topG = (float)(cy - (n - 1) / 2), topB = (float)(cy - (big - 1) / 2);
            const float hatX = catchX + (bx - 1 - catchX) * f;
            // It lands a row down into the top of the head, so it sits on it.
            const float hatY = topG + (topB + 1 - topG) * f - std::sin(f * 3.1416f) * 4.0f;
            drawHat(c, hatX, (int)std::lround(hatY), n, 1, k);
            // Points where the ghost was, as the game would score it, on a
            // dark box so they read over the hero.
            const float pk = std::clamp(1.0f - (u - CATCH - 0.05f) / 0.05f, 0.0f, 1.0f);
            if (pk > 0) {
                const int tw = ArcadeShow::textW("200", 1);
                const int px = std::clamp((int)std::lround(catchX) - tw / 2, 1, W - tw - 1);
                s.shade(c, px, cy - 2, tw, 5, pk * k);
                s.text(c, "200", px, cy - 2, 1, {0.0f, 1.0f, 1.0f}, pk * k);
            }
        }
        s.drawFireworks(c, k);   // the bursts at the pellet and the gobble
    }

    // Behind the credits: the whole cast - the hero, and the four ghosts,
    // hats on - chasing along the bottom of the screen.
    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        const int W = s.W, H = s.H;
        paradeMs += dt * 1000.0;
        const int m = 5, step = m + 3;
        const int y = H - 1 - m / 2 - 1;
        for (int x = 0; x < W; ++x) {
            c.set(x, H - 1, WALL, 0.25f * k);
        }
        const float span = W + 5.0f * step + 2;
        const float lead = (float)std::fmod(paradeMs / 1000.0 * 12.0, (double)span) - m;
        drawHero(c, lead, y, m, 1, 0.75f * std::fabs(std::sin((float)paradeMs * 0.013f)), PLAYER, k);
        for (int g = 0; g < 4; ++g) {
            drawGhost(c, lead - (g + 1) * step, y, m, 1, GHOSTS[g], false, false, paradeMs, true, k);
        }
    }

private:
    // When things happen, as shares of the story.
    static constexpr float IN = 0.04f;      // the hero comes in on the right
    static constexpr float EAT = 0.40f;     // it eats the pellet on the far left
    static constexpr float TURN = 0.46f;    // the ghost turns and runs
    static constexpr float BIG = 0.50f;     // the giant hero comes in on the left
    static constexpr float CATCH = 0.80f;   // and catches the ghost

    // A corridor of the maze: blue walls above and below, with snow along
    // their tops.
    void drawCorridor(ArcadeCanvas::Canvas &c, int W, float k) {
        for (int x = 0; x < W; ++x) {
            c.set(x, wallTop, WALL, 0.3f * k);
            c.set(x, wallBottom, WALL, 0.3f * k);
            const unsigned h = (unsigned)x * 2654435761u;
            if (wallTop > 0 && (h >> 7) % 3 != 0) {
                c.set(x, wallTop - 1, ArcadeCanvas::WHITE, 0.3f * k);
            }
        }
    }

    int n = 7, big = 15, cy = 12, wallTop = 3, wallBottom = 21, pelletX = 3, gap = 10;
    float catchX = 43;
    bool eaten = false, caught = false;
    double paradeMs = 0;
};

// score and level: the game's at the end.
inline ShowConfig config(int score, int level, bool newBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, PLAYER});
    cfg.stats.push_back({"LEVEL", level, false, GHOSTS[2]});
    cfg.roll = {{"MAZE CHASE", HEAD}, {"", WHITE},          {"CAST", HEAD},
                {"THE HERO", PLAYER}, {"FOUR GHOSTS", GHOSTS[1]}, {"", WHITE},
                {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(12340, 4, true);
    story = std::make_unique<Story>();
}

}  // namespace MazeChaseShow
