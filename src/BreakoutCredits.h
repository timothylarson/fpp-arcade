#pragma once
// The show for winning Breakout, stretched to the length of its WonGame music
// (see ArcadeShow.h for the parts every show shares):
//
//   YOU WON!     big letters bouncing in over fireworks
//   stats        bricks broken and time played, counting up
//   a story      a snowy town at night; Santa's sleigh comes by, the paddle
//                tosses the ball up to ride along, presents go down the
//                chimneys and the sleigh flies off across the moon. At
//                Halloween a witch on her broomstick drops candy instead;
//                an everyday show has a hot-air balloon delivering parcels
//   credits      rolling up the screen, with the lines from the settings
//   THE END      over a last burst of fireworks

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

class SleighStory : public ShowStory {
public:
    using RGB = ArcadeCanvas::RGB;

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        theme = s.theme();
        // Where the ball rides, from the top left of the sleigh's row sy:
        // Santa's lap, behind the witch on her broom, or in the basket.
        lapX = theme == ShowTheme::Halloween ? 4 : 3;
        lapY = theme == ShowTheme::Halloween ? 3 : theme == ShowTheme::Everyday ? 2 : 1;
        houses.clear();
        presents.clear();
        ballState = 0;
        ballLaunchMs = 0;
        paddleX = W * 0.28f;
        paddleW = std::max(4, W / 10);
        int x = 0;
        while (x < W) {
            const int w = 5 + (int)(s.rnd() * 4);
            // Leave room around the paddle.
            if (x + w > paddleX - paddleW && x < paddleX + paddleW) {
                x = (int)(paddleX + paddleW) + 1;
                continue;
            }
            if (x + w > W) {
                break;
            }
            const int h = std::min(H / 4, 3 + (int)(s.rnd() * 3));
            houses.push_back({x, w, h, x + w - 2, false, (unsigned)(s.rnd() * 4)});
            x += w + 1 + (int)(s.rnd() * 3);
        }
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override { s.drawNight(c, dt, k); }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        const float u = (float)(ms / len);
        s.drawNight(c, dt, k);

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
            if (sleighOn && sxf + lapX + speed * flight >= paddleX) {
                ballState = 1;
                ballLaunchMs = ms;
            }
        }
        if (ballState == 1) {
            const float f = std::clamp((float)((ms - ballLaunchMs) / 1000.0) / flight, 0.0f, 1.0f);
            bx = paddleX + (sxf + lapX - paddleX) * f;
            by = (py - 1) + (sy + lapY - (py - 1)) * f - std::sin(f * 3.1416f) * 4.0f;
            if (f >= 1.0f) {
                ballState = 2;
                s.burst(sxf + lapX, (float)sy + lapY, {1, 1, 1}, 12, true);
            }
        }

        // Presents (or candy) down the chimneys once the ball is aboard.
        if (sleighOn && ballState == 2) {
            for (size_t i = 0; i < houses.size(); ++i) {
                House &h = houses[i];
                if (!h.gotPresent && sx + lapX - 1 >= h.chimneyX && sx + lapX - 1 < h.chimneyX + 2 &&
                    std::none_of(presents.begin(), presents.end(), [&](const Present &p) { return p.house == (int)i; })) {
                    static const RGB COLS[] = {{1, 0.15f, 0.15f}, {0.2f, 1, 0.3f}, {1, 0.8f, 0.2f}, {0.4f, 0.6f, 1}};
                    static const RGB CANDY[] = {{1, 0.45f, 0}, {0.65f, 0.2f, 1}, {0.35f, 1, 0.15f}, {1, 0.85f, 0.25f}};
                    presents.push_back({(float)h.chimneyX, (float)sy + lapY + 3, (int)i,
                                        (theme == ShowTheme::Halloween ? CANDY : COLS)[i % 4]});
                }
            }
        }
        for (size_t i = 0; i < presents.size();) {
            Present &p = presents[i];
            p.y += std::max(6.0f, H * 0.5f) * dt;
            House &h = houses[p.house];
            if (p.y >= chimneyTop(h, H)) {
                h.gotPresent = true;
                s.burst(p.x, p.y, p.col, 8, false);
                presents.erase(presents.begin() + i);
            } else {
                ++i;
            }
        }

        // Windows flicker on and off now and then.
        if (s.rnd() < dt * 1.5f && !houses.empty()) {
            houses[(size_t)(s.rnd() * houses.size()) % houses.size()].windows ^= 1u << (int)(s.rnd() * 2);
        }

        drawTown(c, W, H, k);
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
            // A trail of gold sparkles behind, or the witch's purple and
            // green; the balloon has none.
            if (theme != ShowTheme::Everyday && s.rnd() < dt * 25.0f) {
                const RGB spark = theme == ShowTheme::Halloween ? (s.rnd() < 0.5f ? RGB{0.65f, 0.2f, 1} : RGB{0.35f, 1, 0.15f})
                                                                : RGB{1, 0.8f, 0.3f};
                s.sparks.push_back({sxf - 0.5f, sy + 3.0f + s.rnd(), -speed * 0.2f, s.rnd() * 2 - 1, 0, 0.5f + s.rnd() * 0.5f,
                                    spark, 30, 2.0f});
            }
            if (theme == ShowTheme::Halloween) {
                drawWitch(c, sx, sy, ms, ballState == 2, k);
            } else if (theme == ShowTheme::Everyday) {
                drawBalloon(c, sx, sy, ballState == 2, k);
            } else {
                drawSleigh(c, sx, sy, ms, ballState == 2, k);
            }
        }
        s.drawFireworks(c, k);   // the sparkles, and the bursts as presents land
    }

private:
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

    static int chimneyTop(const House &h, int H) { return H - 1 - h.h - 4; }

    void drawTown(ArcadeCanvas::Canvas &c, int W, int H, float k) {
        // Snow on the ground and the roofs; at Halloween dark purple roofs
        // edged orange over dead grass; everyday red roofs on grass.
        const bool hallow = theme == ShowTheme::Halloween, everyday = theme == ShowTheme::Everyday;
        const RGB ground = hallow ? RGB{0.35f, 0.22f, 0.05f} : everyday ? RGB{0.1f, 0.6f, 0.1f} : RGB{0.85f, 0.9f, 1.0f};
        const RGB roof = hallow ? RGB{0.35f, 0.08f, 0.45f} : RGB{0.6f, 0.08f, 0.06f};
        const RGB roofEdge = hallow ? RGB{1.0f, 0.45f, 0.0f} : everyday ? roof : RGB{0.85f, 0.9f, 1.0f};
        for (int x = 0; x < W; ++x) {
            c.set(x, H - 1, ground, 0.35f * k);
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
                const RGB warm = hallow ? RGB{1.0f, 0.45f, 0.0f} : RGB{1.0f, 0.7f, 0.25f};
                c.set(wx, top + 1, lit ? warm : RGB{0.05f, 0.05f, 0.1f}, (lit ? glow : 1.0f) * k);
            }
            // The roof, snowy (or orange) along the top edge, and a chimney.
            const int rows = (h.w + 1) / 2;
            for (int r = 1; r <= rows; ++r) {
                const int x0 = h.x + r - 1, x1 = h.x + h.w - r;
                for (int x = x0; x <= x1; ++x) {
                    const bool edge = x == x0 || x == x1;
                    c.set(x, top - r, edge ? roofEdge : roof, (edge && !everyday ? 0.5f : 0.55f) * k);
                }
            }
            // The chimney is the second column from the right, where the
            // roof is two rows deep; it stands two above that.
            c.set(h.chimneyX, top - 3, {0.45f, 0.4f, 0.4f}, 0.6f * k);
            c.set(h.chimneyX, top - 4, {0.45f, 0.4f, 0.4f}, 0.6f * k);
        }
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

    // The witch on her broomstick, facing right: a pointed hat, a green
    // face, a purple cloak, and the ball riding on the broom behind her,
    // where the cat would sit. 17 x 6, from the row above the sleigh's.
    void drawWitch(ArcadeCanvas::Canvas &c, int sx, int sy, double ms, bool withBall, float k) {
        static const char *ROWS[6] = {
            ".......P.........",
            "......PP.........",
            ".....PPPPP.......",
            "......GGC........",
            "YY....CCCC.......",
            "YYYBBBBBBBBBBB...",
        };
        const RGB HAT = {0.45f, 0.12f, 0.6f}, FACE = {0.35f, 0.9f, 0.2f}, CLOAK = {0.4f, 0.1f, 0.55f},
                  STRAW = {1.0f, 0.75f, 0.2f}, STICK = {0.55f, 0.3f, 0.1f};
        // Her cloak flaps.
        const bool flap = std::fmod(ms, 400.0) < 200.0;
        for (int y = 0; y < 6; ++y) {
            for (int x = 0; x < 17; ++x) {
                RGB col;
                float kk = 1;
                switch (ROWS[y][x]) {
                case 'P': col = HAT; break;
                case 'G': col = FACE; kk = 0.8f; break;
                case 'C': col = CLOAK; break;
                case 'Y': col = STRAW; kk = 0.7f; break;
                case 'B': col = STICK; kk = 0.8f; break;
                default: continue;
                }
                c.set(sx + x, sy - 1 + y, col, kk * k);
            }
        }
        c.set(sx + 5, sy + 3 - (flap ? 1 : 0), CLOAK, 0.7f * k);
        if (withBall) {
            c.set(sx + lapX, sy + lapY, {1, 1, 1}, k);
        }
    }

    // A striped hot-air balloon, its top on the row above the sleigh's and
    // the ball riding in the basket. 7 x 9.
    void drawBalloon(ArcadeCanvas::Canvas &c, int sx, int sy, bool withBall, float k) {
        static const char *ROWS[9] = {
            "..EEE..",
            ".EEEEE.",
            "EEEEEEE",
            "EEEEEEE",
            ".EEEEE.",
            "..EEE..",
            "..r.r..",
            "..B.B..",
            "..BBB..",
        };
        static const RGB STRIPE[3] = {{1, 0.2f, 0.15f}, {1, 0.8f, 0.2f}, {0.3f, 0.5f, 1}};
        const RGB ROPE = {0.5f, 0.45f, 0.4f}, BASKET = {0.6f, 0.35f, 0.12f};
        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 7; ++x) {
                switch (ROWS[y][x]) {
                case 'E': c.set(sx + x, sy - 5 + y, STRIPE[x % 3], 0.8f * k); break;
                case 'r': c.set(sx + x, sy - 5 + y, ROPE, 0.5f * k); break;
                case 'B': c.set(sx + x, sy - 5 + y, BASKET, 0.8f * k); break;
                default: break;
                }
            }
        }
        if (withBall) {
            c.set(sx + lapX, sy + lapY, {1, 1, 1}, k);
        }
    }

    std::vector<House> houses;
    std::vector<Present> presents;
    float paddleX = 0;
    int paddleW = 6;
    int ballState = 0;   // 0 on the paddle, 1 flying up, 2 in the sleigh
    double ballLaunchMs = 0;
    ShowTheme theme = ShowTheme::Christmas;
    int lapX = 3, lapY = 1;
};

class BreakoutCredits {
public:
    // thanks and extraLines: the SPECIAL THANKS and the closing lines of the
    // roll, from the settings. No thanks leaves that heading out.
    void start(int w, int h, int bricks, double playMs, const std::vector<std::string> &thanks,
               const std::vector<std::string> &extraLines, double lengthMs) {
        show.start(w, h, config(w, h, bricks, playMs, thanks, extraLines, lengthMs), std::make_unique<SleighStory>());
    }

    // For tools/shows/preview.sh.
    static void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
        cfg = config(60, 25, 412, 395000, {"SANTA", "RUDOLPH"}, {"THANKS FOR PLAYING", "MERRY CHRISTMAS"}, 34158);
        story = std::make_unique<SleighStory>();
    }

    static ShowConfig config(int w, int h, int bricks, double playMs, const std::vector<std::string> &thanks,
                             const std::vector<std::string> &extraLines, double lengthMs) {
        using namespace ShowColours;
        ShowConfig cfg;
        cfg.headline = {"YOU", "WON!"};
        cfg.stats = {{"BRICKS", bricks, false, GOLD}, {"TIME", (int)(playMs / 1000.0), true, GREEN}};
        cfg.ending = "THE END";
        cfg.lengthMs = lengthMs;
        cfg.pStats = 0.15;
        cfg.pStory = 0.27;
        cfg.pRoll = 0.62;
        cfg.pEnd = 0.90;
        const int roll = std::max(1, h / 40);
        const bool wide = (w + roll) / (4 * roll) >= 8;
        auto &r = cfg.roll;
        // The settings' lines alternate red and green, or orange and purple
        // at Halloween, or gold and blue.
        const bool hallow = cfg.theme == ShowTheme::Halloween, everyday = cfg.theme == ShowTheme::Everyday;
        const ArcadeCanvas::RGB one = hallow ? ArcadeCanvas::RGB{1.0f, 0.5f, 0.05f} : everyday ? GOLD : RED;
        const ArcadeCanvas::RGB two = hallow ? ArcadeCanvas::RGB{0.75f, 0.35f, 1.0f} : everyday ? ArcadeCanvas::RGB{0.45f, 0.65f, 1.0f} : GREEN;
        r.push_back({wide ? "BREAKOUT" : "BREAK OUT", HEAD});
        r.push_back({"", WHITE});
        r.push_back({"CAST", HEAD});
        r.push_back({"THE BALL", WHITE});
        r.push_back({"THE PADDLE", WHITE});
        r.push_back({"AND", HEAD});
        r.push_back({std::to_string(bricks) + " BRICKS", WHITE});
        if (!thanks.empty()) {
            r.push_back({"", WHITE});
            r.push_back({"SPECIAL THANKS", HEAD});
            int n = 0;
            for (const auto &l : thanks) {
                r.push_back({l, n++ % 2 ? two : one});
            }
        }
        if (!extraLines.empty()) {
            r.push_back({"", WHITE});
            int n = 0;
            for (const auto &l : extraLines) {
                r.push_back({l, n++ % 2 ? two : one});
            }
        }
        r.push_back({"", WHITE});
        r.push_back({"AND YOU!", HEAD});
        return cfg;
    }

    double elapsedMs() const { return show.elapsedMs(); }
    bool over() const { return show.over(); }
    void finish() { show.finish(); }
    void step(double dtMs, ArcadeCanvas::Canvas &c) { show.step(dtMs, c); }

private:
    ArcadeShow show;
};
