#pragma once
// Road Race's end show (see ArcadeShow.h): NEW BEST! or GREAT DRIVE!, the
// score and the metres driven, then a last drive home through a snowy town
// on Christmas Eve - the car, with a Christmas tree tied to its roof, rolls
// past the houses and drops a present on each doorstep as it goes, and each
// house lights up when its present lands. Then a chequered finish line comes
// up across the road; the car crosses it under fireworks, the road slows to a
// stop and a chequered flag waves it in. The town, the finish line and the
// flag stand behind the credits and GAME OVER.
//
// After two players the headline names the winner, both scores count up in
// their cars' colours, and the red and blue cars drive home side by side,
// the winner a nose ahead over the line.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace RoadRaceShow {

using RGB = ArcadeCanvas::RGB;
const RGB RED = {1.0f, 0.08f, 0.06f}, BLUE = {0.15f, 0.4f, 1.0f};

class Story : public ShowStory {
public:
    // cars: 1 or 2. who: alone, 1 the red car or 2 the blue; with two, the
    // winner (0 a tie).
    explicit Story(int cars = 1, int who = 1) : two(cars >= 2), who(who) {}

    void begin(ArcadeShow &s) override {
        theme = s.theme();
        W = s.W;
        carH = std::max(2, std::min(6, s.H / 8));
        carL = carH <= 2 ? 5 : 2 * carH;
        pitch = carH + 1;
        // Two lanes and the kerbs at the bottom, a verge of snow below; the
        // houses stand on the kerb, under the sky.
        const int roadRows = 2 * pitch + 1;
        kerbTop = std::max(s.H / 3, s.H - roadRows - std::max(1, s.H / 8));
        kerbBot = kerbTop + roadRows - 1;
        speed = W * 0.4f;
        ready = false;
        // A small moon, between the finish banner and the flag where they stop.
        moonR = 2.5f;
        moonY = std::max(3.0f, kerbTop * 0.25f);
        moonX = W * 0.64f;
        flakes.clear();
        for (int i = 0; i < std::max(8, W * s.H / 60); ++i) {
            flakes.push_back({s.rnd() * W, s.rnd() * s.H, 2.0f + s.rnd() * 3.0f, s.rnd() * 6.28f});
        }
        stars.clear();
        for (int i = 0; i < std::max(6, W * kerbTop / 50); ++i) {
            stars.push_back({(int)(s.rnd() * W), (int)(s.rnd() * std::max(1, kerbTop - 6)), s.rnd() * 6.28f});
        }
    }

    // The timings hang on the story's length, which draw() is the first to
    // know: a sprint for the line from 46%, over it at 60%, stopped by 78%.
    void setup(ArcadeShow &s, double storyMs) {
        ready = true;
        tSprint = 0.46 * storyMs / 1000.0;
        tCross = 0.6 * storyMs / 1000.0;
        tStop = 0.78 * storyMs / 1000.0;
        flagX = (float)(scrollAt(1e9) + W * 0.8f);
        finishX = (float)(scrollAt(tCross) + carX(tCross) + carL + (two && who ? 2 : 0));
        // Houses along the street, each a little different, all the way to
        // where the car stops - but none in the way of the gantry or the flag.
        houses.clear();
        float x = W * 0.45f;
        while (x < flagX + W) {
            House h;
            h.x = x;
            h.w = 7 + (int)(s.rnd() * 4);
            h.h = std::min(kerbTop - 2, 4 + (int)(s.rnd() * 3));
            h.wall = WALLS[(int)(s.rnd() * 4) % 4];
            h.door = 1 + (int)(s.rnd() * (h.w - 3));
            const bool clear = !(h.x < finishX + 6 && h.x + h.w > finishX - 6) && !(h.x < flagX + 10 && h.x + h.w > flagX - 2);
            if (clear) {
                houses.push_back(h);
            }
            x += h.w + 3 + s.rnd() * 5;
        }
        flying.clear();
        crossed = false;
        nextBurst = 0;
    }

    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        if (!ready) {
            setup(s, 8400);
        }
        drawSky(s, c, dt, k);
        drawScene(s, c, 1e9, k);   // stopped at the finish
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double storyLen, float k) override {
        if (!ready) {
            setup(s, storyLen);
        }
        const double t = ms / 1000.0;
        drawSky(s, c, dt, k);
        const float scroll = (float)scrollAt(t);

        // A present for each house as its door goes by, thrown from the roof.
        for (auto &h : houses) {
            const float doorX = h.x + h.door + 0.5f - scroll;
            if (!h.thrown && doorX <= carX(t) + carL * 0.5f) {
                h.thrown = true;
                flying.push_back({carX(t) + carL * 0.5f + scroll, (float)carY(0) - 1, h.x + h.door + 0.5f, (float)kerbTop - 1, t, &h - &houses[0]});
            }
        }
        for (auto &f : flying) {
            if (!f.landed && t - f.t0 >= FLIGHT) {
                f.landed = true;
                House &h = houses[f.house];
                h.litAt = t;
                s.burst(f.toX - scroll, f.toY, {1.0f, 0.8f, 0.2f}, 8, false);
            }
        }

        // Over the line: fireworks, and more while the car rolls to a stop.
        if (!crossed && t >= tCross) {
            crossed = true;
            for (int i = 0; i < 4; ++i) {
                s.burst(W * (0.15f + 0.23f * i), kerbTop * (0.3f + 0.3f * s.rnd()), ArcadeShow::hue(s.rnd()), 26, i % 2 == 0);
            }
            nextBurst = t + 0.35;
        }
        if (crossed && t >= nextBurst && t < storyLen / 1000.0 - 0.8) {
            nextBurst = t + 0.35 + s.rnd() * 0.4;
            s.burst(W * (0.1f + 0.8f * s.rnd()), kerbTop * (0.25f + 0.4f * s.rnd()), ArcadeShow::hue(s.rnd()), 22, s.rnd() < 0.3f);
        }
        drawScene(s, c, t, k);
        s.drawFireworks(c, k);
    }

private:
    struct House {
        float x;
        int w, h, door;
        RGB wall;
        bool thrown = false;
        double litAt = -1;   // when its present landed
    };
    struct Present {
        float fromX, fromY, toX, toY;   // in the street, not on the screen
        double t0;
        long house;
        bool landed = false;
    };
    static constexpr double FLIGHT = 0.55;
    const RGB WALLS[4] = {{0.5f, 0.2f, 0.12f}, {0.45f, 0.35f, 0.2f}, {0.25f, 0.3f, 0.45f}, {0.35f, 0.25f, 0.4f}};

    // The road moves by at a lively speed, then slows to a stop once the car
    // is over the line.
    double scrollAt(double t) const {
        if (t < tCross) {
            return speed * t;
        }
        const double d = std::min(t, tStop) - tCross, span = tStop - tCross;
        return speed * tCross + speed * (d - d * d / (2 * span));
    }
    // Where the car is on the screen: a quarter of the way in, then a sprint
    // up the road to cross the line in the middle of the wall, and rolling on
    // a little as the road stops, to come to rest short of the flag.
    float carX(double t) const {
        if (t < tSprint) {
            return W / 4.0f;
        }
        if (t < tCross) {
            const float u = (float)((t - tSprint) / (tCross - tSprint));
            return W / 4.0f + W * 0.22f * u * u * (3 - 2 * u);
        }
        const float u = (float)std::clamp((t - tCross) / (tStop - tCross + 0.6), 0.0, 1.0);
        return W * 0.47f + W * 0.1f * (1.0f - (1.0f - u) * (1.0f - u));
    }
    int carY(int lane) const { return kerbTop + 1 + lane * pitch; }

    // The night: no sky colour (a filled background washes out on the wall),
    // just a few dim stars, the moon and falling snow.
    void drawSky(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) {
        const float tw = (float)(s.t / 1000.0);
        for (const auto &st : stars) {
            c.add(st.x, st.y, {0.9f, 0.9f, 1.0f}, (0.08f + 0.07f * std::sin(tw * 2.5f + st.phase)) * k);
        }
        for (int y = (int)(moonY - moonR - 1); y <= (int)(moonY + moonR + 1); ++y) {
            for (int x = (int)(moonX - moonR - 1); x <= (int)(moonX + moonR + 1); ++x) {
                const float in = ArcadeCanvas::clamp01(moonR - std::hypot(x + 0.5f - moonX, y + 0.5f - moonY) + 0.5f);
                c.add(x, y, theme == ShowTheme::Halloween ? RGB{1.0f, 0.5f, 0.08f} : RGB{1.0f, 0.93f, 0.7f},
                      (theme == ShowTheme::Halloween ? 0.5f : 0.25f) * in * k);
            }
        }
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x += std::sin(tw * 1.4f + f.phase) * 1.2f * dt;
            if (f.y >= s.H) {
                f.y -= s.H + 1;
                f.x = s.rnd() * W;
            }
            // Snow; leaves at Halloween; none on an everyday show.
            static const RGB LEAF[3] = {{1.0f, 0.4f, 0.0f}, {0.85f, 0.12f, 0.05f}, {0.55f, 0.3f, 0.05f}};
            if (theme == ShowTheme::Halloween) {
                c.add((int)f.x, (int)f.y, LEAF[(int)(f.phase * 10) % 3], 0.45f * k);
            } else if (theme == ShowTheme::Christmas) {
                c.add((int)f.x, (int)f.y, {1, 1, 1}, 0.3f * k);
            }
        }
    }

    void drawScene(ArcadeShow &s, ArcadeCanvas::Canvas &c, double t, float k) {
        const int H = s.H;
        const float scroll = (float)scrollAt(t);
        const int base = (int)std::floor(scroll);
        const float tw = (float)(s.t / 1000.0);

        // The street: kerbs, a dashed line between the two lanes, snow below.
        for (int x = 0; x < W; ++x) {
            const int wc = base + x;
            if (wc % 2 == 0) {
                c.set(x, kerbTop, {0.75f, 0.82f, 1.0f}, 0.1f * k);
                c.set(x, kerbBot, {0.75f, 0.82f, 1.0f}, 0.1f * k);
            }
            if (wc % 6 < 2) {
                c.set(x, kerbTop + pitch, {0.75f, 0.82f, 1.0f}, 0.14f * k);
            }
            if ((wc * 7) % 3 == 0 && theme == ShowTheme::Christmas) {
                c.set(x, H - 1, {0.75f, 0.82f, 1.0f}, 0.06f * k);   // a sprinkle of snow
            }
            // Lights along the far verge (orange and purple at Halloween; none on an everyday show).
            if (kerbBot + 2 < H && wc % 3 == 0 && theme != ShowTheme::Everyday) {
                static const RGB BULBS[4] = {{1, 0.1f, 0.05f}, {0.1f, 1, 0.2f}, {1, 0.75f, 0.1f}, {0.2f, 0.35f, 1}};
                static const RGB SPOOKY[4] = {{1, 0.45f, 0}, {0.65f, 0.2f, 1}, {1, 0.45f, 0}, {0.35f, 1, 0.15f}};
                c.set(x, kerbBot + 2, (theme == ShowTheme::Halloween ? SPOOKY : BULBS)[(wc / 3) % 4],
                      (0.25f + 0.15f * std::sin(tw * 3 + wc)) * k);
            }
        }

        for (const auto &h : houses) {
            drawHouse(c, h, (int)std::lround(h.x - scroll), t, tw, k);
        }

        // The finish: a chequered line across the road and a gantry over it
        // with FINISH on its banner.
        const int fx = (int)std::lround(finishX - scroll);
        if (fx > -30 && fx < W + 30) {
            for (int y = kerbTop + 1; y < kerbBot; ++y) {
                for (int dx = 0; dx < 2; ++dx) {
                    if ((y + dx) % 2 == 0) {
                        c.set(fx + dx, y, {1, 1, 1}, 0.75f * k);
                    }
                }
            }
            const std::string word = "FINISH";
            const int bw = ArcadeShow::textW(word, 1) + 2, by = std::max(0, kerbTop - 8);
            const int bx = fx + 1 - bw / 2;
            for (int y = by + 1; y <= kerbTop - 1; ++y) {
                c.set(bx - 1, y, {0.45f, 0.45f, 0.5f}, 0.6f * k);
                c.set(bx + bw, y, {0.45f, 0.45f, 0.5f}, 0.6f * k);
            }
            for (int y = by; y < by + 7; ++y) {
                for (int x = bx; x < bx + bw; ++x) {
                    const bool edge = y == by || y == by + 6;
                    if (edge && (x + y) % 2) {
                        c.set(x, y, {1, 1, 1}, 0.6f * k);   // a chequered edge round the black banner
                    } else {
                        c.set(x, y, {0, 0, 0}, 0);
                    }
                }
            }
            s.text(c, word, bx + 1, by + 1, 1, {1.0f, 0.15f, 0.1f}, k);
        }

        // The chequered flag, waving where the car comes to rest.
        const int gx = (int)std::lround(flagX - scroll);
        if (gx > -10 && gx < W + 2) {
            const int top = std::max(0, kerbTop - 9);
            for (int y = top; y < kerbTop; ++y) {
                c.set(gx, y, {0.55f, 0.5f, 0.45f}, 0.6f * k);
            }
            const int fw = 8, fh = 4;
            for (int col = 0; col < fw; ++col) {
                const int wave = (int)std::lround(std::sin(tw * 7.0f - col * 0.8f) * 0.9f * col / fw);
                for (int row = 0; row < fh; ++row) {
                    const bool white = ((col / 2) + (row / 2)) % 2 == 0;
                    if (white) {   // the dark squares are just the night showing through
                        c.set(gx + 1 + col, top + row + wave, {1, 1, 1}, 0.8f * k);
                    }
                }
            }
        }

        // The presents in the air, arcing up off the roof onto the doorsteps.
        for (const auto &f : flying) {
            if (f.landed) {
                continue;
            }
            const float u = (float)std::clamp((t - f.t0) / FLIGHT, 0.0, 1.0);
            const float x = f.fromX + (f.toX - f.fromX) * u - scroll;
            const float y = f.fromY + (f.toY - f.fromY) * u - 5.0f * std::sin(u * 3.1416f);
            // A present; candy at Halloween; a brown parcel on an everyday show.
            const RGB a = theme == ShowTheme::Halloween ? ShowArt::ORANGE : theme == ShowTheme::Everyday ? RGB{0.6f, 0.4f, 0.15f} : RGB{1, 0.1f, 0.1f};
            const RGB b = theme == ShowTheme::Halloween ? RGB{0.65f, 0.2f, 1.0f} : theme == ShowTheme::Everyday ? RGB{0.6f, 0.4f, 0.15f} : RGB{1, 0.8f, 0.2f};
            c.set((int)std::lround(x), (int)std::lround(y), a, k);
            c.set((int)std::lround(x) + 1, (int)std::lround(y), b, k);
        }

        // The car (or both), a tree on the roof.
        const float cx = carX(t);
        if (two) {
            const float lead = 4.0f;
            const float redX = cx + (who == 1 ? lead / 2 : (who == 2 ? -lead / 2 : 0));
            const float blueX = cx + (who == 2 ? lead / 2 : (who == 1 ? -lead / 2 : 0));
            drawCar(c, (int)std::lround(redX), carY(0), RED, tw, k);
            drawCar(c, (int)std::lround(blueX), carY(1), BLUE, tw, k);
        } else {
            drawCar(c, (int)std::lround(cx), carY(1), who == 2 ? BLUE : RED, tw, k);
        }
    }

    // A house: a wall, a snowy roof with a string of lights along its edge,
    // windows and a door with a wreath. Its windows are dark until its
    // present lands, then glow warm, with the present on the doorstep.
    void drawHouse(ArcadeCanvas::Canvas &c, const House &h, int x0, double t, float tw, float k) {
        const int base = kerbTop - 1, top = base - h.h + 1;
        if (x0 > W + 1 || x0 + h.w < -6) {
            return;
        }
        const bool lit = h.litAt >= 0 && t >= h.litAt;
        const float glow = lit ? (float)std::clamp((t - h.litAt) / 0.4, 0.0, 1.0) : 0.0f;
        for (int y = top + 2; y <= base; ++y) {
            for (int x = 0; x < h.w; ++x) {
                const bool door = (x == h.door || x == h.door + 1) && y >= base - 1;
                const bool window = !door && (y - top) % 2 == 1 && x % 3 == 1 && y < base;
                RGB col = h.wall;
                float kk = 0.1f;
                if (door) {
                    col = {0.35f, 0.15f, 0.05f};
                    kk = 0.25f;
                } else if (window) {
                    col = {1.0f, 0.75f, 0.3f};
                    kk = 0.06f + 0.64f * glow;
                }
                c.set(x0 + x, y, col, kk * k);
            }
        }
        // The roof: two rows stepping in, deep in snow (purple slates at
        // Halloween, red tiles otherwise).
        const RGB roof = theme == ShowTheme::Halloween ? RGB{0.45f, 0.15f, 0.55f} : theme == ShowTheme::Everyday ? RGB{0.75f, 0.15f, 0.1f}
                                                                                                                 : RGB{0.85f, 0.9f, 1.0f};
        for (int x = -1; x <= h.w; ++x) {
            c.set(x0 + x, top + 1, roof, 0.25f * k);
        }
        for (int x = 1; x < h.w - 1; ++x) {
            c.set(x0 + x, top, roof, 0.25f * k);
        }
        for (int x = -1; x <= h.w && theme != ShowTheme::Everyday; x += 2) {
            static const RGB BULBS[4] = {{1, 0.1f, 0.05f}, {0.1f, 1, 0.2f}, {1, 0.75f, 0.1f}, {0.2f, 0.35f, 1}};
            static const RGB SPOOKY[4] = {{1, 0.45f, 0}, {0.65f, 0.2f, 1}, {1, 0.45f, 0}, {0.35f, 1, 0.15f}};
            c.set(x0 + x, top + 2, (theme == ShowTheme::Halloween ? SPOOKY : BULBS)[((x + 1) / 2 + (int)h.x) % 4],
                  (0.3f + 0.3f * std::sin(tw * 4 + x + h.x)) * k);
        }
        // A wreath beside the door; at Halloween a jack-o'-lantern, lit with the house.
        if (h.door + 2 < h.w && theme != ShowTheme::Everyday) {
            c.set(x0 + h.door + 2, base - 1, theme == ShowTheme::Halloween ? ShowArt::ORANGE : RGB{0.1f, 0.7f, 0.2f},
                  (theme == ShowTheme::Halloween ? 0.35f + 0.6f * glow : 0.6f) * k);
        }
        if (lit) {
            // The present on the doorstep: a candy bucket at Halloween, a parcel otherwise.
            const RGB a = theme == ShowTheme::Halloween ? ShowArt::ORANGE : theme == ShowTheme::Everyday ? RGB{0.6f, 0.4f, 0.15f} : RGB{1, 0.1f, 0.1f};
            const RGB b = theme == ShowTheme::Halloween ? ShowArt::ORANGE : theme == ShowTheme::Everyday ? RGB{0.6f, 0.4f, 0.15f} : RGB{1, 0.8f, 0.2f};
            c.set(x0 + h.door, base, a, 0.9f * k);
            c.set(x0 + h.door + 1, base, b, 0.9f * k);
        }
    }

    // The game's car from the side - cab, windscreen, headlight, tail light
    // and wheels - with a little Christmas tree (and its star) on the roof.
    void drawCar(ArcadeCanvas::Canvas &c, int x0, int y0, const RGB &body, float tw, float k) {
        const RGB light = ArcadeCanvas::mix(body, {1, 1, 1}, 0.3f), dark = ArcadeCanvas::mix(body, {0, 0, 0}, 0.45f);
        const RGB GLASS = {0.43f, 0.59f, 0.78f};
        const bool wheels = carH >= 3;
        const int topRows = std::max(1, (carH - (wheels ? 1 : 0)) / 2), bodyEnd = wheels ? carH - 2 : carH - 1;
        const int c0 = carL / 3, c1 = carL - 2;
        for (int i = 0; i < carL; ++i) {
            for (int row = 0; row < carH; ++row) {
                const int x = x0 + i, y = y0 + row;
                if (row < topRows) {
                    if (i >= c0 && i <= c1) {
                        c.set(x, y, i == c1 ? GLASS : light, k);
                    }
                } else if (row <= bodyEnd) {
                    c.set(x, y, i == carL - 1 ? RGB{1, 0.94f, 0.75f} : (i == 0 ? RGB{0.75f, 0.06f, 0.03f} : body), k);
                } else if (i == 1 || i == carL - 2) {
                    c.set(x, y, {0.35f, 0.35f, 0.4f}, k);
                } else if (i > 0 && i < carL - 1) {
                    c.set(x, y, dark, 0.6f * k);
                }
            }
        }
        // The beam of its headlights.
        for (int d = 1; d <= 4; ++d) {
            c.add(x0 + carL - 1 + d, y0 + topRows, {1, 0.94f, 0.75f}, 0.22f * (5 - d) / 4.0f * k);
        }
        // The tree, tied on over the cab - or a pumpkin, or a balloon on a string.
        const int tx = x0 + (c0 + c1) / 2;
        if (theme == ShowTheme::Halloween) {
            c.set(tx - 1, y0 - 1, ShowArt::ORANGE, 0.8f * k);
            c.set(tx, y0 - 1, ShowArt::ORANGE, 0.9f * k);
            c.set(tx + 1, y0 - 1, ShowArt::ORANGE, 0.8f * k);
            c.set(tx, y0 - 2, ShowArt::STEM, 0.8f * k);
            return;
        }
        if (theme == ShowTheme::Everyday) {
            c.set(tx, y0 - 1, {0.5f, 0.5f, 0.5f}, 0.6f * k);
            c.set(tx, y0 - 2, {0.5f, 0.5f, 0.5f}, 0.6f * k);
            c.set(tx, y0 - 3, {1, 0.2f, 0.2f}, 0.9f * k);
            c.set(tx, y0 - 4, {1, 0.2f, 0.2f}, 0.9f * k);
            return;
        }
        c.set(tx - 1, y0 - 1, {0.05f, 0.55f, 0.12f}, 0.8f * k);
        c.set(tx, y0 - 1, {0.05f, 0.55f, 0.12f}, 0.8f * k);
        c.set(tx + 1, y0 - 1, {0.05f, 0.55f, 0.12f}, 0.8f * k);
        c.set(tx, y0 - 2, {0.05f, 0.55f, 0.12f}, 0.8f * k);
        c.set(tx, y0 - 3, {1.0f, 0.8f, 0.15f}, (0.6f + 0.4f * std::sin(tw * 6)) * k);
        c.set(tx + 1, y0 - 1, {1.0f, 0.1f, 0.1f}, (0.5f + 0.5f * std::sin(tw * 5 + 1)) * k);   // a bauble
    }

    struct Flake {
        float x, y, vy, phase;
    };
    struct Star {
        int x, y;
        float phase;
    };
    std::vector<Flake> flakes;
    std::vector<Star> stars;
    float moonX = 40, moonY = 4, moonR = 2.5f;
    bool two = false;
    int who = 1;
    int carH = 3, carL = 6, pitch = 4, kerbTop = 13, kerbBot = 21;
    int W = 60;
    bool ready = false;
    float speed = 24, finishX = 0, flagX = 0;
    double tSprint = 4, tCross = 5, tStop = 7, nextBurst = 0;
    bool crossed = false;
    std::vector<House> houses;
    std::vector<Present> flying;
    ShowTheme theme = ShowTheme::Christmas;
};

// Alone: score and metres driven; who 1 the red car (the D-pad), 2 the blue
// (A/B). previousBest the best before this game.
inline ShowConfig config(int score, int metres, bool newBest, int previousBest, int who = 1) {
    using namespace ShowColours;
    const RGB car = who == 2 ? BLUE : RED;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "DRIVE!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    cfg.stats.push_back({"METRES", metres, false, GREEN});
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"ROAD RACE", HEAD}, {"", WHITE}, {"CAST", HEAD}, {who == 2 ? "THE BLUE CAR" : "THE RED CAR", car},
                {"THE TRAFFIC", WHITE}, {"THE SNOWPLOUGH", {1.0f, 0.5f, 0.1f}}, {"AND", HEAD},
                {std::to_string(metres) + " METRES OF ROAD", GREEN}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// After two players: each car's score, and the winner (1 red, 2 blue, 0 a
// tie) gets the headline. The better score is the one that counts as a best.
inline ShowConfig config2(int red, int blue, int winner, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    if (winner == 0) {
        cfg.headline = {"IT'S A", "TIE!"};
    } else {
        cfg.headline = {winner == 1 ? "RED" : "BLUE", "WINS!"};
    }
    cfg.stats.push_back({"RED", red, false, RED});
    cfg.stats.push_back({"BLUE", blue, false, BLUE});
    if (newBest) {
        cfg.stats.push_back({"NEW BEST", std::max(red, blue), false, GOLD});
    } else if (previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    cfg.roll = {{"ROAD RACE", HEAD}, {"", WHITE}, {"CAST", HEAD}, {"THE RED CAR", RED}, {"THE BLUE CAR", BLUE},
                {"THE TRAFFIC", WHITE}, {"THE SNOWPLOUGH", {1.0f, 0.5f, 0.1f}}, {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config2(2140, 1875, 1, true, 1990);
    story = std::make_unique<Story>(2, 1);
}

}  // namespace RoadRaceShow
