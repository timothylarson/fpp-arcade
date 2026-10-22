#pragma once
// The show for winning Breakout, stretched to the length of its WonGame music:
//
//   YOU WON!     big letters bouncing in over fireworks
//   stats        bricks broken and time played, counting up
//   a story      a snowy town at night; Santa's sleigh comes by, the paddle
//                tosses the ball up to ride along, presents go down the
//                chimneys and the sleigh flies off across the moon
//   credits      rolling up the screen, with the lines from the settings
//   THE END      over a last burst of fireworks
//
// It knows nothing about the game: it draws into a canvas the size of the
// model, from the numbers it's given.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "ArcadeCanvas.h"
#include "FPPArcade.h"

class BreakoutCredits {
public:
    using RGB = ArcadeCanvas::RGB;

    // thanks and extraLines: the SPECIAL THANKS and the closing lines of the
    // roll, from the settings. No thanks leaves that heading out.
    void start(int w, int h, int bricks, double playMs, const std::vector<std::string> &thanks,
               const std::vector<std::string> &extraLines, double lengthMs) {
        W = std::max(8, w);
        H = std::max(8, h);
        T = std::max(5000.0, lengthMs);
        t = 0;
        bricksBroken = bricks;
        playSeconds = (int)(playMs / 1000.0);
        rng.seed(std::random_device{}());
        rockets.clear();
        sparks.clear();
        presents.clear();
        rocketDue = 0;
        statsBursts = 0;
        big = std::max(1, std::min((H - 3) / 11, W / 15));
        roll = std::max(1, H / 40);
        buildSky();
        buildTown();
        buildRoll(thanks, extraLines);
        sleighReset();
    }

    double elapsedMs() const { return t; }
    bool over() const { return t >= T; }
    void finish() { t = T; }

    void step(double dtMs, ArcadeCanvas::Canvas &c) {
        const float dt = (float)std::min(dtMs, 100.0) / 1000.0f;
        t += dtMs;
        c.clear();
        const double s = t / T;

        // Fireworks run underneath everything; how often depends on the scene.
        float rate = 0;
        if (s < P_STATS) {
            rate = 2.4f;
        } else if (s < P_STORY) {
            rate = 1.0f;
        } else if (s >= P_END) {
            rate = 4.0f;
        } else if (s >= P_ROLL) {
            rate = 0.35f;
        }
        stepFireworks(dt, rate);

        if (s < P_STATS) {
            drawFireworks(c, 0.55f);
            drawYouWon(c, t, phaseFade(0, P_STATS));
        } else if (s < P_STORY) {
            drawFireworks(c, 0.45f);
            drawStats(c, t - P_STATS * T, (P_STORY - P_STATS) * T, phaseFade(P_STATS, P_STORY));
        } else if (s < P_ROLL) {
            drawStory(c, dt, t - P_STORY * T, (P_ROLL - P_STORY) * T, phaseFade(P_STORY, P_ROLL));
        } else if (s < P_END) {
            const float k = phaseFade(P_ROLL, P_END);
            drawNight(c, dt, k * 0.6f);
            drawFireworks(c, 0.4f);
            drawRoll(c, (t - P_ROLL * T) / ((P_END - P_ROLL) * T), k);
        } else {
            drawNight(c, dt, 0.5f);
            drawFireworks(c, 0.8f);
            drawTheEnd(c, t - P_END * T);
        }
        // Everything fades out over the last second.
        const float out = (float)std::clamp((T - t) / 1000.0, 0.0, 1.0);
        if (out < 1.0f) {
            c.scale(out);
        }
    }

private:
    // Where each scene starts, as a share of the whole.
    static constexpr double P_STATS = 0.15, P_STORY = 0.27, P_ROLL = 0.62, P_END = 0.90;

    struct Rocket {
        float x, y, vy, burstY;
        RGB col;
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
        float twinkle;   // 0 = steady
        float gravity;
    };
    struct Flake {
        float x, y, vy, phase;
    };
    struct Star {
        int x, y;
        float phase;
    };
    struct House {
        int x, w, h;          // body on rows [H-1-h, H-2]
        int chimneyX;
        bool gotPresent;
        unsigned windows;     // bit per window: lit
    };
    struct Present {
        float x, y;
        int house;
        RGB col;
    };
    struct Line {
        std::string text;
        RGB col;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    static RGB hue(float h) {
        h -= std::floor(h);
        return {ArcadeCanvas::clamp01(std::fabs(h * 6 - 3) - 1), ArcadeCanvas::clamp01(2 - std::fabs(h * 6 - 2)),
                ArcadeCanvas::clamp01(2 - std::fabs(h * 6 - 4))};
    }
    static float easeOutBounce(float x) {
        const float n = 7.5625f, d = 2.75f;
        if (x < 1 / d) {
            return n * x * x;
        }
        if (x < 2 / d) {
            x -= 1.5f / d;
            return n * x * x + 0.75f;
        }
        if (x < 2.5f / d) {
            x -= 2.25f / d;
            return n * x * x + 0.9375f;
        }
        x -= 2.625f / d;
        return n * x * x + 0.984375f;
    }
    // In over the first 300 ms of a scene and out over its last 300.
    float phaseFade(double from, double to) const {
        const double a = t - from * T, b = to * T - t;
        return (float)std::clamp(std::min(a, b) / 300.0, 0.0, 1.0);
    }

    // ------------------------------------------------------------- text
    static int textW(const std::string &s, int scl) { return s.empty() ? 0 : (int)s.size() * 4 * scl - scl; }
    void text(ArcadeCanvas::Canvas &c, const std::string &s, int x, int y, int scl, const RGB &col, float k) {
        for (size_t i = 0; i < s.size(); ++i) {
            letter(c, s[i], x + (int)i * 4 * scl, y, scl, col, k);
        }
    }
    void letter(ArcadeCanvas::Canvas &c, char ch, int x, int y, int scl, const RGB &col, float k) {
        for (int py = 0; py < 5 * scl; ++py) {
            for (int px = 0; px < 3 * scl; ++px) {
                if (arcadeGlyphScaled(ch, px, py, scl)) {
                    c.set(x + px, y + py, col, k);
                }
            }
        }
    }
    // Darkens a box so text reads over fireworks.
    void shade(ArcadeCanvas::Canvas &c, int x, int y, int w, int h, float k) {
        for (int yy = y - 1; yy <= y + h; ++yy) {
            for (int xx = x - 1; xx <= x + w; ++xx) {
                c.add(xx, yy, {-1, -1, -1}, 0.7f * k);
            }
        }
    }

    // -------------------------------------------------------- fireworks
    void stepFireworks(float dt, float rate) {
        rocketDue -= dt * rate;
        while (rate > 0 && rocketDue <= 0) {
            rocketDue += 0.5f + rnd();
            static const RGB COLS[] = {{1, 0.15f, 0.1f}, {0.2f, 1, 0.3f}, {1, 0.8f, 0.2f}, {0.3f, 0.5f, 1}, {0.9f, 0.3f, 1}, {1, 1, 1}};
            rockets.push_back({W * (0.1f + 0.8f * rnd()), (float)H, -(H * 1.3f + rnd() * H * 0.5f),
                               H * (0.12f + 0.35f * rnd()), COLS[(int)(rnd() * 6) % 6]});
        }
        for (size_t i = 0; i < rockets.size();) {
            Rocket &r = rockets[i];
            r.y += r.vy * dt;
            r.vy += H * 0.6f * dt;   // slows toward the top
            if (r.y <= r.burstY || r.vy >= 0) {
                burst(r.x, r.y, r.col, 18 + (int)(rnd() * 16), rnd() < 0.3f);
                rockets.erase(rockets.begin() + i);
            } else {
                ++i;
            }
        }
        for (size_t i = 0; i < sparks.size();) {
            Spark &p = sparks[i];
            p.age += dt;
            p.vy += p.gravity * dt;
            p.vx *= 1.0f - 0.8f * dt;
            p.vy *= 1.0f - 0.8f * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if (p.age >= p.life) {
                sparks[i] = sparks.back();
                sparks.pop_back();
            } else {
                ++i;
            }
        }
    }
    void burst(float x, float y, const RGB &col, int n, bool rainbow) {
        const float sp = (W + H) / 9.0f;
        for (int k = 0; k < n; ++k) {
            const float a = 6.2832f * (k + rnd() * 0.5f) / n, v = sp * (0.55f + 0.6f * rnd());
            sparks.push_back({x, y, std::cos(a) * v, std::sin(a) * v, 0, 0.9f + 0.8f * rnd(),
                              rainbow ? hue((float)k / n) : col, rnd() < 0.4f ? 25 + rnd() * 20 : 0, 7.0f});
        }
    }
    void drawFireworks(ArcadeCanvas::Canvas &c, float k) {
        for (auto &r : rockets) {
            c.add((int)r.x, (int)r.y, {1, 0.8f, 0.4f}, 0.8f * k);
            c.add((int)r.x, (int)r.y + 1, {1, 0.5f, 0.2f}, 0.35f * k);
            c.add((int)r.x, (int)r.y + 2, {1, 0.4f, 0.1f}, 0.15f * k);
        }
        for (auto &p : sparks) {
            float f = std::pow(1.0f - p.age / p.life, 1.5f);
            if (p.twinkle > 0) {
                f *= 0.4f + 0.6f * (0.5f + 0.5f * std::sin(p.age * p.twinkle + p.x));
            }
            c.add((int)std::floor(p.x), (int)std::floor(p.y), p.col, f * k);
        }
    }

    // --------------------------------------------------------- YOU WON!
    void drawYouWon(ArcadeCanvas::Canvas &c, double ms, float k) {
        static const char *LINES[2] = {"YOU", "WON!"};
        const int gap = big + 1, h = 10 * big + gap;
        const int y0 = (H - h) / 2;
        int idx = 0;
        for (int l = 0; l < 2; ++l) {
            const std::string s = LINES[l];
            const int x0 = (W - textW(s, big)) / 2, y = y0 + l * (5 * big + gap);
            for (size_t i = 0; i < s.size(); ++i, ++idx) {
                const float e = easeOutBounce(std::clamp((float)(ms - 150.0 * idx) / 750.0f, 0.0f, 1.0f));
                if (e <= 0) {
                    continue;
                }
                const int yy = y - (int)std::lround((1.0f - e) * (H + 5 * big));
                const int xx = x0 + (int)i * 4 * big;
                shade(c, xx, yy, 3 * big, 5 * big, k);
                letter(c, s[i], xx, yy, big, hue((float)(ms / 2500.0) + idx * 0.13f), k);
            }
        }
    }

    // ------------------------------------------------------------ stats
    // BRICKS for the first half, then TIME, each counting up big and centred.
    void drawStats(ArcadeCanvas::Canvas &c, double ms, double len, float k) {
        const bool second = ms >= len / 2;
        const double half = len / 2, local = second ? ms - half : ms;
        const float f = std::clamp((float)(local / (half * 0.65)), 0.0f, 1.0f);
        const float e = 1.0f - std::pow(1.0f - f, 3.0f);
        std::string name, value;
        RGB col;
        if (!second) {
            name = "BRICKS";
            value = std::to_string((int)std::lround(bricksBroken * e));
            col = {1.0f, 0.8f, 0.2f};
        } else {
            const int secs = (int)std::lround(playSeconds * e);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d:%02d", secs / 60, secs % 60);
            name = "TIME";
            value = buf;
            col = {0.3f, 1.0f, 0.45f};
        }
        // Each half fades in and out on its own.
        const float kk = k * (float)std::clamp(std::min(local, half - local) / 250.0, 0.0, 1.0);
        const int num = textW(value, big) <= W - 2 ? big : 1;
        const int h = 5 + 2 + 5 * num, y = (H - h) / 2;
        const int w = std::max(textW(name, 1), textW(value, num));
        shade(c, (W - w) / 2, y, w, h, kk);
        text(c, name, (W - textW(name, 1)) / 2, y, 1, {0.8f, 0.85f, 1.0f}, 0.8f * kk);
        text(c, value, (W - textW(value, num)) / 2, y + 7, num, col, kk);
        if (f >= 1.0f && statsBursts < (second ? 2 : 1)) {
            ++statsBursts;
            burst(W * 0.2f, y + h / 2.0f, col, 20, false);
            burst(W * 0.8f, y + h / 2.0f, col, 20, false);
        }
    }

    // ------------------------------------------------------------ night
    void buildSky() {
        stars.clear();
        const int n = std::max(6, W * H / 70);
        for (int i = 0; i < n; ++i) {
            stars.push_back({(int)(rnd() * W), (int)(rnd() * H * 0.6f), rnd() * 6.28f});
        }
        flakes.clear();
        const int f = std::max(8, W * H / 55);
        for (int i = 0; i < f; ++i) {
            flakes.push_back({rnd() * W, rnd() * H, 2.0f + rnd() * 3.5f, rnd() * 6.28f});
        }
        moonX = W * 0.74f;
        moonY = H * 0.3f;
        moonR = std::max(2.5f, H / 6.0f);
    }
    // Stars, moon and snow: the sky behind the story and the credits.
    void drawNight(ArcadeCanvas::Canvas &c, float dt, float k) {
        for (int y = 0; y < H; ++y) {
            const float g = 0.07f * (1.0f - (float)y / H);
            for (int x = 0; x < W; ++x) {
                c.add(x, y, {0.0f, 0.02f, 1.0f}, g * k);
            }
        }
        for (auto &s : stars) {
            c.add(s.x, s.y, {0.9f, 0.9f, 1.0f}, (0.12f + 0.12f * std::sin((float)(t / 400.0) + s.phase)) * k);
        }
        for (int y = (int)(moonY - moonR - 1); y <= (int)(moonY + moonR + 1); ++y) {
            for (int x = (int)(moonX - moonR - 1); x <= (int)(moonX + moonR + 1); ++x) {
                const float d = std::hypot(x + 0.5f - moonX, y + 0.5f - moonY);
                const float in = ArcadeCanvas::clamp01(moonR - d + 0.5f);
                // A darker patch or two, so it reads as the moon and not a ball.
                const float spot = (std::hypot(x + 0.5f - (moonX - moonR * 0.3f), y + 0.5f - (moonY - moonR * 0.2f)) < moonR * 0.3f) ? 0.75f : 1.0f;
                c.add(x, y, {1.0f, 0.93f, 0.7f}, 0.5f * in * spot * k);
            }
        }
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x += std::sin((float)(t / 700.0) + f.phase) * 1.2f * dt;
            if (f.y >= H) {
                f.y -= H + 1;
                f.x = rnd() * W;
            }
            c.add((int)f.x, (int)f.y, {1, 1, 1}, 0.45f * k);
        }
    }

    // ------------------------------------------------------------ story
    void buildTown() {
        houses.clear();
        paddleX = W * 0.28f;
        paddleW = std::max(4, W / 10);
        int x = 0;
        while (x < W) {
            const int w = 5 + (int)(rnd() * 4);
            // Leave room around the paddle.
            if (x + w > paddleX - paddleW && x < paddleX + paddleW) {
                x = (int)(paddleX + paddleW) + 1;
                continue;
            }
            if (x + w > W) {
                break;
            }
            const int h = std::min(H / 4, 3 + (int)(rnd() * 3));
            houses.push_back({x, w, h, x + w - 2, false, (unsigned)(rnd() * 4)});
            x += w + 1 + (int)(rnd() * 3);
        }
    }
    int chimneyTop(const House &h) const { return H - 1 - h.h - 4; }
    void drawTown(ArcadeCanvas::Canvas &c, float k) {
        for (int x = 0; x < W; ++x) {
            c.set(x, H - 1, {0.85f, 0.9f, 1.0f}, 0.35f * k);   // snow on the ground
        }
        for (const auto &h : houses) {
            const int top = H - 1 - h.h;
            for (int y = top; y <= H - 2; ++y) {
                for (int x = h.x; x < h.x + h.w; ++x) {
                    c.set(x, y, {0.35f, 0.17f, 0.08f}, 0.45f * k);
                }
            }
            // Two windows, warm when lit; a house that got its present glows.
            const float glow = h.gotPresent ? 1.0f : 0.55f;
            for (int wdx = 0; wdx < 2; ++wdx) {
                const bool lit = h.gotPresent || (h.windows >> wdx & 1);
                const int wx = wdx ? h.x + h.w - 2 : h.x + 1;
                c.set(wx, top + 1, lit ? RGB{1.0f, 0.7f, 0.25f} : RGB{0.05f, 0.05f, 0.1f}, (lit ? glow : 1.0f) * k);
            }
            // The roof, snowy along the top edge, and a chimney.
            const int rows = (h.w + 1) / 2;
            for (int r = 1; r <= rows; ++r) {
                const int x0 = h.x + r - 1, x1 = h.x + h.w - r;
                for (int x = x0; x <= x1; ++x) {
                    const bool edge = x == x0 || x == x1;
                    c.set(x, top - r, edge ? RGB{0.85f, 0.9f, 1.0f} : RGB{0.6f, 0.08f, 0.06f}, (edge ? 0.5f : 0.55f) * k);
                }
            }
            // The chimney is the second column from the right, where the
            // roof is two rows deep; it stands two above that.
            c.set(h.chimneyX, top - 3, {0.45f, 0.4f, 0.4f}, 0.6f * k);
            c.set(h.chimneyX, top - 4, {0.45f, 0.4f, 0.4f}, 0.6f * k);
        }
    }

    void sleighReset() {
        ballState = 0;
        ballLaunchMs = 0;
    }

    // The sleigh, facing right: Santa in the sleigh, then two reindeer on a
    // harness, the lead one with a red nose. 17 x 5.
    void drawSleigh(ArcadeCanvas::Canvas &c, int sx, int sy, double ms, bool withBall, float k) {
        static const char *ROWS[5] = {
            ".RW.......A.A.A.A",
            "SKW..S.....B...BN",
            "SRRSSSHHHBBHHBB..",
            "SSSSSS...........",
            "GGGGGGG..........",
        };
        const RGB RED = {1, 0.1f, 0.08f}, WHITE = {1, 1, 1}, SKIN = {1, 0.6f, 0.45f}, GOLD = {1, 0.75f, 0.15f},
                  BROWN = {0.55f, 0.28f, 0.1f}, ANTLER = {0.8f, 0.6f, 0.35f}, HARNESS = {0.7f, 0.5f, 0.1f};
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 17; ++x) {
                RGB col;
                float kk = 1;
                switch (ROWS[y][x]) {
                case 'R': col = RED; break;
                case 'S': col = RED; kk = 0.75f; break;
                case 'W': col = WHITE; break;
                case 'K': col = SKIN; break;
                case 'G': col = GOLD; kk = 0.8f; break;
                case 'B': col = BROWN; break;
                case 'A': col = ANTLER; kk = 0.8f; break;
                case 'H': col = HARNESS; kk = 0.6f; break;
                case 'N': col = RED; kk = 0.55f + 0.45f * (float)(std::fmod(ms, 600.0) < 300.0); break;   // Rudolph
                default: continue;
                }
                c.set(sx + x, sy + y, col, kk * k);
            }
        }
        // Legs, trotting.
        const bool step = std::fmod(ms, 300.0) < 150.0;
        for (int deer : {9, 13}) {
            c.set(sx + deer + (step ? 0 : -1), sy + 3, BROWN, 0.9f * k);
            c.set(sx + deer + (step ? 1 : 2), sy + 3, BROWN, 0.9f * k);
        }
        if (withBall) {
            c.set(sx + 3, sy + 1, WHITE, k);
        }
    }

    void drawStory(ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) {
        const float u = (float)(ms / len);
        drawNight(c, dt, k);

        // The sleigh crosses from u = 0.15 to u = 0.95, bobbing a little.
        const float enter = 0.15f, leave = 0.95f;
        const float speed = (W + 19) / ((leave - enter) * (float)len / 1000.0f);   // LEDs a second
        const float sxf = -18.0f + (u - enter) / (leave - enter) * (W + 19);
        const int sx = (int)std::floor(sxf);
        const int sy = (int)std::lround(H * 0.2f + std::sin(sxf * 0.25f) * 1.2f);
        const bool sleighOn = u >= enter && u <= leave;

        // The ball: bouncing on the paddle, then tossed up to the sleigh
        // timed to land in Santa's lap, then riding along.
        const int py = H - 2;
        float bx = paddleX, by = py - 1;
        const float flight = 0.7f;
        if (ballState == 0) {
            by = py - 1 - std::fabs(std::sin((float)ms * 0.005f)) * std::min(5.0f, H * 0.2f);
            if (sleighOn && sxf + 3 + speed * flight >= paddleX) {
                ballState = 1;
                ballLaunchMs = ms;
            }
        }
        if (ballState == 1) {
            const float f = std::clamp((float)((ms - ballLaunchMs) / 1000.0) / flight, 0.0f, 1.0f);
            bx = paddleX + (sxf + 3 - paddleX) * f;
            by = (py - 1) + (sy + 1 - (py - 1)) * f - std::sin(f * 3.1416f) * 4.0f;
            if (f >= 1.0f) {
                ballState = 2;
                burst(sxf + 3, (float)sy + 1, {1, 1, 1}, 12, true);
            }
        }

        // Presents down the chimneys once the ball is aboard.
        if (sleighOn && ballState == 2) {
            for (size_t i = 0; i < houses.size(); ++i) {
                House &h = houses[i];
                if (!h.gotPresent && sx + 2 >= h.chimneyX && sx + 2 < h.chimneyX + 2 &&
                    std::none_of(presents.begin(), presents.end(), [&](const Present &p) { return p.house == (int)i; })) {
                    static const RGB COLS[] = {{1, 0.15f, 0.15f}, {0.2f, 1, 0.3f}, {1, 0.8f, 0.2f}, {0.4f, 0.6f, 1}};
                    presents.push_back({(float)h.chimneyX, (float)sy + 4, (int)i, COLS[i % 4]});
                }
            }
        }
        for (size_t i = 0; i < presents.size();) {
            Present &p = presents[i];
            p.y += std::max(6.0f, H * 0.5f) * dt;
            House &h = houses[p.house];
            if (p.y >= chimneyTop(h)) {
                h.gotPresent = true;
                burst(p.x, p.y, p.col, 8, false);
                presents.erase(presents.begin() + i);
            } else {
                ++i;
            }
        }

        // Windows flicker on and off now and then.
        if (rnd() < dt * 1.5f && !houses.empty()) {
            houses[(size_t)(rnd() * houses.size()) % houses.size()].windows ^= 1u << (int)(rnd() * 2);
        }

        drawTown(c, k);
        for (auto &p : presents) {
            c.set((int)p.x, (int)p.y, p.col, k);
        }

        // The paddle, which hops for joy once the ball is away.
        const int hop = ballState == 2 && std::fmod(ms - ballLaunchMs, 4000.0) < 2500.0
                            ? (int)std::lround(std::fabs(std::sin((float)(ms - ballLaunchMs) * 0.009f)) * 2.0f) : 0;
        for (int x = 0; x < paddleW; ++x) {
            c.set((int)paddleX - paddleW / 2 + x, py - hop, {0.85f, 0.9f, 1.0f}, k);
        }
        if (ballState < 2) {
            c.set((int)std::lround(bx), (int)std::lround(by), {1, 1, 1}, k);
        }

        if (sleighOn) {
            // A trail of gold sparkles behind.
            if (rnd() < dt * 25.0f) {
                sparks.push_back({sxf - 0.5f, sy + 3.0f + rnd(), -speed * 0.2f, rnd() * 2 - 1, 0, 0.5f + rnd() * 0.5f,
                                  {1, 0.8f, 0.3f}, 30, 2.0f});
            }
            drawSleigh(c, sx, sy, ms, ballState == 2, k);
        }
        drawFireworks(c, k);   // the sparkles, and the bursts as presents land
    }

    // ------------------------------------------------------------ roll
    void buildRoll(const std::vector<std::string> &thanks, const std::vector<std::string> &extra) {
        lines.clear();
        const RGB HEAD = {1.0f, 0.8f, 0.2f}, WHITE = {1, 1, 1}, RED = {1, 0.3f, 0.25f}, GREEN = {0.35f, 1, 0.45f};
        const int maxChars = std::max(1, (W + roll) / (4 * roll));
        auto add = [&](const std::string &s, const RGB &col) {
            // Word-wrap to the width; a word too long for a line is hyphenated.
            std::string up;
            for (char ch : s) {
                up += (char)std::toupper((unsigned char)ch);
            }
            std::string cur;
            size_t i = 0;
            while (i < up.size()) {
                size_t j = up.find(' ', i);
                std::string word = up.substr(i, j == std::string::npos ? std::string::npos : j - i);
                i = j == std::string::npos ? up.size() : j + 1;
                while ((int)word.size() > maxChars) {
                    if (!cur.empty()) {
                        lines.push_back({cur, col});
                        cur.clear();
                    }
                    const int cut = std::max(1, maxChars - 1);
                    lines.push_back({word.substr(0, cut) + (maxChars > 1 ? "-" : ""), col});
                    word = word.substr(cut);
                }
                if (word.empty()) {
                    continue;
                }
                if (!cur.empty() && (int)(cur.size() + 1 + word.size()) > maxChars) {
                    lines.push_back({cur, col});
                    cur.clear();
                }
                cur += (cur.empty() ? "" : " ") + word;
            }
            if (!cur.empty() || s.empty()) {
                lines.push_back({cur, col});
            }
        };
        add(maxChars >= 8 ? "BREAKOUT" : "BREAK OUT", HEAD);
        add("", WHITE);
        add("CAST", HEAD);
        add("THE BALL", WHITE);
        add("THE PADDLE", WHITE);
        add("AND", HEAD);
        add(std::to_string(bricksBroken) + " BRICKS", WHITE);
        if (!thanks.empty()) {
            add("", WHITE);
            add("SPECIAL THANKS", HEAD);
            int n = 0;
            for (const auto &l : thanks) {
                add(l, n++ % 2 ? GREEN : RED);
            }
        }
        if (!extra.empty()) {
            add("", WHITE);
            int n = 0;
            for (const auto &l : extra) {
                add(l, n++ % 2 ? GREEN : RED);
            }
        }
        add("", WHITE);
        add("AND YOU!", HEAD);
    }
    void drawRoll(ArcadeCanvas::Canvas &c, double f, float k) {
        const int lh = 7 * roll;
        const int content = (int)lines.size() * lh;
        const int y0 = H - (int)std::lround(f * (H + content));
        for (size_t i = 0; i < lines.size(); ++i) {
            const int y = y0 + (int)i * lh;
            if (y < -5 * roll || y >= H || lines[i].text.empty()) {
                continue;
            }
            const int x = (W - textW(lines[i].text, roll)) / 2;
            shade(c, x, y, textW(lines[i].text, roll), 5 * roll, k);
            text(c, lines[i].text, x, y, roll, lines[i].col, k);
        }
    }

    // ---------------------------------------------------------- THE END
    void drawTheEnd(ArcadeCanvas::Canvas &c, double ms) {
        const float k = std::clamp((float)(ms / 800.0), 0.0f, 1.0f);
        const RGB col = {1.0f, 0.85f, 0.4f};
        if (textW("THE END", big) <= W - 2) {
            const int x = (W - textW("THE END", big)) / 2, y = (H - 5 * big) / 2;
            shade(c, x, y, textW("THE END", big), 5 * big, k);
            text(c, "THE END", x, y, big, col, k);
        } else {
            const int gap = big + 1, y = (H - 10 * big - gap) / 2;
            for (int l = 0; l < 2; ++l) {
                const std::string s = l ? "END" : "THE";
                const int x = (W - textW(s, big)) / 2, yy = y + l * (5 * big + gap);
                shade(c, x, yy, textW(s, big), 5 * big, k);
                text(c, s, x, yy, big, col, k);
            }
        }
    }

    int W = 60, H = 25, big = 2, roll = 1;
    double T = 30000, t = 0;
    int bricksBroken = 0, playSeconds = 0;
    std::mt19937 rng;
    std::vector<Rocket> rockets;
    std::vector<Spark> sparks;
    float rocketDue = 0;
    int statsBursts = 0;
    std::vector<Star> stars;
    std::vector<Flake> flakes;
    float moonX = 0, moonY = 0, moonR = 3;
    std::vector<House> houses;
    std::vector<Present> presents;
    float paddleX = 0;
    int paddleW = 6;
    int ballState = 0;   // 0 on the paddle, 1 flying up, 2 in the sleigh
    double ballLaunchMs = 0;
    std::vector<Line> lines;
};
