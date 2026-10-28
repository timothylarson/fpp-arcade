#pragma once
// Breakout Versus's end show (see ArcadeShow.h): YELLOW WINS! or BLUE WINS!,
// both players' goals, then the winner's paddle serves a rainbow ball into
// what's left of the wall and smashes through it brick by brick, every brick
// bursting, the loser's paddle doing its best to keep it in play - until the
// ball goes up in a last firework. The credits, and GAME OVER.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace BreakoutVersusShow {

const ArcadeCanvas::RGB YELLOW = {1.0f, 0.85f, 0.1f};
const ArcadeCanvas::RGB BLUE = {0.3f, 0.6f, 1.0f};

class Story : public ShowStory {
public:
    explicit Story(bool topWon) : topWins(topWon) {}

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        bricks.clear();
        // The wall as the game builds it: rows of three-LED bricks across the middle.
        const int rows = std::max(2, H / 6), top = H / 2 - rows / 2;
        for (int r = 0; r < rows; ++r) {
            for (int x = 1 + (r & 1) * 2; x + 2 < W - 1; x += 4) {
                bricks.push_back({x, top + r, true, s.accent(0.08f + 0.6f * r / std::max(1, rows - 1))});
            }
        }
        pw = std::max(4, W / 8);
        bottomX = topX = W / 2.0f - pw / 2.0f;
        bx = W / 2.0f;
        by = topWins ? 1.5f : H - 1.5f;
        vx = 9.0f;
        vy = topWins ? 24.0f : -24.0f;
        popped = false;
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        const float u = (float)(ms / len);

        // The ball, in small steps so it can't skip a brick; both paddles
        // follow it, so it stays in play until it's time for the finale.
        if (!popped) {
            const int steps = std::max(1, (int)std::ceil(std::hypot(vx, vy) * dt / 0.5f));
            for (int i = 0; i < steps; ++i) {
                move(s, dt / steps, W, H);
            }
            const float follow = 40.0f * dt;
            bottomX += std::clamp(bx - pw / 2.0f - bottomX, -follow, follow);
            topX += std::clamp(bx - pw / 2.0f - topX, -follow, follow);
            bottomX = std::clamp(bottomX, 0.0f, (float)(W - pw));
            topX = std::clamp(topX, 0.0f, (float)(W - pw));
            if (u > 0.82f) {
                popped = true;
                s.burst(bx, by, {1, 1, 1}, 50, true);
                for (auto &b : bricks) {
                    if (b.alive) {
                        b.alive = false;
                        s.burst(b.x + 1.0f, (float)b.y, b.col, 6, false);
                    }
                }
            }
            // A rainbow trail behind the ball.
            if (s.rnd() < dt * 40.0f) {
                s.sparks.push_back({bx, by, 0, 0, 0, 0.3f, s.accent((float)(ms / 300.0)), 0, 0});
            }
        }

        for (const auto &b : bricks) {
            if (b.alive) {
                for (int i = 0; i < 3; ++i) {
                    c.set(b.x + i, b.y, b.col, 0.6f * k);
                }
            }
        }
        // The paddles: the winner's flashes once the ball has gone up.
        const float flash = popped && std::fmod(ms, 300.0) < 150.0 ? 1.0f : 0.8f;
        for (int i = 0; i < pw; ++i) {
            c.set((int)bottomX + i, H - 1, YELLOW, (!topWins ? flash : 0.8f) * k);
            c.set((int)topX + i, 0, BLUE, (topWins ? flash : 0.8f) * k);
        }
        if (!popped) {
            c.set((int)std::floor(bx), (int)std::floor(by), s.accent((float)(ms / 400.0)), k);
        }
        s.drawFireworks(c, k);
    }

private:
    struct Brick {
        int x, y;
        bool alive;
        ArcadeCanvas::RGB col;
    };

    void move(ArcadeShow &s, float dt, int W, int H) {
        const float nx = bx + vx * dt, ny = by + vy * dt;
        // A power ball: it smashes straight through, each brick bursting.
        // (Bouncing off each one, it only ever dug a single column.)
        for (auto &b : bricks) {
            if (b.alive && nx >= b.x && nx < b.x + 3 && (int)std::floor(ny) == b.y) {
                b.alive = false;
                s.burst(b.x + 1.0f, b.y + 0.5f, b.col, 10, false);
            }
        }
        bx = nx;
        by = ny;
        if (bx < 0.5f || bx > W - 0.5f) {
            vx = -vx;
            bx = std::clamp(bx, 0.5f, W - 0.5f);
        }
        // The paddles always make the save in the show.
        // A new angle off each paddle, so it carves its way across the wall.
        if ((by > H - 1.5f && vy > 0) || (by < 1.5f && vy < 0)) {
            vy = -vy;
            vx = (s.rnd() < 0.5f ? -1.0f : 1.0f) * (8.0f + s.rnd() * 14.0f);
        }
    }

    bool topWins = false, popped = false;
    std::vector<Brick> bricks;
    int pw = 6;
    float bottomX = 0, topX = 0, bx = 0, by = 0, vx = 0, vy = 0;
};

// bottom and top: each player's goals; the winner gets the headline.
inline ShowConfig config(int bottom, int top) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = {bottom > top ? "YELLOW" : "BLUE", "WINS!"};
    cfg.stats.push_back({"YELLOW", bottom, false, YELLOW});
    cfg.stats.push_back({"BLUE", top, false, BLUE});
    cfg.roll = {{"BREAKOUT VERSUS", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE YELLOW PADDLE", YELLOW},
                {"THE BLUE PADDLE", BLUE}, {"TWO BALLS", WHITE}, {"AND A WALL", WHITE}, {"", WHITE},
                {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(5, 2);
    story = std::make_unique<Story>(false);
}

}  // namespace BreakoutVersusShow
