#pragma once
// Pong's end show (see ArcadeShow.h): P1 WINS! or P2 WINS!, the two scores,
// then one last rally - the paddles knock the ball back and forth, faster
// every time, its rainbow trail growing, until the winner's last smash bursts
// into fireworks in mid-court and it starts to snow. The two paddles meet in
// the middle to shake hands, and a Santa hat drops onto the winner. Then the
// credits, and GAME OVER.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace PongShow {

class Story : public ShowStory {
public:
    using RGB = ArcadeCanvas::RGB;

    // leftWon: the left paddle (player 1) won. p1 and p2: the final score.
    Story(bool leftWon, int p1, int p2) : leftWon(leftWon), p1(p1), p2(p2) {}

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        size = std::max(3, H / 5);   // the game's paddle
        lo = 1;
        hi = (float)(H - 2);
        // The rally, one leg per trip across. Each is quicker than the one
        // before, and the last - the winner's - only gets halfway, where the
        // ball bursts. Times are in shares for now; draw() scales them once
        // it knows how long the story is.
        legs.clear();
        const int n = 8;
        const int winner = leftWon ? 0 : 1;
        int side = (n - 1) % 2 == 0 ? winner : 1 - winner;   // so the winner hits last
        float y = H / 2.0f, t = 0;
        for (int i = 0; i < n; ++i) {
            const bool last = i == n - 1;
            Leg l;
            l.from = side;
            l.x0 = side == 0 ? 2.0f : W - 3.0f;
            l.x1 = last ? W / 2.0f : (side == 0 ? W - 3.0f : 2.0f);
            l.t0 = t;
            l.dur = std::pow(0.76f, (float)i) * (last ? 0.5f : 1.0f);
            l.y0 = y;
            // Up or down, often far enough to come off a wall on the way. The
            // last one heads for the open court below the score, so the burst
            // isn't lost behind it.
            l.dy = last ? (8 + H - 2) / 2.0f - y : (s.rnd() * 2 - 1) * (hi - lo) * 1.3f;
            legs.push_back(l);
            y = fold(y + l.dy);
            t += l.dur;
            side = 1 - side;
        }
        scaled = false;
        hitsShown = 0;
        burstDone = metDone = hatDone = false;
        theme = s.theme();
        flakes.clear();
        const int f = std::max(8, W * H / 45);
        for (int i = 0; i < f; ++i) {
            flakes.push_back({s.rnd() * W, -s.rnd() * H, 2.0f + s.rnd() * 3.0f, s.rnd() * 6.28f});
        }
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        drawCourt(s, c, k);
        drawSnow(s, c, dt, k);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        if (!scaled) {
            // The rally takes the first half, after a moment to see the court.
            const double total = legs.back().t0 + legs.back().dur;
            const double scale = (len * 0.5 - 300.0) / total;
            for (auto &l : legs) {
                l.t0 = 300.0 + l.t0 * scale;
                l.dur *= scale;
            }
            scaled = true;
        }
        const double burstAt = legs.back().t0 + legs.back().dur;
        const double u = ms - burstAt;   // time since the burst
        const int cx = W / 2;

        drawCourt(s, c, k);
        // Score at the top, where the game shows it.
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%d", p1, p2);
        s.text(c, buf, (W - ArcadeShow::textW(buf, 1)) / 2, 2, 1, {0.5f, 0.5f, 0.5f}, k);

        if (u > 0) {
            drawSnow(s, c, dt, k * std::clamp((float)(u / 1500.0), 0.0f, 1.0f));
        }

        // Sparks off each paddle as it hits, in its colour.
        while (hitsShown + 1 < (int)legs.size() && ms >= legs[hitsShown + 1].t0) {
            const Leg &l = legs[++hitsShown];
            s.burst(l.x0, l.y0, paddleColour(l.from), 7, false);
        }

        if (u < 0) {
            // The rainbow trail: where the ball was over the last moment, so
            // it stretches out as the ball speeds up. A pixel is drawn once,
            // or the slow start would pile up into a blob.
            const int samples = 18;
            int lastX = -1000, lastY = -1000;
            for (int j = samples; j >= 1; --j) {
                const double when = ms - j * 9.0;
                float bx, by;
                if (when < legs.front().t0 || !ballAt(when, bx, by)) {
                    continue;
                }
                const int px = (int)std::lround(bx), py = (int)std::lround(by);
                if (px == lastX && py == lastY) {
                    continue;
                }
                lastX = px;
                lastY = py;
                const float f = 1.0f - (float)j / (samples + 1);
                c.add(px, py, s.accent((float)(ms / 700.0) + j * 0.05f), 0.75f * f * f * k);
            }
            float bx, by;
            ballAt(ms, bx, by);
            c.set((int)std::lround(bx), (int)std::lround(by), {1, 1, 1}, k);
        } else if (!burstDone) {
            burstDone = true;
            const Leg &l = legs.back();
            const float by = fold(l.y0 + l.dy);
            s.burst(l.x1, by, {1, 1, 1}, 44, true);
            s.burst(l.x1, by, {1, 0.85f, 0.3f}, 20, false);
        }

        // The paddles: in the rally they follow the ball; after the burst
        // they glide to the middle, shake hands, and the winner gets a hat.
        const float glide = smooth((float)((u - 200.0) / 900.0));
        // Centred in the court below the score, with room for the hat on top.
        const int below = 8, hat = 5;
        const int midTop = std::max(1, std::min(H - 1 - size, below + hat + (H - 2 - below - hat - size) / 2));
        float shake = 0;
        if (u > 1200 && u < 2200) {
            shake = std::sin((float)(u - 1200) / 1000.0f * 3 * 6.2832f) * 1.5f;
        }
        int ys[2], xs[2];
        for (int side = 0; side < 2; ++side) {
            const float rallyTop = paddleTop(side, std::min(ms, burstAt), H);
            const float home = side == 0 ? 1.0f : W - 2.0f, meet = side == 0 ? cx - 1.0f : cx + 1.0f;
            xs[side] = (int)std::lround(home + (meet - home) * glide);
            ys[side] = (int)std::lround(rallyTop + (midTop - rallyTop) * glide + shake);
        }
        if (glide >= 1.0f && !metDone) {
            metDone = true;
            s.burst((float)cx, midTop + size / 2.0f, {1, 0.85f, 0.3f}, 12, false);
        }
        // Two hops for joy once the hat is on.
        const int winner = leftWon ? 0 : 1;
        if (u > 2900 && u < 3700) {
            ys[winner] -= (int)std::lround(std::fabs(std::sin((float)(u - 2900) / 800.0f * 2 * 3.1416f)) * 2.0f);
        }
        s.drawFireworks(c, k);   // the hit sparks and the bursts, under the paddles
        for (int side = 0; side < 2; ++side) {
            for (int y = 0; y < size; ++y) {
                c.set(xs[side], ys[side] + y, paddleColour(side), k);
            }
        }

        // The hat falls from above the wall and lands on the winner's head,
        // its tip flopping away from the handshake.
        const float drop = std::clamp((float)((u - 2300.0) / 400.0), 0.0f, 1.0f);
        if (drop > 0) {
            const int fall = (int)std::lround((1.0f - drop * drop) * (ys[winner] + 1));
            drawHat(c, xs[winner], ys[winner] - fall, leftWon ? -1 : 1, k);
            if (drop >= 1.0f && !hatDone) {
                hatDone = true;
                s.burst((float)xs[winner], ys[winner] - 3.0f, {1, 0.85f, 0.3f}, 8, false);
            }
        }
    }

private:
    struct Leg {
        int from;              // 0 left paddle, 1 right
        float x0, x1, y0, dy;  // across from x0 to x1, y0 + dy unfolded off the walls
        double t0, dur;
    };
    struct Flake {
        float x, y, vy, phase;
    };

    static float smooth(float f) {
        f = std::clamp(f, 0.0f, 1.0f);
        return f * f * (3 - 2 * f);
    }

    // y bounced back and forth off the top and bottom walls.
    float fold(float y) const {
        const float span = hi - lo;
        float m = std::fmod(y - lo, 2 * span);
        if (m < 0) {
            m += 2 * span;
        }
        return lo + (m <= span ? m : 2 * span - m);
    }

    // Where the ball is at ms; false once it has burst.
    bool ballAt(double ms, float &x, float &y) const {
        if (ms < legs.front().t0) {
            x = legs.front().x0;
            y = legs.front().y0;
            return true;
        }
        for (const auto &l : legs) {
            if (ms < l.t0 + l.dur) {
                const float f = (float)((ms - l.t0) / l.dur);
                x = l.x0 + (l.x1 - l.x0) * f;
                y = fold(l.y0 + l.dy * f);
                return true;
            }
        }
        return false;
    }

    // A paddle's top row in the rally: at each of its hits it is centred on
    // the ball, and in between it slides across to where the next one will be,
    // getting there in good time.
    float paddleTop(int side, double ms, int H) const {
        const Leg *prev = nullptr, *next = nullptr;
        for (const auto &l : legs) {
            if (l.from != side) {
                continue;
            }
            if (l.t0 <= ms) {
                prev = &l;
            } else if (!next) {
                next = &l;
            }
        }
        float y;
        if (!prev) {
            y = next ? next->y0 : H / 2.0f;
        } else if (!next) {
            y = prev->y0;
        } else {
            const float f = smooth((float)(((ms - prev->t0) / (next->t0 - prev->t0) - 0.15) / 0.6));
            y = prev->y0 + (next->y0 - prev->y0) * f;
        }
        return std::clamp(y - (size - 1) / 2.0f, 1.0f, (float)(H - 1 - size));
    }

    RGB paddleColour(int side) const {
        return side == (leftWon ? 0 : 1) ? RGB{1, 0.8f, 0.25f} : RGB{0.85f, 0.9f, 1};
    }

    // The game's dim court edge.
    void drawCourt(ArcadeShow &s, ArcadeCanvas::Canvas &c, float k) {
        const RGB edge = {0.14f, 0.14f, 0.27f};
        for (int x = 0; x < s.W; ++x) {
            c.set(x, 0, edge, k);
            c.set(x, s.H - 1, edge, k);
        }
        for (int y = 1; y < s.H - 1; ++y) {
            c.set(0, y, edge, k);
            c.set(s.W - 1, y, edge, k);
        }
    }

    void drawSnow(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) {
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x += std::sin((float)(s.t / 700.0) + f.phase) * 1.2f * dt;
            if (f.y >= s.H) {
                f.y -= s.H + 1;
                f.x = s.rnd() * s.W;
            }
            // Snow; at Halloween falling leaves, drifting further; on an
            // everyday show confetti.
            static const RGB LEAF[3] = {{1.0f, 0.4f, 0.0f}, {0.85f, 0.12f, 0.05f}, {0.55f, 0.3f, 0.05f}};
            const int i = (int)(&f - flakes.data());
            if (theme == ShowTheme::Halloween) {
                f.x += std::sin((float)(s.t / 500.0) + f.phase) * 1.8f * dt;
                c.add((int)f.x, (int)std::floor(f.y), LEAF[i % 3], 0.5f * k);
            } else if (theme == ShowTheme::Everyday) {
                c.add((int)f.x, (int)std::floor(f.y), ArcadeShow::hue(f.phase / 6.28f), 0.55f * k);
            } else {
                c.add((int)f.x, (int)std::floor(f.y), {1, 1, 1}, 0.4f * k);
            }
        }
    }

    // A Santa hat sitting on a paddle whose top is at (x, top): a white brim,
    // a red crown narrowing to a tip that flops over to dir, and a white
    // bobble hanging off the end. Five rows tall. At Halloween a witch's hat
    // (a wide purple brim, an orange band, a tall crown bent over at the
    // tip); on an everyday show a striped party hat with a pompom.
    void drawHat(ArcadeCanvas::Canvas &c, int x, int top, int dir, float k) {
        if (theme == ShowTheme::Halloween) {
            for (int dx = -2; dx <= 2; ++dx) {
                c.set(x + dx, top - 1, ShowArt::HAT, k);
            }
            for (int dx = -1; dx <= 1; ++dx) {
                c.set(x + dx, top - 2, ShowArt::ORANGE, k);
            }
            c.set(x, top - 3, ShowArt::HAT, k);
            c.set(x, top - 4, ShowArt::HAT, k);
            c.set(x + dir, top - 5, ShowArt::HAT, k);
            return;
        }
        if (theme == ShowTheme::Everyday) {
            const RGB A = {0.2f, 0.8f, 1.0f}, B = {1.0f, 0.3f, 0.8f};
            for (int dx = -1; dx <= 1; ++dx) {
                c.set(x + dx, top - 1, A, k);
            }
            c.set(x, top - 2, B, k);
            c.set(x, top - 3, A, k);
            c.set(x, top - 4, {1.0f, 0.85f, 0.1f}, k);   // the pompom
            return;
        }
        const RGB RED = {1, 0.1f, 0.08f}, WHITE = {1, 1, 1};
        for (int dx = -1; dx <= 1; ++dx) {
            c.set(x + dx, top - 1, WHITE, 0.9f * k);
            c.set(x + dx, top - 2, RED, k);
        }
        c.set(x, top - 3, RED, k);
        c.set(x + dir, top - 3, RED, k);
        c.set(x + dir, top - 4, RED, k);
        c.set(x + 2 * dir, top - 4, RED, k);
        c.set(x + 3 * dir, top - 3, WHITE, k);
    }

    bool leftWon;
    int p1, p2;
    int size = 5;
    float lo = 1, hi = 23;
    std::vector<Leg> legs;
    bool scaled = false;
    int hitsShown = 0;
    bool burstDone = false, metDone = false, hatDone = false;
    ShowTheme theme = ShowTheme::Christmas;
    std::vector<Flake> flakes;
};

// p1 and p2: the final score, player 1 (left) first. w: the model's width, so
// the score labels can be spelt out where they fit.
inline ShowConfig config(int p1, int p2, int w) {
    using namespace ShowColours;
    ShowConfig cfg;
    const bool leftWon = p1 >= p2;
    cfg.headline = {leftWon ? "P1" : "P2", "WINS!"};
    const bool roomy = ArcadeShow::textW("PLAYER 1", 1) <= w - 2;
    cfg.stats.push_back({roomy ? "PLAYER 1" : "P1", p1, false, leftWon ? GOLD : LABEL});
    cfg.stats.push_back({roomy ? "PLAYER 2" : "P2", p2, false, leftWon ? LABEL : GOLD});
    cfg.roll = {{"PONG", HEAD},       {"", WHITE},          {"CAST", HEAD}, {"THE BALL", WHITE},
                {"TWO PADDLES", WHITE}, {"AND", HEAD},
                {cfg.theme == ShowTheme::Halloween ? "A WITCH'S HAT" : cfg.theme == ShowTheme::Everyday ? "A PARTY HAT" : "A SANTA HAT", RED},
                {"", WHITE},
                {"THANKS FOR PLAYING", GREEN}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(5, 2, 60);
    story = std::make_unique<Story>(true, 5, 2);
}

}  // namespace PongShow
