#pragma once
// Sky Jump's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the
// score and the height (and the best, when this wasn't it), then the elf's
// last flight - it stands on the snow by the town, fires up its rocket pack
// and blasts off, collecting a trail of coins as the stars stream by and the
// moon slides past. Santa's sleigh crosses overhead, and the elf climbs on
// to the North Star, which flashes and bursts into fireworks as it arrives
// and then shines with the elf beside it behind the credits and GAME OVER.
//
// After two players the headline names the winner, both scores count up in
// their elves' colours, and both elves fly: the winner leads, and gets
// closest to the star.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace SkyJumpShow {

using RGB = ArcadeCanvas::RGB;
const RGB GREEN_SUIT = {0.1f, 0.95f, 0.2f}, GREEN_HAT = {1.0f, 0.1f, 0.05f};
const RGB BLUE_SUIT = {0.2f, 0.55f, 1.0f}, BLUE_HAT = {1.0f, 1.0f, 1.0f};
const RGB COIN = {1.0f, 0.72f, 0.08f};

class Story : public ShowStory {
public:
    // twoElves after a race; leftWon says whose elf leads (the green one).
    explicit Story(bool twoElves = false, bool leftWon = true) : two(twoElves), greenLeads(leftWon) {}

    void begin(ArcadeShow &s) override {
        theme = s.theme();
        const int W = s.W, H = s.H;
        anchor = (H - 1) * 0.4f;
        climb = std::max(90.0f, H * 4.5f);
        // The star ends up a few rows from the top, over the elf.
        starRow = std::max(4, H / 5);
        starH = 1 + climb + (H - 1 - anchor) - starRow;
        coins.clear();
        for (float h = 8; h < climb - 6; h += 5.0f) {
            coins.push_back({pathX(W, h), h, false});
        }
        streaks.clear();
        for (int i = 0; i < std::max(8, W * H / 75); ++i) {
            streaks.push_back({s.rnd() * W, s.rnd() * H, s.rnd() * 6.28f});
        }
        flashed = false;
        launched = false;
        lastCam = 0;
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        drawSky(s, c, dt, s.t, 0.0f, 1.0f, k);
        drawMoon(c, s.W * 0.75f, s.H * 0.55f, std::max(2.5f, s.H / 7.0f), k);
        // Up with the North Star: it shines, and the elf (or both) beside it.
        const int cx = s.W / 2, cy = starRow;
        drawStar(c, cx, cy, s.t, 1.0f, k);
        perch(s, c, cx, cy, s.t, k);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        const float u = (float)(ms / len);
        // 0-12%: on the snow; 12-78%: the climb, easing in and out; then
        // the arrival at the star.
        const float v = std::clamp((u - 0.12f) / 0.66f, 0.0f, 1.0f);
        const float e = v * v * (3 - 2 * v);
        const float lead = 1.0f + climb * e;
        const float cam = std::max(0.0f, lead - anchor);
        const float camV = (cam - lastCam) / std::max(0.001f, dt);
        lastCam = cam;
        if (!launched && u >= 0.12f) {
            launched = true;
            s.burst(W / 2.0f, (float)(H - 2), {1, 1, 1}, 16, false);
        }

        // The stars stream down past as it climbs, coming out as it goes.
        drawSky(s, c, dt, ms, camV, 0.3f + 0.7f * std::clamp(cam / climb, 0.0f, 1.0f), k);
        // The moon slides by halfway up.
        drawMoon(c, W * 0.75f, -8.0f + cam * 0.35f, std::max(2.5f, H / 7.0f), k);

        // The snow and the town, until they drop away.
        const int ground = (int)std::lround(H - 1 + cam);
        if (ground - 5 < H) {
            for (int y = std::max(0, ground); y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    // Snow; dark ground at Halloween; grass otherwise.
                    const RGB g = theme == ShowTheme::Halloween ? RGB{0.45f, 0.2f, 0.5f}
                                : theme == ShowTheme::Everyday ? RGB{0.15f, 0.7f, 0.15f} : RGB{0.85f, 0.9f, 1.0f};
                    c.add(x, y, g, (y == ground ? 0.35f : theme == ShowTheme::Christmas ? 0.12f : 0.06f) * k);
                }
            }
            house(c, 3, ground, k);
            house(c, W - 9, ground, k);
            if (theme == ShowTheme::Halloween) {
                ShowArt::pumpkin(c, W / 4 - 2, ground - 5, k);
                ShowArt::pumpkin(c, W * 3 / 4 - 2, ground - 5, k);
            } else {
                tree(c, W / 4, ground, ms, k);
                tree(c, W * 3 / 4, ground, ms, k);
            }
        }

        // The coins on the way, each gone in a sparkle as the elf gets it.
        for (auto &cn : coins) {
            if (!cn.got && lead >= cn.h) {
                cn.got = true;
                s.burst(cn.x, H - 1 - (cn.h - cam), COIN, 6, false);
            }
            if (!cn.got) {
                const int y = (int)std::lround(H - 1 - (cn.h - cam));
                c.add((int)std::lround(cn.x), y, COIN, 0.85f * k);
            }
        }

        // Santa crosses overhead, in front of the moon, late in the climb.
        const float su = (u - 0.42f) / 0.4f;
        if (su > 0 && su < 1) {
            drawSleigh(c, (int)std::lround(-18 + su * (W + 22)), std::max(1, H / 8), ms, k);
        }

        // The North Star, coming into view at the top.
        const float sx = W / 2.0f, sy = H - 1 - (starH - cam);
        if (u >= 0.8f && !flashed) {
            flashed = true;
            s.burst(sx, sy, {1, 1, 1}, 40, true);
            s.burst(sx, sy, {1, 0.85f, 0.3f}, 30, false);
        }
        const float glow = flashed ? 1.0f + 1.5f * std::max(0.0f, 1.0f - (u - 0.8f) / 0.06f) : 1.0f;
        drawStar(c, (int)std::lround(sx), (int)std::lround(sy), ms, glow, k);

        // The elves: the lead one, and in a race the other just behind it.
        // Once arrived they move in beside the star.
        const float arrive = std::clamp((u - 0.78f) / 0.08f, 0.0f, 1.0f);
        const RGB leadSuit = greenLeads ? GREEN_SUIT : BLUE_SUIT, leadHat = greenLeads ? GREEN_HAT : BLUE_HAT;
        const RGB backSuit = greenLeads ? BLUE_SUIT : GREEN_SUIT, backHat = greenLeads ? BLUE_HAT : GREEN_HAT;
        if (two) {
            // A little behind, and arriving a moment later.
            const float late = std::clamp((u - 0.82f) / 0.08f, 0.0f, 1.0f);
            const float h2 = std::max(1.0f, lead - 3.0f);
            const float x2 = pathX(W, h2) + 4 + late * (sx + 4 - pathX(W, h2) - 4);
            const float y2 = H - 1 - (h2 - cam);
            elf(c, x2, y2 + late * (sy + 4 - y2), backSuit, backHat, u >= 0.12f && late < 1, ms, k);
        }
        // The climb is over, so the camera holds and the elf flies up the
        // screen to the star's side.
        const float x1 = pathX(W, lead) + arrive * (sx - 3 - pathX(W, lead));
        const float y1 = H - 1 - (lead - cam);
        elf(c, x1, y1 + arrive * (sy + 3 - y1), leadSuit, leadHat, u >= 0.12f && arrive < 1, ms, k);
        s.drawFireworks(c, k);
    }

private:
    struct Coin {
        float x, h;
        bool got;
    };
    struct Streak {
        float x, y, phase;
    };

    // Black sky with sparse, dim stars - a lit sky washes out on the wall -
    // falling past at the camera's speed (streaking when it's fast).
    void drawSky(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, float camV, float out, float k) {
        for (auto &st : streaks) {
            st.y += (0.5f + camV * 0.35f) * dt;
            if (st.y >= s.H) {
                st.y -= s.H;
                st.x = s.rnd() * s.W;
            }
            const float tw = 0.5f + 0.5f * std::sin((float)ms / 300.0f + st.phase);
            c.add((int)st.x, (int)st.y, {0.9f, 0.9f, 1.0f}, (0.04f + 0.1f * tw) * out * k);
            if (camV > 25.0f) {
                c.add((int)st.x, (int)st.y - 1, {0.9f, 0.9f, 1.0f}, 0.04f * k);
            }
        }
    }

    // The elf's way up: a lazy weave across the middle, through the coins.
    static float pathX(int W, float h) { return W / 2.0f + W * 0.18f * std::sin(h * 0.07f); }

    // The game's elf: the hat's tip, the face and the suit with its arms out,
    // the rocket pack's flame under it.
    void elf(ArcadeCanvas::Canvas &c, float fx, float fy, const RGB &suit, const RGB &hat, bool flame, double ms, float k) {
        const int x = (int)std::lround(fx), y = (int)std::lround(fy);
        if (flame) {
            const float f = 0.6f + 0.4f * (float)(std::fmod(ms, 120.0) < 60.0);
            c.set(x, y + 1, {1.0f, 0.75f, 0.3f}, f * k);
            c.add(x, y + 2, {1.0f, 0.5f, 0.05f}, 0.5f * f * k);
        }
        c.set(x - 1, y, suit, 0.8f * k);
        c.set(x, y, suit, k);
        c.set(x + 1, y, suit, 0.8f * k);
        c.set(x, y - 1, {1.0f, 0.62f, 0.4f}, 0.9f * k);
        c.set(x, y - 2, hat, k);
    }

    // After the arrival: the elves beside the star, bobbing.
    void perch(ArcadeShow &s, ArcadeCanvas::Canvas &c, int cx, int cy, double ms, float k) {
        const float bob = std::sin((float)ms / 250.0f) > 0 ? 0.0f : 1.0f;
        const RGB leadSuit = greenLeads ? GREEN_SUIT : BLUE_SUIT, leadHat = greenLeads ? GREEN_HAT : BLUE_HAT;
        const RGB backSuit = greenLeads ? BLUE_SUIT : GREEN_SUIT, backHat = greenLeads ? BLUE_HAT : GREEN_HAT;
        elf(c, cx - 3.0f, cy + 3 + bob, leadSuit, leadHat, false, ms, k);
        if (two) {
            elf(c, cx + 4.0f, cy + 4 - bob, backSuit, backHat, false, ms, k);
        }
        (void)s;
    }

    // A big twinkling star: a white heart with long golden rays up, down and
    // across, short ones on the diagonals, pulsing in turn.
    void drawStar(ArcadeCanvas::Canvas &c, int cx, int cy, double ms, float glow, float k) {
        const float tw = 0.5f + 0.5f * std::sin((float)ms / 220.0f);
        const RGB GOLD = {1.0f, 0.85f, 0.35f};
        c.set(cx, cy, {1, 1, 1}, k);
        for (int d = 1; d <= 4; ++d) {
            const float f = std::min(1.0f, glow * (0.95f - 0.2f * d) * (0.75f + 0.25f * tw));
            if (f <= 0) {
                continue;
            }
            c.add(cx + d, cy, GOLD, f * k);
            c.add(cx - d, cy, GOLD, f * k);
            c.add(cx, cy + d, GOLD, f * k);
            c.add(cx, cy - d, GOLD, f * k);
        }
        for (int d = 1; d <= 2; ++d) {
            const float f = std::min(1.0f, glow * (0.7f - 0.25f * d) * (1.0f - 0.5f * tw));
            c.add(cx + d, cy + d, GOLD, f * k);
            c.add(cx - d, cy + d, GOLD, f * k);
            c.add(cx + d, cy - d, GOLD, f * k);
            c.add(cx - d, cy - d, GOLD, f * k);
        }
    }

    void drawMoon(ArcadeCanvas::Canvas &c, float mx, float my, float r, float k) {
        for (int y = (int)(my - r - 1); y <= (int)(my + r + 1); ++y) {
            for (int x = (int)(mx - r - 1); x <= (int)(mx + r + 1); ++x) {
                const float d = std::hypot(x + 0.5f - mx, y + 0.5f - my);
                const float in = ArcadeCanvas::clamp01(r - d + 0.5f);
                const float spot = std::hypot(x + 0.5f - (mx - r * 0.3f), y + 0.5f - (my - r * 0.2f)) < r * 0.3f ? 0.75f : 1.0f;
                c.add(x, y, theme == ShowTheme::Halloween ? RGB{1.0f, 0.5f, 0.08f} : RGB{1.0f, 0.93f, 0.7f},
                      (theme == ShowTheme::Halloween ? 0.5f : 0.22f) * in * spot * k);
            }
        }
    }

    void house(ArcadeCanvas::Canvas &c, int x, int ground, float k) {
        const RGB WALL = {0.6f, 0.35f, 0.2f}, ROOF = theme == ShowTheme::Halloween ? RGB{0.45f, 0.15f, 0.55f} : RGB{0.75f, 0.1f, 0.08f},
                  WINDOW = theme == ShowTheme::Halloween ? RGB{1.0f, 0.45f, 0.0f} : RGB{1.0f, 0.8f, 0.3f};
        for (int dx = 1; dx < 5; ++dx) c.add(x + dx, ground - 4, ROOF, 0.45f * k);
        for (int dx = 0; dx < 6; ++dx) c.add(x + dx, ground - 3, ROOF, 0.45f * k);
        for (int dx = 0; dx < 6; ++dx) {
            for (int y = ground - 2; y < ground; ++y) {
                const bool window = (dx == 1 || dx == 4) && y == ground - 2;
                c.add(x + dx, y, window ? WINDOW : WALL, (window ? 0.7f : 0.3f) * k);
            }
        }
    }

    void tree(ArcadeCanvas::Canvas &c, int x, int ground, double ms, float k) {
        const RGB TREE = {0.05f, 0.6f, 0.15f};
        c.add(x, ground - 4, TREE, 0.45f * k);
        for (int dx = -1; dx <= 1; ++dx) c.add(x + dx, ground - 3, TREE, 0.45f * k);
        for (int dx = -2; dx <= 2; ++dx) c.add(x + dx, ground - 2, TREE, 0.4f * k);
        c.add(x, ground - 1, {0.45f, 0.25f, 0.1f}, 0.4f * k);
        c.add(x, ground - 5, COIN, (std::fmod(ms, 1000.0) < 500.0 ? 0.8f : 0.4f) * k);
    }

    // Santa's sleigh, facing right: Santa, the sleigh's runner, and two
    // reindeer on a harness, the lead one with a red nose. 14 x 4.
    // At Halloween a cartoon witch on her broomstick instead, cape flapping;
    // on an everyday show a bunch of balloons drifting by.
    void drawSleigh(ArcadeCanvas::Canvas &c, int sx, int sy, double ms, float k) {
        if (theme == ShowTheme::Halloween) {
            static const char *const W0[] = {"...h........", "..hh........", ".hhhhh......", "..gcc.......", "bbbbbbbbyyy."};
            static const char *const W1[] = {"...h........", "..hh........", ".hhhhh......", "..gccc......", "bbbbbbbbyyy."};
            ShowArt::sprite(c, std::fmod(ms, 400.0) < 200.0 ? W0 : W1, 5, sx + 2, sy, {{'h', ShowArt::HAT},
                            {'g', {0.35f, 0.9f, 0.2f}}, {'c', {0.5f, 0.1f, 0.6f}}, {'b', {0.55f, 0.28f, 0.1f}},
                            {'y', ShowArt::CANDLE}}, 0.9f * k, true);
            return;
        }
        if (theme == ShowTheme::Everyday) {
            ShowArt::balloon(c, sx + 4, sy, {1, 0.2f, 0.2f}, 0.9f * k);
            ShowArt::balloon(c, sx + 7, sy - 1, {0.2f, 0.6f, 1}, 0.9f * k);
            ShowArt::balloon(c, sx + 10, sy + 1, {1, 0.85f, 0.1f}, 0.9f * k);
            return;
        }
        static const char *ROWS[4] = {
            ".RW.......A..A",
            "RKW.....B..B.N",
            "RRRRHHHBBHBB..",
            "GGGGG.........",
        };
        const RGB RED = {1, 0.1f, 0.08f}, WHITE = {1, 1, 1}, SKIN = {1, 0.6f, 0.45f}, GOLD = {1, 0.75f, 0.15f},
                  BROWN = {0.55f, 0.28f, 0.1f}, ANTLER = {0.8f, 0.6f, 0.35f}, HARNESS = {0.7f, 0.5f, 0.1f};
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 14; ++x) {
                RGB col;
                float kk = 0.9f;
                switch (ROWS[y][x]) {
                case 'R': col = RED; break;
                case 'W': col = WHITE; break;
                case 'K': col = SKIN; break;
                case 'G': col = GOLD; kk = 0.7f; break;
                case 'B': col = BROWN; break;
                case 'A': col = ANTLER; kk = 0.7f; break;
                case 'H': col = HARNESS; kk = 0.5f; break;
                case 'N': col = RED; kk = 0.5f + 0.5f * (float)(std::fmod(ms, 600.0) < 300.0); break;   // Rudolph
                default: continue;
                }
                c.set(sx + x, sy + y, col, kk * k);
            }
        }
        // Legs, galloping.
        const bool step = std::fmod(ms, 300.0) < 150.0;
        for (int deer : {8, 11}) {
            c.set(sx + deer + (step ? 0 : -1), sy + 3, BROWN, 0.8f * k);
            c.set(sx + deer + (step ? 1 : 0), sy + 3, BROWN, 0.8f * k);
        }
    }

    ShowTheme theme = ShowTheme::Christmas;
    bool two = false, greenLeads = true;
    float anchor = 9, climb = 110, starH = 130, lastCam = 0;
    int starRow = 5;
    bool flashed = false, launched = false;
    std::vector<Coin> coins;
    std::vector<Streak> streaks;
};

// Who crosses the sky, for the roll.
inline std::string santa(ShowTheme t) {
    return t == ShowTheme::Halloween ? "THE WITCH" : t == ShowTheme::Everyday ? "THE BALLOONS" : "SANTA";
}

inline void timing(ShowConfig &cfg) {
    // A longer story than most: the climb needs room.
    cfg.pStats = 0.12;
    cfg.pStory = 0.30;
    cfg.pRoll = 0.76;
    cfg.pEnd = 0.91;
}

// score: the game's score; height the rows climbed; coins collected;
// previousBest the best before this game.
inline ShowConfig config(int score, int height, int coins, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    timing(cfg);
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    cfg.stats.push_back({"HEIGHT", height, false, {0.3f, 0.85f, 1.0f}});
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"SKY JUMP", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE ELF", GREEN}, {santa(cfg.theme), RED},
                {"THE NORTH STAR", GOLD}, {"AND", HEAD}, {std::to_string(coins) + " COINS", GOLD}, {"", WHITE},
                {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// After two players: left and right are each side's score. The winner gets
// the headline; the better score is the one that counts as a best.
inline ShowConfig config2(int left, int right, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    timing(cfg);
    if (left == right) {
        cfg.headline = {"IT'S A", "TIE!"};
    } else {
        cfg.headline = {left > right ? "LEFT" : "RIGHT", "WINS!"};
    }
    cfg.stats.push_back({"LEFT", left, false, GREEN_SUIT});
    cfg.stats.push_back({"RIGHT", right, false, BLUE_SUIT});
    if (newBest) {
        cfg.stats.push_back({"NEW BEST", std::max(left, right), false, GOLD});
    } else if (previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"SKY JUMP", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE GREEN ELF", GREEN_SUIT},
                {"THE BLUE ELF", BLUE_SUIT}, {santa(cfg.theme), RED}, {"THE NORTH STAR", GOLD}, {"", WHITE},
                {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config2(840, 610, true, 700);
    story = std::make_unique<Story>(true, true);
}

}  // namespace SkyJumpShow
