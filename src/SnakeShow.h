#pragma once
// Snake's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the length
// (and the best, when this wasn't it), then a victory lap - the snake winds
// across a dark field eating a line of apples, grows with each, swallows a
// golden one that turns it rainbow, and slithers off the right edge - the
// credits, and GAME OVER.
//
// After a duel the headline names the winner, both lengths count up, and the
// lap is the red snake's with the blue one following its trail.

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
    explicit Story(bool twoSnakes = false) : two(twoSnakes) {}

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        hallow = s.halloween();
        body.clear();
        trail.clear();
        lastStepY = 0;
        horizontal = true;
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
            leg(exitX(W), exitY);
            speed = path / std::max(1.0f, (float)(len / 1000.0) * 0.9f);
        }

        // A faint checked field, like the playfield.
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                c.add(x, y, hallow ? ArcadeCanvas::RGB{0.4f, 0.1f, 0.5f} : ArcadeCanvas::RGB{0.1f, 0.5f, 0.15f},
                      ((x + y) & 1 ? 0.05f : 0.08f) * k);
            }
        }

        stepAcc += dt * speed;
        while (stepAcc >= 1.0f) {
            stepAcc -= 1.0f;
            move(s);
        }

        // Apples: red with a green leaf; the golden one twinkles and is bigger.
        // At Halloween little pumpkins, and a jack-o'-lantern that flickers.
        for (const auto &a : apples) {
            if (!a.eaten) {
                c.set(a.x, a.y, hallow ? ShowArt::ORANGE : ArcadeCanvas::RGB{1, 0.1f, 0.08f}, k);
                c.set(a.x, a.y - 1, {0.2f, 0.9f, 0.2f}, 0.7f * k);
            }
        }
        if (!gold.eaten && allEaten()) {
            const float tw = 0.75f + 0.25f * std::sin((float)ms * 0.012f);
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    // The lantern: orange, its top row's eyes lit and flickering.
                    const ArcadeCanvas::RGB col = !hallow ? ArcadeCanvas::RGB{1, 0.8f, 0.15f} : dy == 0 ? ShowArt::CANDLE : ShowArt::ORANGE;
                    c.set(gold.x + dx, gold.y + dy, col, (hallow ? (dy == 0 ? tw : 1.0f) : tw) * k);
                }
            }
            c.set(gold.x, gold.y - 1, {0.2f, 0.9f, 0.2f}, 0.7f * k);
        }

        // The snake, head brightest; rainbow after the golden apple. After a
        // duel it's the red snake, and the blue one follows its trail.
        for (size_t i = 0; i < body.size(); ++i) {
            ArcadeCanvas::RGB col;
            if (rainbow) {
                col = s.accent((float)(ms / 900.0) - i * 0.04f);
            } else {
                const float g = i == 0 ? 1.0f : 0.55f + 0.35f * ((i / 2) & 1);
                col = two ? ArcadeCanvas::RGB{g, 0.15f * g, 0.1f * g} : ArcadeCanvas::RGB{0.25f * g, g, 0.3f * g};
            }
            c.set(body[i].x, body[i].y, i == 0 ? (two ? ArcadeCanvas::RGB{1, 0.8f, 0.7f} : ArcadeCanvas::RGB{0.85f, 1, 0.85f}) : col, k);
        }
        if (two) {
            for (size_t i = 0; i < FOLLOW_LEN; ++i) {
                const size_t at = body.size() + FOLLOW_GAP + i;
                if (at >= trail.size()) {
                    break;
                }
                ArcadeCanvas::RGB col;
                if (rainbow) {
                    col = s.accent((float)(ms / 900.0) + 0.5f - i * 0.04f);
                } else {
                    const float g = i == 0 ? 1.0f : 0.55f + 0.35f * ((i / 2) & 1);
                    col = {0.15f * g, 0.4f * g, g};
                }
                c.set(trail[at].x, trail[at].y, i == 0 ? ArcadeCanvas::RGB{0.6f, 0.9f, 1} : col, k);
            }
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
    // carry on the way it's going until lined up, then turn. Never straight
    // back the way it came: arriving at an apple heading down with the next
    // one up and to the right, it used to reverse up its own column, through
    // its own body (and the blue snake following its trail went through it
    // too). It steps right first instead - every target is to the right -
    // so the route never crosses itself.
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
            target = {exitX(s.W), exitY};
        }
        Cell head = body.front();
        const int dx = target.x - head.x, dy = target.y - head.y;
        const int stepY = dy > 0 ? 1 : -1;
        if (dx != 0 && (horizontal || dy == 0 || stepY == -lastStepY)) {
            head.x += dx > 0 ? 1 : -1;
            horizontal = true;
            lastStepY = 0;
        } else if (dy != 0) {
            head.y += stepY;
            horizontal = false;
            lastStepY = stepY;
        }
        body.push_front(head);
        trail.push_front(head);
        if (trail.size() > 600) {
            trail.pop_back();
        }
        if (grow > 0) {
            --grow;
        } else {
            body.pop_back();
        }
        if (next && head.x == next->x && head.y == next->y) {
            next->eaten = true;
            grow += 3;
            s.burst(head.x, head.y, hallow ? ShowArt::ORANGE : ArcadeCanvas::RGB{1, 0.2f, 0.15f}, 12, false);
        } else if (!next && !gold.eaten && std::abs(head.x - gold.x) <= 1 && std::abs(head.y - gold.y) <= 1) {
            gold.eaten = true;
            rainbow = true;
            grow += 6;
            s.burst(head.x, head.y, {1, 0.8f, 0.2f}, 30, !hallow);
        }
    }

    // Off the right edge, far enough for the whole snake (and a follower) to leave.
    int exitX(int W) const { return W + (int)body.size() + 12 + (two ? (int)(FOLLOW_LEN + FOLLOW_GAP) : 0); }

    static constexpr size_t FOLLOW_LEN = 7, FOLLOW_GAP = 2;   // the blue snake behind the red one
    bool two = false;
    std::deque<Cell> trail;   // where the head has been, newest first, for the follower
    std::deque<Cell> body;
    std::vector<Apple> apples;
    Apple gold{0, 0, false};
    int exitY = 0, grow = 0;
    bool rainbow = false, horizontal = true;
    bool hallow = false;
    int lastStepY = 0;   // the way the last vertical step went (0 after a sideways one)
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
                {std::to_string(std::max(0, length - 3)) + (cfg.theme == ShowTheme::Halloween ? " PUMPKINS" : " APPLES"), RED},
                {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// After a duel: each snake's length, and winner 0 for a tie, 1 red, 2 blue.
// The longer snake is the one that counts as a best.
inline ShowConfig config2(int red, int blue, int winner, bool newBest, int previousBest) {
    using namespace ShowColours;
    const ArcadeCanvas::RGB REDC = {1, 0.25f, 0.2f}, BLUEC = {0.3f, 0.55f, 1};
    ShowConfig cfg;
    if (winner == 0) {
        cfg.headline = {"IT'S A", "TIE!"};
    } else {
        cfg.headline = {winner == 1 ? "RED" : "BLUE", "WINS!"};
    }
    cfg.stats.push_back({"RED", red, false, REDC});
    cfg.stats.push_back({"BLUE", blue, false, BLUEC});
    if (newBest) {
        cfg.stats.push_back({"NEW BEST", std::max(red, blue), false, GOLD});
    } else if (previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"SNAKE", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE RED SNAKE", REDC}, {"THE BLUE SNAKE", BLUEC},
                {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config2(21, 14, 1, true, 19);
    story = std::make_unique<Story>(true);
}

}  // namespace SnakeShow
