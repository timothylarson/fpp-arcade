#pragma once
// Frogger's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the score
// and the level, then one last crossing on a snowy Christmas night - the frog,
// in a Santa hat, hops over a road of cars with their headlights on (one just
// misses it), rides two snowy logs across an icy river and lands on the lily
// pad in its home bay. The view tips up to the sky, its four little frogs pop
// up out of the other bays, and they all hop for joy under the moon and the
// fireworks - the credits, and GAME OVER.
//
// At Halloween it's an autumn night instead: leaf-strewn banks, two
// jack-o'-lanterns on the median, a hedge strung with orange and purple
// lights, dark water with the moon's orange glints, leaves falling and the
// frog in a witch's hat. On an everyday show: grass, little round trees, a
// hedge of flowers, plain logs and no hat.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace FroggerShow {

class Story : public ShowStory {
public:
    using RGB = ArcadeCanvas::RGB;

    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        theme = s.theme();
        // The scene is seven bands three rows deep, filling the bottom of the
        // panel: the start bank, two lanes of road, a snowy median, two lanes
        // of river and the home bays. What's left at the top is sky.
        top = H - 7 * ROW;
        homeX.clear();
        for (int i = 0; i < 5; ++i) {
            homeX.push_back((i + 1) * W / 6);   // the game's own five bays
        }
        fx = homeX[2];   // the frog starts below the middle bay and goes home to it

        // The road: right along the near lane, left along the far one. Each
        // lane's first car is timed to pass just behind the frog as it hops
        // out - a near miss - and the rest are far enough apart to leave it a
        // gap to hop into.
        lanes.clear();
        lanes.push_back(lane(W, 1, +1, 14.0f, CAR_W, 8, 24, HOP[1] + 0.25f));
        lanes.push_back(lane(W, 2, -1, 15.0f, CAR_W, 8, 24, HOP[2] + 0.25f));
        // The river: logs drifting right then left at the same speed, so the
        // frog rides out and back and comes off the second log right below
        // its bay. Each lane's first log is under the frog as it hops on.
        logW = W >= 40 ? 10 : 8;
        lanes.push_back(lane(W, 4, +1, LOG_V, logW, 6, logW + 7, HOP[3]));
        lanes.push_back(lane(W, 5, -1, LOG_V, logW, 6, logW + 7, HOP[4]));
        Lane &a = lanes[2], &b = lanes[3];
        a.phase = (fx - logW / 2 + 1) - LOG_V * HOP[3];
        b.phase = (fx + LOG_V * (HOP[4] - HOP[3]) - logW / 2 + 1) + LOG_V * HOP[4];
        Lane &r1 = lanes[0], &r2 = lanes[1];
        r1.phase = (fx - 2 - CAR_W) - r1.speed * r1.miss;
        r2.phase = (fx + 2) + r2.speed * r2.miss;

        flakes.clear();
        const int n = std::max(6, W * H / 80);
        for (int i = 0; i < n; ++i) {
            flakes.push_back({s.rnd() * W, s.rnd() * H, 1.5f + s.rnd() * 2.5f, s.rnd() * 6.28f});
        }
        glints.clear();
        for (int i = 0; i < W * 2 * ROW / 20; ++i) {
            glints.push_back({s.rnd() * W, (int)(s.rnd() * ROW), s.rnd() < 0.5f ? 4 : 5, s.rnd() * 6.28f});
        }
        // The moon up in the corner, where the view tips up to.
        s.moonX = W * 0.8f;
        s.moonY = std::max(3.0f, H * 0.2f);
        s.moonR = std::max(2.5f, H / 7.0f);
        nextFirework = FIREWORKS;
        popped = 0;
        landed = false;
        now = 0;
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        // The family still hopping on its bank, under the credits.
        now += dt;
        s.drawNight(c, dt, k);
        drawScene(s, c, dt, now, k);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        // Everything below is timed in seconds of an 8.4 second story, and
        // stretched to whatever length the show gives it.
        const float sec = (float)(ms / len) * STORY_S;
        dt *= STORY_S / std::max(1.0f, (float)(len / 1000.0));
        now = sec;
        s.drawNight(c, dt, k);

        // A burst of green as the frog lands home, a little one as each
        // youngster pops up, then fireworks across the sky.
        if (!landed && sec >= HOP[5] + HOP_S) {
            landed = true;
            s.burst(fx + 0.5f, (float)rowTop(6, offset(sec)) + 1, {0.3f, 1, 0.3f}, 16, false);
        }
        while (popped < 4 && sec >= POP[popped] + 0.15f) {
            const int i = BABY_BAY[popped++];
            s.burst(homeX[i] + 0.5f, (float)rowTop(6, offset(sec)) + 1, {0.6f, 1, 0.4f}, 8, false);
        }
        while (sec >= nextFirework) {
            nextFirework += 0.45f + 0.2f * s.rnd();
            static const RGB COLS[] = {{1, 0.15f, 0.1f}, {0.2f, 1, 0.3f}, {1, 0.8f, 0.2f}, {0.3f, 0.5f, 1}, {1, 1, 1}};
            const float x = s.W * (0.1f + 0.8f * s.rnd());
            const float y = std::max(2.0f, (rowTop(6, offset(sec)) - 2) * (0.25f + 0.4f * s.rnd()));
            s.burst(x, y, COLS[(int)(s.rnd() * 5) % 5], 18 + (int)(s.rnd() * 10), s.rnd() < 0.3f);
        }

        drawScene(s, c, dt, sec, k);
        s.drawFireworks(c, k);
    }

private:
    // A road car or a log: all of a lane's movers share its speed and spacing.
    struct Lane {
        int band;        // 1, 2 road; 4, 5 river
        int dir;         // +1 right, -1 left
        float speed;     // LEDs a second
        int w;           // each mover's width
        float period;    // wraps every this many LEDs
        int count;
        float phase;     // where the first one's left end is at time 0
        float miss;      // road: when the first car passes behind the frog
    };
    struct Flake {
        float x, y, vy, phase;
    };
    struct Glint {
        float x;
        int row, band;
        float phase;
    };

    static constexpr int ROW = 3;          // each band's depth, and the frog's size
    static constexpr int CAR_W = 5;
    static constexpr float STORY_S = 8.4f;
    static constexpr float HOP_S = 0.22f;  // a hop's flight
    static constexpr float LOG_V = 5.0f;
    // When the frog leaves each band: start, road, road, median, log, log.
    static constexpr float HOP[6] = {0.8f, 1.5f, 2.2f, 3.0f, 3.8f, 4.6f};
    static constexpr float PAN_FROM = 5.0f, PAN_TO = 5.9f;   // the view tips up
    static constexpr int PAN = 12;                           // by this many rows
    static constexpr float POP[4] = {5.9f, 6.15f, 6.4f, 6.65f};
    static constexpr int BABY_BAY[4] = {1, 3, 0, 4};
    static constexpr float FIREWORKS = 5.7f;

    static Lane lane(int W, int band, int dir, float speed, int w, int gap, int spacing, float miss) {
        Lane l{band, dir, speed, w, (float)(W + w + gap), 1, 0, miss};
        l.count = std::max(1, (int)(l.period / spacing));
        return l;
    }

    // The row a band's top is on, with the view tipped up by oy.
    int rowTop(int band, int oy) const { return top + oy + (6 - band) * ROW; }

    static float smooth(float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3 - 2 * x);
    }
    static int offset(float sec) { return (int)std::lround(PAN * smooth((sec - PAN_FROM) / (PAN_TO - PAN_FROM))); }

    // Where a lane's movers are at time sec: each one's left end.
    std::vector<int> moverXs(const Lane &l, float sec) const {
        std::vector<int> xs;
        for (int j = 0; j < l.count; ++j) {
            float x = l.phase + l.dir * l.speed * sec - l.dir * j * l.period / l.count;
            x = std::fmod(x + l.w, l.period);
            if (x < 0) {
                x += l.period;
            }
            xs.push_back((int)std::floor(x - l.w + 0.5f));
        }
        return xs;
    }

    // The frog's band (fractional mid-hop) and its drift along the logs.
    static float frogBand(float sec) {
        float b = 0;
        for (float h : HOP) {
            b += smooth((sec - h) / HOP_S);
        }
        return b;
    }
    static float frogDrift(float sec) {
        return LOG_V * (std::clamp(sec, HOP[3], HOP[4]) - HOP[3]) - LOG_V * (std::clamp(sec, HOP[4], HOP[5]) - HOP[4]);
    }

    void drawScene(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float sec, float k) {
        const int W = s.W, H = s.H;
        const int oy = offset(sec);
        const bool hallow = theme == ShowTheme::Halloween, xmas = theme == ShowTheme::Christmas;
        // The banks' ground: snow, leaf litter or grass.
        const RGB SNOW = xmas ? RGB{0.75f, 0.82f, 0.95f} : hallow ? RGB{0.45f, 0.25f, 0.05f} : RGB{0.15f, 0.6f, 0.15f};
        const RGB WATER = {0.0f, 0.12f, 0.6f}, HEDGE = {0.05f, 0.4f, 0.1f};

        // Snowy banks: the start, and the median with two little trees. At
        // Halloween the glints are fallen leaves, on an everyday show flowers.
        for (int band : {0, 3}) {
            const int y0 = rowTop(band, oy);
            for (int y = y0; y < y0 + ROW; ++y) {
                for (int x = 0; x < W; ++x) {
                    const unsigned h = (unsigned)(x * 7 + y * 13) % 11;
                    const float tw = h == 0 ? 0.25f + 0.15f * std::sin(sec * 4.0f + x) : 0.09f;
                    const RGB col = h != 0 || xmas ? SNOW : hallow ? ((x + y) % 2 ? ShowArt::ORANGE : RGB{0.85f, 0.15f, 0.05f})
                                                                   : ArcadeShow::hue((x * 3 + y) / 13.0f);
                    // (Leaf litter and grass dimmer than snow: a big fill.)
                    c.set(x, y, col, (h == 0 && !xmas ? 0.4f : xmas ? tw : 0.05f) * k);
                }
            }
        }
        for (float f : {0.14f, 0.86f}) {
            if (hallow) {
                ShowArt::pumpkin(c, (int)(W * f) - 2, rowTop(3, oy) - 2, 0.8f * k);
            } else {
                drawTree(c, (int)(W * f), rowTop(3, oy), sec, k);
            }
        }

        // The road, black, with a dashed line down the middle.
        for (int band : {1, 2}) {
            const int y0 = rowTop(band, oy);
            for (int y = y0; y < y0 + ROW; ++y) {
                for (int x = 0; x < W; ++x) {
                    c.set(x, y, {0, 0, 0}, 0);
                }
            }
        }
        for (int x = 0; x < W; ++x) {
            if (x % 4 < 2) {
                c.set(x, rowTop(1, oy), {1, 0.85f, 0.4f}, 0.18f * k);
            }
        }

        // The icy river, with glints of light drifting along it.
        for (int band : {4, 5}) {
            const int y0 = rowTop(band, oy);
            for (int y = y0; y < y0 + ROW; ++y) {
                for (int x = 0; x < W; ++x) {
                    c.set(x, y, WATER, 0.13f * k);
                }
            }
        }
        for (auto &g : glints) {
            const Lane &l = lanes[g.band == 4 ? 2 : 3];
            float x = std::fmod(g.x + l.dir * l.speed * 0.6f * sec, (float)W);
            if (x < 0) {
                x += W;
            }
            const float b = 0.5f + 0.5f * std::sin(sec * 3.0f + g.phase);
            c.set((int)x, rowTop(g.band, oy) + g.row, hallow ? RGB{1.0f, 0.5f, 0.1f} : RGB{0.6f, 0.85f, 1.0f}, (0.1f + 0.25f * b) * k);
        }

        // The home bank: a snowy hedge strung with lights, broken by five
        // bays of water, each with a lily pad and a red flower.
        const int hy = rowTop(6, oy);
        for (int x = 0; x < W; ++x) {
            if (inBay(x)) {
                c.set(x, hy, WATER, 0.13f * k);
                c.set(x, hy + 1, WATER, 0.13f * k);
                c.set(x, hy + 2, {0.2f, 0.75f, 0.2f}, 0.35f * k);
            } else {
                if (xmas) {
                    c.set(x, hy, SNOW, 0.15f * k);
                }
                c.set(x, hy + 1, HEDGE, 0.3f * k);
                c.set(x, hy + 2, HEDGE, 0.2f * k);
                if (x % 3 == 0) {
                    // Christmas lights; orange and purple ones; or flowers.
                    static const RGB BULBS[] = {{1, 0.1f, 0.05f}, {1, 0.7f, 0.1f}, {0.2f, 0.4f, 1}, {1, 1, 1}};
                    static const RGB SPOOKY[] = {{1, 0.45f, 0}, {0.65f, 0.2f, 1}, {1, 0.45f, 0}, {0.35f, 1, 0.15f}};
                    const int i = (x / 3 + (int)(sec * 2.5f)) % 4;
                    if (theme == ShowTheme::Everyday) {
                        c.set(x, hy, ArcadeShow::hue(x / 17.0f), 0.5f * k);
                    } else {
                        c.set(x, hy + 1, hallow ? SPOOKY[i] : BULBS[i], 0.6f * k);
                    }
                }
            }
        }
        for (int bx : homeX) {
            c.set(bx, hy + 2, {1, 0.15f, 0.2f}, 0.8f * k);
        }

        // Logs, snow along their tops, the cut ends paler.
        for (size_t li = 2; li < 4; ++li) {
            const Lane &l = lanes[li];
            const int y0 = rowTop(l.band, oy);
            for (int lx : moverXs(l, sec)) {
                for (int x = 0; x < l.w; ++x) {
                    const bool end = x == 0 || x == l.w - 1;
                    if (xmas && !end && (x * 5 + lx) % 7 != 0) {
                        c.set(lx + x, y0, SNOW, 0.22f * k);
                    }
                    c.set(lx + x, y0 + 1, end ? RGB{0.85f, 0.6f, 0.35f} : RGB{0.55f, 0.27f, 0.08f}, (end ? 0.5f : 0.45f) * k);
                    c.set(lx + x, y0 + 2, {0.4f, 0.18f, 0.05f}, 0.3f * k);
                }
            }
        }

        // Cars on the bottom two rows of their lanes, headlights on.
        static const RGB CARS[] = {{1, 0.2f, 0.2f}, {0.25f, 0.6f, 1}, {1, 0.85f, 0.2f}, {0.75f, 0.3f, 1}, {0.5f, 1, 0.35f}};
        for (size_t li = 0; li < 2; ++li) {
            const Lane &l = lanes[li];
            const int y0 = rowTop(l.band, oy) + 1;
            const std::vector<int> xs = moverXs(l, sec);
            for (size_t j = 0; j < xs.size(); ++j) {
                drawCar(c, xs[j], y0, l.dir, CARS[(li * 2 + j) % 5], k);
            }
        }

        // The family in the other bays, popping up out of the water once the
        // frog is home, then everyone hopping for joy.
        for (int i = 0; i < 4; ++i) {
            const float t = sec - POP[i];
            if (t < 0) {
                continue;
            }
            const float rise = smooth(t / 0.15f) * 2.0f;
            const int y = hy + 3 - (int)std::lround(rise) - joy(sec, 6.9f + i * 0.17f, 2.0f);
            drawFrog(c, homeX[BABY_BAY[i]], y, false, hy + 2, k);
        }

        // The frog: hopping band to band, riding the logs, then home.
        const float band = frogBand(sec);
        const int x = (int)std::lround(fx + frogDrift(sec));
        int y = (int)std::lround(top + oy + (6 - band) * ROW);
        y -= joy(sec, 6.2f, 3.0f);
        drawFrog(c, x, y, true, H, k);

        // Snow falling over it all (leaves at Halloween; nothing otherwise).
        for (size_t i = 0; i < flakes.size() && theme != ShowTheme::Everyday; i += hallow ? 2 : 1) {
            Flake &f = flakes[i];
            f.y += f.vy * (hallow ? 0.6f : 1.0f) * dt;
            f.x += std::sin(sec * 1.3f + f.phase) * (hallow ? 2.5f : 1.0f) * dt;
            if (f.y >= H) {
                f.y -= H + 1;
                f.x = s.rnd() * W;
            }
            static const RGB LEAF[3] = {{1.0f, 0.4f, 0.0f}, {0.85f, 0.12f, 0.05f}, {0.55f, 0.3f, 0.05f}};
            c.add((int)f.x, (int)f.y, hallow ? LEAF[i % 3] : RGB{1, 1, 1}, (hallow ? 0.45f : 0.3f) * k);
        }
    }

    bool inBay(int x) const {
        for (int bx : homeX) {
            if (std::abs(x - bx) <= 1) {
                return true;
            }
        }
        return false;
    }

    // How high a frog is off the ground, hopping for joy from time from.
    static int joy(float sec, float from, float height) {
        if (sec < from) {
            return 0;
        }
        return (int)std::lround(std::fabs(std::sin((sec - from) * 5.5f)) * height);
    }

    // A frog seen from above, 3 wide: eyes, body, back legs. The hero is
    // three rows and wears a Santa hat; a youngster is the top two rows.
    // Nothing is drawn below clipY (a youngster still under the water).
    void drawFrog(ArcadeCanvas::Canvas &c, int cx, int y, bool hero, int clipY, float k) {
        const RGB EYE = {0.95f, 1, 0.4f}, BODY = hero ? RGB{0.15f, 1, 0.2f} : RGB{0.45f, 1, 0.3f},
                  LEGS = {0.08f, 0.6f, 0.1f};
        auto px = [&](int x, int yy, const RGB &col, float kk) {
            if (yy <= clipY) {
                c.set(x, yy, col, kk * k);
            }
        };
        px(cx - 1, y, EYE, 1);
        px(cx + 1, y, EYE, 1);
        for (int dx = -1; dx <= 1; ++dx) {
            px(cx + dx, y + 1, BODY, 1);
        }
        if (hero) {
            px(cx - 1, y + 2, LEGS, 1);
            px(cx + 1, y + 2, LEGS, 1);
            // The hat sits between the eyes, its bobble flopped to one side -
            // or a witch's hat, its tip bent over; none on an everyday show.
            if (theme == ShowTheme::Halloween) {
                px(cx, y, ShowArt::HAT, 1);
                px(cx, y - 1, ShowArt::HAT, 1);
                px(cx + 1, y - 2, ShowArt::HAT, 0.9f);
            } else if (theme == ShowTheme::Christmas) {
                px(cx, y, {1, 0.1f, 0.08f}, 1);
                px(cx, y - 1, {1, 0.1f, 0.08f}, 0.8f);
                px(cx + 1, y - 1, {1, 1, 1}, 0.9f);
            }
        }
    }

    // 5 x 2: roof and window, then the body with a tail light and a
    // headlight, and the headlight's beam on the road ahead.
    static void drawCar(ArcadeCanvas::Canvas &c, int x, int y, int dir, const RGB &col, float k) {
        const int front = dir > 0 ? x + CAR_W - 1 : x, back = dir > 0 ? x : x + CAR_W - 1;
        for (int i = 1; i < CAR_W - 1; ++i) {
            c.set(x + i, y, col, 0.7f * k);
        }
        c.set(front - dir, y, {0.6f, 0.8f, 1}, 0.45f * k);   // the windscreen
        for (int i = 0; i < CAR_W; ++i) {
            c.set(x + i, y + 1, col, k);
        }
        c.set(back, y + 1, {1, 0, 0}, 0.8f * k);
        c.set(front, y + 1, {1, 1, 0.7f}, k);
        c.add(front + dir, y + 1, {1, 0.9f, 0.5f}, 0.3f * k);
        c.add(front + 2 * dir, y + 1, {1, 0.9f, 0.5f}, 0.15f * k);
    }

    // A little Christmas tree with a star on top, 3 x 3.
    // A little Christmas tree: a star, green, a bauble. On an everyday show a
    // round green tree on a trunk.
    void drawTree(ArcadeCanvas::Canvas &c, int x, int y, float sec, float k) const {
        const RGB G = {0.05f, 0.65f, 0.15f};
        if (theme == ShowTheme::Everyday) {
            c.set(x, y, G, 0.8f * k);
            c.set(x - 1, y + 1, G, 0.8f * k);
            c.set(x, y + 1, G, 0.8f * k);
            c.set(x + 1, y + 1, G, 0.8f * k);
            c.set(x, y + 2, {0.5f, 0.25f, 0.06f}, 0.7f * k);
            return;
        }
        c.set(x, y, {1, 0.85f, 0.2f}, (0.7f + 0.3f * std::sin(sec * 5.0f)) * k);
        c.set(x, y + 1, G, 0.8f * k);
        c.set(x - 1, y + 2, G, 0.8f * k);
        c.set(x, y + 2, {1, 0.1f, 0.1f}, 0.8f * k);   // a bauble
        c.set(x + 1, y + 2, G, 0.8f * k);
    }

    ShowTheme theme = ShowTheme::Christmas;
    int top = 4, fx = 30, logW = 10;
    std::vector<int> homeX;
    std::vector<Lane> lanes;
    std::vector<Flake> flakes;
    std::vector<Glint> glints;
    float nextFirework = 0, now = 0;
    int popped = 0;
    bool landed = false;
};

// score and level at the end of the game.
inline ShowConfig config(int score, int level, bool newBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    cfg.stats.push_back({"LEVEL", level, false, GREEN});
    cfg.roll = {{"FROGGER", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE FROG", GREEN}, {"THE CARS", RED},
                {"THE LOGS", GOLD}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(1385, 5, true);
    story = std::make_unique<Story>();
}

}  // namespace FroggerShow
