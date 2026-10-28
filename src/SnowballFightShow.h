#pragma once
// Snowball Fight's end show (see ArcadeShow.h): RED WINS! or GREEN WINS!, the
// winner's throws and the health it had left, then a truce on a snowy night -
// the winning elf rolls a snowball along the hill, growing as it goes, into
// the base of a giant snowman, tosses the middle and the head up onto it, and
// the snowman gets coal eyes, a carrot nose, stick arms and the winner's own
// hat. At Halloween its head is a jack-o'-lantern under a witch's hat, bats
// fly past an orange moon and leaves fall instead of snow; an everyday show
// has a clear night. Then the other elf walks over and the two of them throw snowballs
// straight up, where they burst like fireworks. The snowman stands behind
// the credits and GAME OVER.
//
// The elves are drawn here, so the game and the show draw them the same.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace SnowballFightShow {

using RGB = ArcadeCanvas::RGB;

// The two throwers, by hat: red on the left, green on the right.
const RGB HAT[2] = {{1.0f, 0.12f, 0.08f}, {0.15f, 1.0f, 0.2f}};
const char *const NAME[2] = {"RED", "GREEN"};
const RGB SKIN = {0.9f, 0.55f, 0.38f};
const RGB POM = {1.0f, 1.0f, 1.0f};

// An elf, three LEDs wide and four tall, standing with its feet on row
// feetY (the sprite fills the four rows above it), facing +1 right or -1
// left: a pointed hat with a white bobble flopping out behind, a face on the
// side it's looking, and a tunic a darker shade of its hat. Lying (knocked
// out) it is one row, flat on its back, hat at the far end. Drawn opaque, so
// it stands out against whatever is behind. flash whitens all of it (hit),
// hatFlash just the hat (its turn).
inline void drawElf(ArcadeCanvas::Canvas &c, int cx, int feetY, int facing, const RGB &hat, float k,
                    bool lying = false, float flash = 0.0f, float hatFlash = 0.0f) {
    const RGB body = {hat.r * 0.45f, hat.g * 0.45f, hat.b * 0.45f};
    const RGB hatCol = ArcadeCanvas::mix(hat, ArcadeCanvas::WHITE, hatFlash);
    auto put = [&](int x, int y, const RGB &col, float b) {
        c.set(x, y, ArcadeCanvas::mix(col, ArcadeCanvas::WHITE, flash), b * k);
    };
    if (lying) {
        // Feet toward where it was facing, head away: body, face, hat, bobble.
        const int y = feetY - 1, f = facing;
        put(cx + f, y, body, 0.8f);
        put(cx, y, body, 0.8f);
        put(cx - f, y, SKIN, 0.7f);
        put(cx - 2 * f, y, hat, 0.9f);
        put(cx - 3 * f, y - 1, POM, 0.6f);
        return;
    }
    const int top = feetY - 4;
    put(cx - facing, top, POM, 0.7f);   // the bobble, behind
    put(cx, top, hatCol, 0.9f);
    for (int dx = -1; dx <= 1; ++dx) {
        put(cx + dx, top + 1, hatCol, 0.9f);
    }
    put(cx, top + 2, SKIN, 0.75f);
    put(cx + facing, top + 2, SKIN, 0.75f);
    for (int dx = -1; dx <= 1; ++dx) {
        put(cx + dx, top + 3, body, 0.85f);
    }
}

class Story : public ShowStory {
public:
    explicit Story(int winnerSide) : winner(winnerSide == 1 ? 1 : 0) {}

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        // The winner comes from its own side of the wall and faces the middle.
        from = winner == 0 ? -1 : 1;   // -1: in from the left
        theme = s.theme();
        sx = W / 2.0f;
        rb = std::max(2.5f, H * 0.17f);
        rm = rb * 0.68f;
        rh = rb * 0.48f;
        // The middle and the head sit a little into the ball below.
        baseY = groundY(sx, H) - rb;
        midY = baseY - rb - rm + 0.7f;
        headY = midY - rm - rh + 0.5f;
        moonX = winner == 0 ? W * 0.82f : W * 0.18f;
        moonY = H * 0.22f;
        moonR = std::min(std::max(2.0f, H / 8.0f), 2.5f);
        stars.clear();
        for (int i = 0; i < std::max(6, W * H / 90); ++i) {
            stars.push_back({s.rnd() * W, s.rnd() * H * 0.55f, 0.0f, s.rnd() * 6.28f});
        }
        flakes.clear();
        for (int i = 0; i < std::max(6, W * H / 70); ++i) {
            flakes.push_back({s.rnd() * W, s.rnd() * H, 2.0f + s.rnd() * 2.5f, s.rnd() * 6.28f});
        }
        snowballs.clear();
        nextThrow = 0;
        popped = 0;
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        drawSky(s, c, dt, k);
        drawGround(s, c, k);
        drawSnowman(s, c, rb, sx, 3, k);
        drawElf(c, (int)std::lround(sx + from * (rb + 4)), (int)std::lround(groundY(sx + from * (rb + 4), s.H)), -from,
                HAT[winner], k);
        drawElf(c, (int)std::lround(sx - from * (rb + 4)), (int)std::lround(groundY(sx - from * (rb + 4), s.H)), from,
                HAT[1 - winner], k);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const int W = s.W, H = s.H;
        const float u = (float)(ms / len);
        drawSky(s, c, dt, k);
        drawGround(s, c, k);

        // 0 - 0.36: the base, rolled in from the winner's edge and growing as
        // it picks up snow.
        const float startX = from < 0 ? -2.0f : W + 1.0f;
        const float a = std::clamp(u / 0.36f, 0.0f, 1.0f);
        const float ease = 1.0f - (1.0f - a) * (1.0f - a);
        const float r = 1.0f + (rb - 1.0f) * ease;
        const float bx = startX + (sx - startX) * ease;
        // The elf ends up beside the snowman, on the side it came from.
        const float elfHome = sx + from * (rb + 4);
        float elfX = bx + from * (r + 2.0f);
        int elfFace = -from;
        if (u >= 0.36f) {
            // Back off to the side to toss up the middle and the head.
            const float b = std::clamp((u - 0.36f) / 0.06f, 0.0f, 1.0f);
            elfX = (sx + from * (rb + 2.0f)) + (elfHome - (sx + from * (rb + 2.0f))) * b;
        }

        // The base ball, with a dark fleck going round so it reads as rolling.
        drawBall(c, bx, groundY(bx, H) - r, r, k);
        if (a < 1.0f) {
            const float spin = (bx - startX) / std::max(1.0f, r) * -from;
            c.add((int)std::floor(bx + std::cos(spin) * r * 0.55f), (int)std::floor(groundY(bx, H) - r + std::sin(spin) * r * 0.55f),
                  {-0.3f, -0.3f, -0.25f}, k);
        }

        // 0.42 - 0.54: the middle, tossed from the elf's hands in an arc and
        // landing with a bounce. 0.54 - 0.64: the head, the same way.
        tossed(s, c, u, 0.42f, 0.54f, elfX, midY, rm, k);
        tossed(s, c, u, 0.54f, 0.64f, elfX, headY, rh, k, theme == ShowTheme::Halloween);
        if (theme == ShowTheme::Halloween && u >= 0.64f) {
            drawStem(c, k);
        }
        // 0.64 - 0.74: eyes, nose, buttons, arms and the hat, one by one,
        // each with a little puff.
        const int parts = std::clamp((int)((u - 0.64f) / 0.02f) + 1, 0, 5);
        if (u >= 0.64f) {
            for (; popped < parts; ++popped) {
                // Where each one goes: the face, the buttons and arms on the
                // middle, the hat on top.
                const float py = popped >= 2 && popped <= 3 ? midY : (popped == 4 ? headY - rh - 1 : headY);
                const RGB hatCol = theme == ShowTheme::Halloween ? ShowArt::ORANGE : HAT[winner];
                s.burst(sx, py, popped == 4 ? hatCol : RGB{1, 1, 1}, 8, false);
            }
            drawDetails(c, popped, k);
        }

        // 0.74 on: the other elf walks over from its side, and the two throw
        // snowballs straight up, turn about, that burst like fireworks.
        const float otherHome = sx - from * (rb + 4);
        const float otherStart = from < 0 ? W + 2.0f : -3.0f;
        const float w = std::clamp((u - 0.70f) / 0.08f, 0.0f, 1.0f);
        const float otherX = otherStart + (otherHome - otherStart) * w;
        if (u >= 0.70f) {
            const bool walking = w < 1.0f;
            drawElf(c, (int)std::lround(otherX), (int)std::lround(groundY(otherX, H)) - (walking && std::fmod(ms, 240.0) < 120.0 ? 1 : 0),
                    from, HAT[1 - winner], k);
        }
        if (u >= 0.79f && ms >= nextThrow) {
            // Turn about, a throw every half second or so.
            const int who = (int)snowballs.size() % 2 == 0 ? winner : 1 - winner;
            const float hx = who == winner ? elfX - from * 2.0f : otherX + from * 2.0f;
            snowballs.push_back({hx, groundY(hx, H) - 2.0f, (who == winner ? -from : from) * (1.0f + s.rnd() * 2.0f),
                                 -(H * 1.25f + s.rnd() * H * 0.35f), who});
            nextThrow = ms + 420.0 + s.rnd() * 200.0;
        }
        const bool hop = u >= 0.66f && u < 0.74f && std::fmod(ms, 300.0) < 150.0;   // happy with the snowman
        drawElf(c, (int)std::lround(elfX), (int)std::lround(groundY(elfX, H)) - (hop ? 1 : 0), u < 0.42f ? elfFace : -from,
                HAT[winner], k);

        // The snowballs: up, slowing, and a burst at the top in a thrower's
        // colour, white or gold.
        for (size_t i = 0; i < snowballs.size();) {
            Ball &b = snowballs[i];
            b.vy += H * 1.6f * dt;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            if (b.vy >= -H * 0.25f) {
                static const RGB GOLD = {1.0f, 0.8f, 0.25f};
                const float pick = s.rnd();
                // At Halloween, orange and purple in place of white and gold.
                const bool hallow = theme == ShowTheme::Halloween;
                const RGB col = pick < 0.5f ? HAT[b.who]
                                            : (pick < 0.8f ? (hallow ? ShowArt::ORANGE : RGB{1, 1, 1})
                                                           : (hallow ? RGB{0.65f, 0.2f, 1.0f} : GOLD));
                s.burst(b.x, b.y, col, 22 + (int)(s.rnd() * 10), s.rnd() < 0.2f);
                snowballs[i] = snowballs.back();
                snowballs.pop_back();
                continue;
            }
            c.set((int)std::floor(b.x), (int)std::floor(b.y), {1, 1, 1}, k);
            c.add((int)std::floor(b.x), (int)std::floor(b.y) + 1, {1, 1, 1}, 0.3f * k);
            ++i;
        }
        s.drawFireworks(c, k);
    }

private:
    struct Ball {
        float x, y, vx, vy;
        int who;
    };
    struct Dot {
        float x, y, v, phase;
    };

    // The night as the game has it: a black sky - no coloured fill, which
    // looks washed out on the lights - with stars, the moon and falling snow.
    // At Halloween an orange moon, bats and falling leaves; everyday, no snow.
    void drawSky(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) {
        for (auto &st : stars) {
            c.add((int)st.x, (int)st.y, {0.9f, 0.9f, 1.0f}, (0.1f + 0.08f * std::sin((float)(s.t / 400.0) + st.phase)) * k);
        }
        for (int y = (int)(moonY - moonR - 1); y <= (int)(moonY + moonR + 1); ++y) {
            for (int x = (int)(moonX - moonR - 1); x <= (int)(moonX + moonR + 1); ++x) {
                const float in = ArcadeCanvas::clamp01(moonR - std::hypot(x + 0.5f - moonX, y + 0.5f - moonY) + 0.5f);
                c.add(x, y, theme == ShowTheme::Halloween ? RGB{1.0f, 0.5f, 0.08f} : RGB{1.0f, 0.93f, 0.7f},
                      (theme == ShowTheme::Halloween ? 0.5f : 0.4f) * in * k);
            }
        }
        for (auto &f : flakes) {
            f.y += f.v * dt;
            f.x += std::sin((float)(s.t / 700.0) + f.phase) * 1.2f * dt;
            if (f.y >= s.H) {
                f.y -= s.H + 1;
                f.x = s.rnd() * s.W;
            }
            static const RGB LEAF[3] = {{1.0f, 0.4f, 0.0f}, {0.85f, 0.12f, 0.05f}, {0.55f, 0.3f, 0.05f}};
            if (theme == ShowTheme::Halloween) {
                c.add((int)std::floor(f.x), (int)std::floor(f.y), LEAF[(int)(f.phase * 10) % 3], 0.45f * k);
            } else if (theme == ShowTheme::Christmas) {
                c.add((int)std::floor(f.x), (int)std::floor(f.y), {1, 1, 1}, 0.25f * k);
            }
        }
        if (theme == ShowTheme::Halloween) {
            s.drawBats(c, dt, k);
        }
    }

    // A gentle rise in the snow, the snowman on its top.
    float groundY(float x, int H) const { return H - 3.0f - 1.2f * std::cos((x - sx) * 0.09f); }

    void drawGround(ArcadeShow &s, ArcadeCanvas::Canvas &c, float k) const {
        for (int x = 0; x < s.W; ++x) {
            const float g = groundY(x + 0.5f, s.H);
            for (int y = (int)std::floor(g); y < s.H; ++y) {
                const float cover = ArcadeCanvas::clamp01(y + 1 - g);
                // A lit surface over all-but-black snow, as in the game.
                const float depth = std::clamp((y - g) / 1.5f, 0.0f, 1.0f);
                c.set(x, y, ArcadeCanvas::mix({0.16f, 0.19f, 0.26f}, {0.006f, 0.008f, 0.02f}, depth), cover * k);
            }
        }
    }

    // A ball of snow, lit from the upper left, with a soft edge: brighter
    // than the ground, as it's the star of the scene, but not glaring. Or a
    // pumpkin, for the head at Halloween.
    static void drawBall(ArcadeCanvas::Canvas &c, float bx, float by, float r, float k, bool pumpkin = false) {
        for (int y = (int)std::floor(by - r - 1); y <= (int)std::ceil(by + r + 1); ++y) {
            for (int x = (int)std::floor(bx - r - 1); x <= (int)std::ceil(bx + r + 1); ++x) {
                const float dx = x + 0.5f - bx, dy = y + 0.5f - by;
                const float in = ArcadeCanvas::clamp01(r - std::hypot(dx, dy) + 0.5f);
                if (in <= 0) {
                    continue;
                }
                const float light = 0.75f - 0.25f * (dx + dy) / std::max(1.0f, r);
                c.set(x, y, pumpkin ? RGB{0.9f * light, 0.38f * light, 0.0f} : RGB{0.5f * light, 0.55f * light, 0.66f * light},
                      in * k);
            }
        }
    }

    // A ball thrown from the elf's hands to (sx, toY), arcing over and
    // landing with a bounce, between u0 and u1; there for good after.
    void tossed(ArcadeShow &s, ArcadeCanvas::Canvas &c, float u, float u0, float u1, float fromX, float toY, float r, float k,
                bool pumpkin = false) {
        if (u < u0) {
            return;
        }
        const float f = std::clamp((u - u0) / (u1 - u0), 0.0f, 1.0f);
        const float flight = std::min(1.0f, f / 0.7f);
        const float fromY = groundY(fromX, s.H) - 3.0f;
        float x = fromX + (sx - fromX) * flight;
        float y = fromY + (toY - fromY) * flight - std::sin(flight * 3.14159f) * s.H * 0.3f;
        if (f > 0.7f) {
            // A small bounce as it settles on the ball below.
            const float b = (f - 0.7f) / 0.3f;
            x = sx;
            y = toY - (1.0f - ArcadeShow::easeOutBounce(b)) * 1.5f;
        }
        drawBall(c, x, y, r, k, pumpkin);
    }

    // The snowman's face and the rest, the first n of: eyes, nose, buttons,
    // arms, hat.
    void drawDetails(ArcadeCanvas::Canvas &c, int n, float k) const {
        const RGB COAL = {0.0f, 0.0f, 0.0f}, CARROT = {1.0f, 0.45f, 0.05f}, STICK = {0.45f, 0.25f, 0.1f};
        const int hx = (int)std::floor(sx), hy = (int)std::floor(headY);
        const int look = -from;   // the snowman looks the way the winner is standing from it
        const bool hallow = theme == ShowTheme::Halloween;
        if (n >= 1) {
            // Coal eyes, or a jack-o'-lantern's, lit from inside.
            c.set(hx - 1, hy - 1, hallow ? ShowArt::CANDLE : COAL, k);
            c.set(hx + 1, hy - 1, hallow ? ShowArt::CANDLE : COAL, k);
        }
        if (n >= 2 && hallow) {
            // A grin.
            for (int dx = -1; dx <= 1; ++dx) {
                c.set(hx + dx, hy + 1, ShowArt::CANDLE, (dx == 0 ? 0.7f : 1.0f) * k);
            }
        } else if (n >= 2) {
            c.set(hx + look, hy, CARROT, k);
            c.set(hx + 2 * look, hy, CARROT, 0.6f * k);
        }
        const int my = (int)std::floor(midY);
        if (n >= 3) {
            c.set(hx, my - 1, COAL, k);
            c.set(hx, my + 1, COAL, k);
        }
        if (n >= 4) {
            const int reach = (int)std::lround(rm) + 1;
            for (int side = -1; side <= 1; side += 2) {
                for (int i = 0; i < 3; ++i) {
                    c.set(hx + side * (reach + i), my - i, STICK, 0.8f * k);
                }
                c.set(hx + side * (reach + 3), my - 3, STICK, 0.8f * k);
                c.set(hx + side * (reach + 2), my - 3, STICK, 0.6f * k);   // a twig, like fingers
            }
        }
        if (n >= 5 && hallow) {
            // A witch's hat, its brim on the top of the pumpkin.
            ShowArt::witchHat(c, hx - 3, (int)std::floor(headY - rh) - 3, k);
        } else if (n >= 5) {
            // The winner's elf hat: a white fur brim, the point flopping over
            // to the back, and the bobble.
            const int top = (int)std::floor(headY - rh);
            for (int dx = -2; dx <= 2; ++dx) {
                c.set(hx + dx, top, POM, 0.8f * k);
            }
            for (int dx = -1; dx <= 1; ++dx) {
                c.set(hx + dx, top - 1, HAT[winner], k);
            }
            c.set(hx, top - 2, HAT[winner], k);
            c.set(hx - look, top - 2, HAT[winner], k);
            c.set(hx - 2 * look, top - 2, POM, 0.8f * k);
        }
    }

    // The pumpkin's stem, under where the hat goes.
    void drawStem(ArcadeCanvas::Canvas &c, float k) const {
        c.set((int)std::floor(sx), (int)std::floor(headY - rh), ShowArt::STEM, k);
    }

    void drawSnowman(ArcadeShow &s, ArcadeCanvas::Canvas &c, float r, float x, int, float k) const {
        drawBall(c, x, baseY, r, k);
        drawBall(c, x, midY, rm, k);
        drawBall(c, x, headY, rh, k, theme == ShowTheme::Halloween);
        if (theme == ShowTheme::Halloween) {
            drawStem(c, k);
        }
        drawDetails(c, 5, k);
        (void)s;
    }

    int winner = 0, from = -1;
    float sx = 30, rb = 4, rm = 3, rh = 2, baseY = 0, midY = 0, headY = 0;
    std::vector<Ball> snowballs;
    std::vector<Dot> stars, flakes;
    float moonX = 0, moonY = 0, moonR = 2;
    double nextThrow = 0;
    int popped = 0;
    ShowTheme theme = ShowTheme::Christmas;
};

// winner 0 red, 1 green; throws: the winner's throws; health: what it had
// left; computerSide: the elf the computer played (-1 for none), named in the
// credits.
inline ShowConfig config(int winner, int throws, int health, int computerSide) {
    using namespace ShowColours;
    const RGB REDC = {1.0f, 0.3f, 0.25f}, GREENC = {0.35f, 1.0f, 0.45f};
    const RGB &mine = winner == 0 ? REDC : GREENC;
    ShowConfig cfg;
    cfg.headline = {NAME[winner == 1 ? 1 : 0], "WINS!"};
    cfg.stats.push_back({"THROWS", throws, false, mine});
    cfg.stats.push_back({"HEALTH", health, false, GOLD});
    cfg.roll = {{"SNOWBALL FIGHT", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE RED ELF", REDC}};
    if (computerSide == 0) {
        cfg.roll.push_back({"PLAYED BY THE COMPUTER", LABEL});
    }
    cfg.roll.push_back({"THE GREEN ELF", GREENC});
    if (computerSide == 1) {
        cfg.roll.push_back({"PLAYED BY THE COMPUTER", LABEL});
    }
    cfg.roll.push_back({cfg.theme == ShowTheme::Halloween ? "A PUMPKIN HEAD" : "A GIANT SNOWMAN", WHITE});
    cfg.roll.push_back({"AND", HEAD});
    cfg.roll.push_back({"A LOT OF SNOW", WHITE});
    cfg.roll.push_back({"", WHITE});
    cfg.roll.push_back({"THANKS FOR PLAYING", WHITE});
    cfg.ending = "GAME OVER";
    // A longer story than most, for the snowman to get built.
    cfg.lengthMs = 22000;
    cfg.pStats = 0.13;
    cfg.pStory = 0.27;
    cfg.pRoll = 0.75;
    cfg.pEnd = 0.91;
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(1, 7, 2, 0);
    story = std::make_unique<Story>(1);
}

}  // namespace SnowballFightShow
