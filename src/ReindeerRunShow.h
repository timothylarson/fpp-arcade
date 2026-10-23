#pragma once
// Reindeer Run's end show (see ArcadeShow.h): NEW BEST! or GREAT RUN!, the
// score (and the best, when this wasn't it), then the reindeer's big moment on
// a snowy night - it gallops along, hopping a log and a present for the candy
// canes over them, while Santa's sleigh swoops in overhead. The reindeer
// sprints to catch up, leaps up to the head of the team, and the whole sleigh
// takes off past the moon in a trail of sparkles. Under the credits and GAME
// OVER the sleigh is a speck crossing the moon. The night is black, with a few
// dim stars and flakes: a lit sky only glows on the wall.
//
// After two players the headline names the winner, both scores count up in
// their reindeer's colours, and both join the team, the winner in the lead.
//
// The reindeer here are the game's own sprites, which the game draws from.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace ReindeerRunShow {

using RGB = ArcadeCanvas::RGB;

// A reindeer's colours. Rudolph (the directions) is dark brown with the red
// nose; Dasher (A and B) is a lighter gold with a green collar, so two players
// can tell theirs apart at a glance.
struct Look {
    RGB body, head, antler, tail, nose, legs, collar;
    bool glow;   // the nose lights the dark around it
};
const Look RUDOLPH = {{0.55f, 0.24f, 0.06f}, {0.62f, 0.3f, 0.08f}, {0.85f, 0.62f, 0.3f}, {0.85f, 0.75f, 0.55f},
                      {1.0f, 0.02f, 0.02f},  {0.38f, 0.16f, 0.04f}, {0.62f, 0.3f, 0.08f}, true};
const Look DASHER = {{0.8f, 0.52f, 0.16f}, {0.85f, 0.58f, 0.2f}, {0.95f, 0.85f, 0.6f}, {1.0f, 0.95f, 0.85f},
                     {0.35f, 0.12f, 0.04f}, {0.5f, 0.3f, 0.08f}, {0.1f, 0.9f, 0.2f}, false};
// The rest of Santa's team: plain brown, a little dimmer than the stars of the show.
const Look TEAM = {{0.42f, 0.22f, 0.08f}, {0.48f, 0.26f, 0.1f}, {0.7f, 0.55f, 0.3f}, {0.7f, 0.62f, 0.45f},
                   {0.3f, 0.12f, 0.05f},  {0.3f, 0.15f, 0.05f}, {0.48f, 0.26f, 0.1f}, false};
// What each player is called in the shows and their colour in the scores:
// Rudolph's red nose, Dasher's green collar.
const RGB RUDOLPH_TEXT = {1.0f, 0.3f, 0.25f};
const RGB DASHER_TEXT = {0.35f, 1.0f, 0.45f};

// The sprites, top row first, facing right: a for antler, h head, n nose,
// c the neck (Dasher's collar), b body, t the pale rump, l legs.
constexpr int RW = 7, RH = 4;   // standing
const char *const RUN[3][RH] = {
    {"...a.a.", "....ahn", "tbbbbc.", ".l..l.."},
    {"...a.a.", "....ahn", "tbbbbc.", "l....l."},
    {"...a.a.", "....ahn", "tbbbbc.", "..ll..."},
};
// Galloping: legs under, stretched out, under, gathered.
const int RUN_CYCLE[4] = {0, 1, 0, 2};
// In the air the legs stretch out fore and aft.
const char *const JUMP[RH] = {"...a.a.", "....ahn", "tbbbbc.", "l....l."};
// Ducking: head down and forward, antlers laid back, two rows tall.
constexpr int DW = 8, DH = 2;
const char *const DUCK[2][DH] = {
    {"tbbbbahn", ".l..l..."},
    {"tbbbbahn", "l....l.."},
};

inline RGB colourOf(const Look &look, char ch) {
    switch (ch) {
    case 'a': return look.antler;
    case 'h': return look.head;
    case 'n': return look.nose;
    case 'c': return look.collar;
    case 't': return look.tail;
    case 'l': return look.legs;
    default: return look.body;
    }
}

// Draws a sprite with its top-left at (x, y). A glowing nose puts a faint red
// halo round itself.
inline void drawSprite(ArcadeCanvas::Canvas &c, const char *const *rows, int nRows, int x, int y, const Look &look,
                       float k) {
    for (int r = 0; r < nRows; ++r) {
        for (int i = 0; rows[r][i]; ++i) {
            const char ch = rows[r][i];
            if (ch == '.') {
                continue;
            }
            c.set(x + i, y + r, colourOf(look, ch), k);
            if (ch == 'n' && look.glow) {
                const int dx[4] = {1, 0, 0, 1}, dy[4] = {0, -1, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    c.add(x + i + dx[d], y + r + dy[d], {1.0f, 0.05f, 0.02f}, 0.18f * k);
                }
            }
        }
    }
}

// Santa's sleigh, the same height as a reindeer: Santa's hat and beard, his
// sack, the red sleigh with its gold runner curling up at the front.
constexpr int SW = 7;
const char *const SLEIGH[RH] = {".R.....", ".Wgg...", "rrrrrry", ".yyyyy."};
inline void drawSleigh(ArcadeCanvas::Canvas &c, int x, int y, float k) {
    for (int r = 0; r < RH; ++r) {
        for (int i = 0; i < SW; ++i) {
            RGB col;
            switch (SLEIGH[r][i]) {
            case 'R': col = {1.0f, 0.05f, 0.05f}; break;
            case 'W': col = {1.0f, 0.95f, 0.9f}; break;
            case 'g': col = {0.1f, 0.6f, 0.15f}; break;
            case 'r': col = {0.85f, 0.05f, 0.05f}; break;
            case 'y': col = {1.0f, 0.75f, 0.15f}; break;
            default: continue;
            }
            c.set(x + i, y + r, col, k);
        }
    }
}

// The candy cane the game's runners collect, hook at the top.
const char *const CANDY[3] = {".r.", "w.w", "r.."};
inline void drawCandy(ArcadeCanvas::Canvas &c, int x, int y, float k) {
    for (int r = 0; r < 3; ++r) {
        for (int i = 0; i < 3; ++i) {
            if (CANDY[r][i] != '.') {
                c.set(x + i, y + r, CANDY[r][i] == 'r' ? RGB{1.0f, 0.05f, 0.05f} : RGB{1, 1, 1}, k);
            }
        }
    }
}

class Story : public ShowStory {
public:
    // dasherLeads: the winner (or the only player) is Dasher, not Rudolph.
    // two: the other reindeer runs too, and joins the team behind the winner.
    explicit Story(bool dasherLeads = false, bool twoReindeer = false) : dasher(dasherLeads), two(twoReindeer) {}

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        feet = H - 3;   // two rows of snow below
        scroll = 0;
        runX = W / 4.0f;
        for (auto &r : runners) {
            r = Runner();
        }
        // A log and a present to hop, each with a candy cane over it, timed to
        // reach the runner a third and two thirds of the way to the sleigh.
        const float v = speed(W);
        hurdles = {{runX + RW + v * 1.4f, 0, false}, {runX + RW + v * 2.9f, 1, false}};
        moonX = W * 0.74f;
        moonY = std::max(3.0f, H * 0.28f);
        stars.clear();
        for (int i = 0; i < std::max(5, W * H / 110); ++i) {
            stars.push_back({s.rnd() * W, s.rnd() * (H - 5), s.rnd() * 6.28f});
        }
        flakes.clear();
        for (int i = 0; i < std::max(5, W * H / 90); ++i) {
            flakes.push_back({s.rnd() * W, s.rnd() * H, 2.0f + s.rnd() * 2.5f});
        }
        sleighT = -1;
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        night(s, c, dt, k);
        drawGround(s, c, k);
        // The sleigh, now a speck: Santa and a line of reindeer crossing the
        // moon, the first one's nose red.
        sleighT = sleighT < 0 ? 0 : sleighT + dt;
        const float x = -8.0f + std::fmod(sleighT, 14.0f) / 14.0f * (s.W + 16);
        const int y = (int)std::lround(moonY - 0.5f + std::sin(x * 0.2f) * 0.6f);
        c.set((int)x, y, {1.0f, 0.15f, 0.1f}, 0.8f * k);
        for (int i = 1; i <= 3; ++i) {
            c.set((int)x + 2 * i, y, {0.55f, 0.3f, 0.12f}, 0.8f * k);
        }
        c.set((int)x + 8, y, {1.0f, 0.02f, 0.02f}, k);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        const float u = (float)(ms / len);
        night(s, c, dt, k);

        // The ground runs by until the leap, then slows to a stop.
        const float v = speed(W) * (1.0f - std::clamp((u - 0.5f) / 0.12f, 0.0f, 1.0f));
        scroll += v * dt;
        drawGround(s, c, k);

        // The team: the sleigh, one reindeer (two with one player), then a
        // place in the lead for each runner. Members are 9 LEDs apart.
        const int team = two ? 1 : 2;
        const float leadSlot = (team + (two ? 2 : 1)) * 9.0f;   // from the sleigh's left
        const float catchX = std::min(W * 0.55f, W - RW - 2.0f);
        const float skyY = std::max(1.0f, H * 0.18f);   // the team's top row
        float tx, ty = skyY;
        if (u < 0.22f) {
            tx = -leadSlot - RW - 40.0f;   // not here yet
        } else if (u < 0.48f) {
            // Swooping in from the left to fly over the runner, easing to its pace.
            const float e = 1.0f - std::pow(1.0f - (u - 0.22f) / 0.26f, 3.0f);
            const float from = -leadSlot - RW - 4.0f, to = catchX - leadSlot;
            tx = from + e * (to - from);
            ty = skyY - 3.0f * std::sin(e * 3.14159f);   // a swoop down and up again
        } else if (u < 0.66f) {
            tx = catchX - leadSlot;
        } else {
            // Off past the moon: up and away, faster and faster.
            const float f = (u - 0.66f) / 0.3f;
            tx = catchX - leadSlot + f * f * (W + leadSlot + 30.0f);
            ty = skyY - f * 3.0f;
        }

        // Each runner: along the ground hopping the hurdles, sprinting to get
        // under its place in the team, then the leap up into it.
        for (int i = 0; i < (two ? 2 : 1); ++i) {
            Runner &r = runners[i];
            // The winner (0) runs in front and takes the lead; the other
            // follows a team place behind.
            const float slotX = tx + leadSlot - i * 9.0f;
            const float homeX = runX - i * 9.0f;
            const float sprint = std::clamp((u - 0.3f) / 0.18f, 0.0f, 1.0f);
            const float groundX = homeX + (catchX - i * 9.0f - homeX) * sprint * sprint * (3 - 2 * sprint);
            const float leapAt = 0.5f + i * 0.04f;
            if (u < leapAt) {
                r.x = groundX;
                // Hop each hurdle as it comes, gathering its candy cane.
                for (auto &hd : hurdles) {
                    const float d = hd.x - scroll - (r.x + RW - 2);
                    if (!r.air && d > 0 && d < v * 0.3f) {
                        r.air = true;
                        r.vy = 21.0f;
                    }
                    const float cx = hd.x - scroll + 1, cy = feet - 5.0f;
                    if (!hd.taken && r.air && std::fabs(cx - (r.x + 4)) < 2.5f && feet - r.h - RH < cy + 2) {
                        hd.taken = true;
                        s.burst(cx, cy, {1, 0.1f, 0.1f}, 8, false);
                        s.burst(cx, cy, {1, 1, 1}, 8, false);
                    }
                }
                if (r.air) {
                    r.vy -= 70.0f * dt;
                    r.h += r.vy * dt;
                    if (r.h <= 0) {
                        r.h = 0;
                        r.air = false;
                    }
                }
                r.y = feet - r.h;
            } else {
                // Up in an arc from where it left the ground to its place,
                // trailing gold, a little above it at the top.
                if (r.leapFromY == 0) {
                    r.leapFromX = r.x;
                    r.leapFromY = feet - r.h;
                    s.burst(r.x + 3, feet, {1, 1, 1}, 14, false);
                }
                const float f = std::clamp((u - leapAt) / 0.09f, 0.0f, 1.0f);
                const float toY = ty + RH - 1;
                r.x = r.leapFromX + (slotX - r.leapFromX) * f;
                r.y = r.leapFromY + (toY - r.leapFromY) * f - std::sin(f * 3.14159f) * 4.0f;
                if (f >= 1.0f) {
                    r.x = slotX;
                    r.y = toY;
                }
                if (s.rnd() < dt * 40.0f && r.x < W + 2) {
                    s.sparks.push_back({r.x + s.rnd() * 3, r.y - s.rnd() * 3, -3.0f, s.rnd() * 2 - 1, 0,
                                        0.4f + s.rnd() * 0.5f, {1, 0.8f, 0.3f}, 30, 2.0f});
                }
            }
        }

        // The hurdles and their candy canes.
        for (const auto &hd : hurdles) {
            const int hx = (int)std::floor(hd.x - scroll);
            if (hx < -4 || hx > W) {
                continue;
            }
            if (hd.kind == 0) {
                for (int dy = 0; dy < 2; ++dy) {
                    c.set(hx, feet - dy, {0.85f, 0.6f, 0.3f}, k);
                    for (int dx = 1; dx < 4; ++dx) {
                        c.set(hx + dx, feet - dy, (dy == 1 && dx % 2) ? RGB{0.7f, 0.7f, 0.75f} : RGB{0.45f, 0.2f, 0.05f}, k);
                    }
                }
            } else {
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 3; ++dx) {
                        c.set(hx + dx, feet - dy, dx == 1 ? RGB{1, 0.8f, 0.1f} : RGB{0.1f, 0.75f, 0.15f}, k);
                    }
                }
                c.set(hx + 1, feet - 2, {1, 0.8f, 0.1f}, k);
            }
            if (!hd.taken) {
                drawCandy(c, hx, feet - 6, k);
            }
        }

        // The team, with gold reins from one to the next along the bottom row.
        const int frame = RUN_CYCLE[(int)(ms / 90.0) % 4];
        const int top = (int)std::lround(ty);
        if (tx > -leadSlot - RW - 20) {
            // The reins reach as far as the team goes: to the front once the
            // runners are up in their places.
            const float reach = u >= 0.5f + (two ? 0.04f : 0.0f) + 0.09f ? leadSlot : team * 9.0f;
            for (int x = (int)tx + SW; x < (int)(tx + reach); x += 2) {
                c.add(x, top + RH - 2, {1.0f, 0.75f, 0.15f}, 0.35f * k);
            }
            drawSleigh(c, (int)tx, top, k);
            for (int m = 1; m <= team; ++m) {
                drawSprite(c, RUN[(frame + m) % 3], RH, (int)tx + 9 * m, top, TEAM, 0.8f * k);
            }
            // A trail of sparkles behind the sleigh once it's away.
            if (u > 0.66f && s.rnd() < dt * 50.0f) {
                s.sparks.push_back({tx + s.rnd() * 2, ty + RH - 1 + s.rnd(), -6.0f, s.rnd() * 2, 0, 0.6f + s.rnd() * 0.6f,
                                    ArcadeShow::hue(s.rnd()), 25, 1.5f});
            }
        }
        for (int i = (two ? 1 : 0); i >= 0; --i) {
            const Runner &r = runners[i];
            const Look &look = (i == 0) == !dasher ? RUDOLPH : DASHER;
            const bool flying = u >= 0.5f + i * 0.04f;
            const char *const *rows = flying ? RUN[frame] : (r.air ? JUMP : RUN[RUN_CYCLE[(int)(r.x + scroll) / 2 % 4]]);
            drawSprite(c, rows, RH, (int)std::lround(r.x), (int)std::lround(r.y) - (RH - 1), look, k);
        }
        // Off the edge: a burst of every colour where they went.
        if (!gone && tx > W + 1) {
            gone = true;
            s.burst(W * 0.85f, std::max(2.0f, ty), {1, 0.85f, 0.3f}, 40, true);
        }
        s.drawFireworks(c, k);
    }

private:
    struct Runner {
        float x = 0, y = 0, h = 0, vy = 0;
        bool air = false;
        float leapFromX = 0, leapFromY = 0;
    };
    struct Dot {
        float x, y, v;   // v: a star's twinkle phase, a flake's fall speed
    };
    struct Hurdle {
        float x;   // in the world; on screen at x - scroll
        int kind;  // 0 a log, 1 a present
        bool taken;
    };

    static float speed(int W) { return std::max(12.0f, W * 0.35f); }

    // The night: black, a few dim twinkling stars, a small dim moon and
    // sparse falling snow.
    void night(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) {
        for (const auto &st : stars) {
            c.add((int)st.x, (int)st.y, {0.85f, 0.9f, 1.0f}, (0.08f + 0.06f * std::sin((float)(s.t / 400.0) + st.v)) * k);
        }
        const float r = std::min(3.0f, s.H / 8.0f);
        for (int y = (int)(moonY - r - 1); y <= (int)(moonY + r + 1); ++y) {
            for (int x = (int)(moonX - r - 1); x <= (int)(moonX + r + 1); ++x) {
                const float in = ArcadeCanvas::clamp01(r - std::hypot(x + 0.5f - moonX, y + 0.5f - moonY) + 0.5f);
                c.add(x, y, {1.0f, 0.93f, 0.7f}, 0.16f * in * k);
            }
        }
        for (auto &f : flakes) {
            f.y += f.v * dt;
            if (f.y >= s.H) {
                f.y -= s.H + 1;
                f.x = s.rnd() * s.W;
            }
            c.add((int)f.x, (int)f.y, {1, 1, 1}, 0.12f * k);
        }
    }

    // Snow along the bottom, a speck in it now and then going by with the scroll.
    void drawGround(ArcadeShow &s, ArcadeCanvas::Canvas &c, float k) {
        for (int y = feet + 1; y < s.H; ++y) {
            for (int x = 0; x < s.W; ++x) {
                const unsigned hsh = (unsigned)(x + (int)scroll) * 2654435761u;
                const float spark = y == feet + 1 && (hsh >> 27) == 0 ? 0.3f : 0.0f;
                c.add(x, y, {0.75f, 0.82f, 1.0f}, (y == feet + 1 ? 0.14f : 0.012f) * k + spark * k);
            }
        }
    }

    bool dasher = false, two = false, gone = false;
    int feet = 22;
    float scroll = 0, runX = 15, sleighT = -1;
    Runner runners[2];
    std::vector<Hurdle> hurdles;
    std::vector<Dot> stars, flakes;
    float moonX = 44, moonY = 7;
};

// The credits' title: REINDEER is too long for a line on a 30-wide half,
// where the roll would break it as REINDE-ER.
inline std::vector<std::pair<std::string, RGB>> title(int W) {
    using namespace ShowColours;
    if (W >= 32) {
        return {{"REINDEER RUN", HEAD}};
    }
    return {{"REIN-", HEAD}, {"DEER RUN", HEAD}};
}

// score: the run's score, candy the candy canes in it; dasher if Dasher ran
// it; W the model's width.
inline ShowConfig config(int score, int candy, bool dasher, bool newBest, int previousBest, int W) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "RUN!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    if (candy > 0) {
        cfg.stats.push_back({"CANDY", candy, false, RED});
    }
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = title(W);
    cfg.roll.insert(cfg.roll.end(), {{"", WHITE}, {"CAST", HEAD},
                                     {dasher ? "DASHER" : "RUDOLPH", dasher ? DASHER_TEXT : RUDOLPH_TEXT}, {"SANTA", RED},
                                     {"AND THE SLEIGH TEAM", GOLD}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}});
    cfg.ending = "GAME OVER";
    return cfg;
}

// After two players: top is Rudolph's score (the directions, the top lane),
// bottom Dasher's (A and B). The winner gets the headline; the better score
// is the one that counts as a best. W is the model's width.
inline ShowConfig config2(int top, int bottom, bool newBest, int previousBest, int W) {
    using namespace ShowColours;
    ShowConfig cfg;
    if (top == bottom) {
        cfg.headline = {"IT'S A", "TIE!"};
    } else {
        cfg.headline = {top > bottom ? "TOP" : "BOTTOM", "WINS!"};
    }
    cfg.stats.push_back({"TOP", top, false, RUDOLPH_TEXT});
    cfg.stats.push_back({"BOTTOM", bottom, false, DASHER_TEXT});
    if (newBest) {
        cfg.stats.push_back({"NEW BEST", std::max(top, bottom), false, GOLD});
    } else if (previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = title(W);
    cfg.roll.insert(cfg.roll.end(), {{"", WHITE}, {"CAST", HEAD}, {"TOP: RUDOLPH", RUDOLPH_TEXT},
                                     {"BOTTOM: DASHER", DASHER_TEXT}, {"SANTA", RED}, {"AND THE SLEIGH TEAM", GOLD},
                                     {"", WHITE}, {"THANKS FOR PLAYING", WHITE}});
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config2(812, 640, true, 700, 60);
    story = std::make_unique<Story>(false, true);
}

}  // namespace ReindeerRunShow
