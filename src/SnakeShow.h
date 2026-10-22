#pragma once
// Snake's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the length
// (and the best, when this wasn't it), then a victory lap - the snake winds
// across a dark field eating a line of apples, grows with each, swallows a
// golden one that turns it rainbow, and slithers off the right edge - the
// credits, and GAME OVER.

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace SnakeShow {

class Story : public ShowStory {
public:
    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        body.clear();
        apples.clear();
        const int y0 = H / 2;
        for (int i = 0; i < 4; ++i) {
            body.push_back({-1 - i, y0});   // head first, just off the left edge
        }
        // A line of apples left to right, at different heights, so the lap
        // winds up and down on its way across.
        const int n = std::max(3, std::min(6, W / 10));
        for (int i = 0; i < n; ++i) {
            const int x = (int)(W * (0.12f + 0.62f * i / std::max(1, n - 1)));
            const int y = 3 + (int)(s.rnd() * std::max(1, H - 7));
            apples.push_back({x, y, false});
        }
        gold = {(int)(W * 0.86f), std::max(3, H / 2 - 2), false};
        exitY = gold.y;
        grow = 0;
        rainbow = false;
        stepAcc = 0;
        speed = 0;
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        if (speed == 0) {
            // Fast enough to cover the whole lap in the time there is.
            int path = 0, x = body.front().x, y = body.front().y;
            auto leg = [&](int tx, int ty) {
                path += std::abs(tx - x) + std::abs(ty - y);
                x = tx;
                y = ty;
            };
            for (const auto &a : apples) {
                leg(a.x, a.y);
            }
            leg(gold.x, gold.y);
            leg(W + (int)body.size() + 12, exitY);
            speed = path / std::max(1.0f, (float)(len / 1000.0) * 0.9f);
        }

        // A faint checked field, like the playfield.
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                c.add(x, y, {0.1f, 0.5f, 0.15f}, ((x + y) & 1 ? 0.05f : 0.08f) * k);
            }
        }

        stepAcc += dt * speed;
        while (stepAcc >= 1.0f) {
            stepAcc -= 1.0f;
            move(s);
        }

        // Apples: red with a green leaf; the golden one twinkles and is bigger.
        for (const auto &a : apples) {
            if (!a.eaten) {
                c.set(a.x, a.y, {1, 0.1f, 0.08f}, k);
                c.set(a.x, a.y - 1, {0.2f, 0.9f, 0.2f}, 0.7f * k);
            }
        }
        if (!gold.eaten && allEaten()) {
            const float tw = 0.75f + 0.25f * std::sin((float)ms * 0.012f);
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    c.set(gold.x + dx, gold.y + dy, {1, 0.8f, 0.15f}, tw * k);
                }
            }
            c.set(gold.x, gold.y - 1, {0.2f, 0.9f, 0.2f}, 0.7f * k);
        }

        // The snake, head brightest; rainbow after the golden apple.
        for (size_t i = 0; i < body.size(); ++i) {
            ArcadeCanvas::RGB col;
            if (rainbow) {
                col = ArcadeShow::hue((float)(ms / 900.0) - i * 0.04f);
            } else {
                const float g = i == 0 ? 1.0f : 0.55f + 0.35f * ((i / 2) & 1);
                col = {0.25f * g, g, 0.3f * g};
            }
            c.set(body[i].x, body[i].y, i == 0 ? ArcadeCanvas::RGB{0.85f, 1, 0.85f} : col, k);
        }
        s.drawFireworks(c, k);   // the bursts as apples go down
    }

private:
    struct Cell {
        int x, y;
    };
    struct Apple {
        int x, y;
        bool eaten;
    };

    bool allEaten() const {
        return std::all_of(apples.begin(), apples.end(), [](const Apple &a) { return a.eaten; });
    }

    // One step toward the next apple, the golden one, or off the edge, in
    // straight runs with right-angle turns, the way the game's snake moves:
    // carry on the way it's going until lined up, then turn.
    void move(ArcadeShow &s) {
        Cell target;
        Apple *next = nullptr;
        for (auto &a : apples) {
            if (!a.eaten) {
                next = &a;
                break;
            }
        }
        if (next) {
            target = {next->x, next->y};
        } else if (!gold.eaten) {
            target = {gold.x, gold.y};
        } else {
            target = {s.W + (int)body.size() + 12, exitY};
        }
        Cell head = body.front();
        const int dx = target.x - head.x, dy = target.y - head.y;
        if (dx != 0 && (horizontal || dy == 0)) {
            head.x += dx > 0 ? 1 : -1;
            horizontal = true;
        } else if (dy != 0) {
            head.y += dy > 0 ? 1 : -1;
            horizontal = false;
        }
        body.push_front(head);
        if (grow > 0) {
            --grow;
        } else {
            body.pop_back();
        }
        if (next && head.x == next->x && head.y == next->y) {
            next->eaten = true;
            grow += 3;
            s.burst(head.x, head.y, {1, 0.2f, 0.15f}, 12, false);
        } else if (!next && !gold.eaten && std::abs(head.x - gold.x) <= 1 && std::abs(head.y - gold.y) <= 1) {
            gold.eaten = true;
            rainbow = true;
            grow += 6;
            s.burst(head.x, head.y, {1, 0.8f, 0.2f}, 30, true);
        }
    }

    std::deque<Cell> body;
    std::vector<Apple> apples;
    Apple gold{0, 0, false};
    int exitY = 0, grow = 0;
    bool rainbow = false, horizontal = true;
    float stepAcc = 0, speed = 0;
};

// length: the snake's length at the end; previousBest the best before it.
inline ShowConfig config(int length, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"LENGTH", length, false, GREEN});
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"SNAKE", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE SNAKE", GREEN}, {"AND", HEAD},
                {std::to_string(std::max(0, length - 3)) + " APPLES", RED}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(27, true, 19);
    story = std::make_unique<Story>();
}

}  // namespace SnakeShow
