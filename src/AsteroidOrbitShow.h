#pragma once
// Asteroid Orbit's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the
// score and the sector reached, then a victory flight - the ship sweeps in
// over the stars and flies a loop, blasts the rocks in its way and scoops up
// the crystals they drop, a black hole opens and swallows the rest, and the
// ship is pulled round and in. It warps through in a flash of light that
// becomes a Christmas star, and pops out of the star and away - the credits,
// and GAME OVER.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace AsteroidOrbitShow {

using RGB = ArcadeCanvas::RGB;

// The game's own colours (FPPAsteroidOrbit.cpp), so the story looks like it.
const RGB SHIP = {0.2f, 0.85f, 1.0f};
const RGB FLAME = {1.0f, 0.45f, 0.05f};
const RGB BULLET = {1.0f, 0.95f, 0.6f};
const RGB FUEL = {0.15f, 0.45f, 1.0f};
const RGB AMMO = {1.0f, 0.8f, 0.1f};
const RGB PULSE = {1.0f, 0.2f, 0.9f};
const RGB HOLE = {0.55f, 0.25f, 1.0f};
const RGB STAR = {1.0f, 0.8f, 0.2f};
// Rock colours by sector, repeating, as in the game.
inline RGB rockColour(int sector) {
    static const RGB ROCK[4] = {{0.8f, 0.62f, 0.45f}, {0.95f, 0.42f, 0.2f}, {0.5f, 0.75f, 0.95f}, {0.75f, 0.45f, 0.95f}};
    return ROCK[(std::max(1, sector) - 1) % 4];
}

class Story : public ShowStory {
public:
    // sector: the one reached, so the rocks are that sector's colour.
    explicit Story(int sector) : rockCol(rockColour(sector)) {}

    void begin(ArcadeShow &s) override {
        W = s.W;
        H = s.H;
        stars.clear();
        for (int i = 0; i < std::max(6, W * H / 40); ++i) {
            stars.push_back({s.rnd() * W, s.rnd() * H, 0.05f + s.rnd() * 0.1f, s.rnd() * 6.28f});
        }
        // The loop on the left, the black hole on the right, the star between.
        cx = W * 0.26f;
        cy = H * 0.45f;
        R = std::min(H * 0.28f, W * 0.2f);
        hx = W * 0.74f;
        hy = H * 0.45f;
        r0 = std::min(W * 0.18f, H * 0.3f);
        starX = W * 0.5f;
        starY = H * 0.45f;
        starR = std::min(H * 0.26f, W * 0.2f);
        fireDist = std::clamp(W * 0.15f, 5.0f, 9.0f);
        rocks.clear();
        shots.clear();
        crystals.clear();
        ready = false;
        warped = false;
        nextCapture = 0;
        trailAcc = 0;
    }

    // Behind the credits: the stars, and the Christmas star the warp left.
    void backdrop(ArcadeShow &, ArcadeCanvas::Canvas &c, float dt, float k) override {
        drawStars(c, dt, k);
        drawStar(c, starX, starY, starR, 0.6f * k);
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        const float u = (float)(ms / len), sec = (float)(ms / 1000.0);
        lenS = (float)(len / 1000.0);
        if (!ready) {
            setup();
        }
        drawStars(c, dt, k);

        // ------------------------------------------------ the black hole
        float holeR = 0;
        if (u >= U_HOLE) {
            holeR = std::min(HOLE_R, (u - U_HOLE) * lenS * 2.0f);   // opens as fast as in the game
            if (u >= U_WARP) {
                holeR *= std::max(0.0f, 1.0f - (u - U_WARP) * lenS / 0.25f);   // and shuts behind the ship
            }
        }
        if (holeR > 0) {
            drawHole(c, holeR, sec, k);
        }

        // ------------------------------------------------ rocks
        for (auto &r : rocks) {
            if (!r.alive) {
                continue;
            }
            if (!r.captured && u >= U_HOLE && !r.target) {
                capture(r, rockPos(r, u, sec), u);
            }
            float x, y, scale = 1.0f;
            rockAt(r, u, sec, x, y, scale);
            if (r.captured && u >= r.cu1) {
                // Gone over the edge: a little purple puff.
                r.alive = false;
                puff(s, hx, hy, HOLE, 10, 6.0f);
                continue;
            }
            drawRock(c, x, y, r.r * scale, sec * r.spinV + r.id, r.id, k);
        }

        // ------------------------------------------------ the ship
        float sx = 0, sy = 0, dx = 0, dy = -1;
        const bool shipOn = u < U_WARP || u >= U_OUT;
        if (shipOn) {
            shipAt(u, sx, sy);
            float nx, ny;
            shipAt(u + 0.004f, nx, ny);
            const float d = std::hypot(nx - sx, ny - sy);
            if (d > 0.0001f) {
                dx = (nx - sx) / d;
                dy = (ny - sy) / d;
            }
            // A comet trail, so the loop draws itself in the sky.
            trailAcc += dt * 45.0f;
            while (trailAcc >= 1.0f) {
                trailAcc -= 1.0f;
                // Dimmer than the ship, so the ship still stands out at its head.
                const bool hot = s.rnd() < 0.45f;
                const RGB col = hot ? FLAME : SHIP;
                s.sparks.push_back({sx - dx * 1.2f + (s.rnd() - 0.5f) * 0.6f, sy - dy * 1.2f + (s.rnd() - 0.5f) * 0.6f,
                                    -dx * 2.0f + (s.rnd() - 0.5f), -dy * 2.0f + (s.rnd() - 0.5f), 0,
                                    0.4f + s.rnd() * 0.4f, {col.r * 0.4f, col.g * 0.4f, col.b * 0.4f}, 0, 0});
            }
        }

        // Fire at the next rock in the way once it's close.
        if (shipOn && u < U_SPIRAL) {
            for (auto &r : rocks) {
                if (r.target && r.alive && !r.shotAt) {
                    const RGB p = rockPos(r, u, sec);
                    const float ddx = p.r - sx, ddy = p.g - sy, d = std::hypot(ddx, ddy);
                    if (d < fireDist) {
                        r.shotAt = true;
                        shots.push_back({sx + dx, sy + dy, ddx / d * 30.0f, ddy / d * 30.0f, 0.9f, r.id});
                    }
                    break;
                }
            }
        }
        for (size_t i = 0; i < shots.size();) {
            Shot &b = shots[i];
            b.life -= dt;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            bool gone = b.life <= 0;
            for (auto &r : rocks) {
                if (!gone && r.alive && r.id == b.rock) {
                    const RGB p = rockPos(r, u, sec);
                    if (std::hypot(b.x - p.r, b.y - p.g) < r.r + 0.6f) {
                        breakRock(s, r, p.r, p.g, sec, u);
                        gone = true;
                    }
                }
            }
            if (gone) {
                shots.erase(shots.begin() + i);
            } else {
                c.add((int)std::floor(b.x), (int)std::floor(b.y), BULLET, k);
                c.add((int)std::floor(b.x - b.vx * 0.03f), (int)std::floor(b.y - b.vy * 0.03f), BULLET, 0.25f * k);
                ++i;
            }
        }

        // ------------------------------------------------ crystals
        for (auto &cr : crystals) {
            if (!cr.alive) {
                continue;
            }
            cr.x += cr.vx * dt;
            if (shipOn && std::hypot(cr.x - sx, cr.y - sy) < 1.8f) {
                cr.alive = false;
                puff(s, cr.x, cr.y, cr.col, 10, 6.0f);
                continue;
            }
            c.add((int)std::floor(cr.x), (int)std::floor(cr.y), cr.col, (0.6f + 0.4f * std::sin(sec * 8.0f)) * k);
        }

        if (shipOn) {
            drawShip(c, sx, sy, dx, dy, ms, k);
        }

        // ------------------------------------------------ through, and the star
        if (u >= U_WARP) {
            const float w = (u - U_WARP) * lenS;   // seconds since the warp
            if (!warped) {
                warped = true;
                s.burst(hx, hy, ArcadeCanvas::WHITE, 40, true);
                puff(s, hx, hy, ArcadeCanvas::WHITE, 16, 10.0f);
            }
            // A flash of light from the hole, and a ring of it racing out.
            if (w < 0.3f) {
                const float reach = W * 0.4f;
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) {
                        const float d = std::hypot(x + 0.5f - hx, y + 0.5f - hy);
                        if (d < reach) {
                            c.add(x, y, {0.85f, 0.8f, 1.0f}, 0.45f * (1.0f - w / 0.3f) * (1.0f - d / reach) * k);
                        }
                    }
                }
            }
            if (w < 0.8f) {
                const float rad = w * 45.0f, f = w / 0.8f;
                const int n = std::max(12, (int)(rad * 7));
                for (int j = 0; j < n; ++j) {
                    const float a = j * 6.2831853f / n;
                    c.add((int)std::floor(hx + std::cos(a) * rad), (int)std::floor(hy + std::sin(a) * rad),
                          ArcadeCanvas::mix(ArcadeCanvas::WHITE, HOLE, f), 0.9f * (1.0f - f) * k);
                }
            }
            // The light gathers into a Christmas star, which drifts to the
            // middle, the ship bursting out of it on its way.
            const float g = std::clamp((w - 0.1f) / 0.5f, 0.0f, 1.0f);
            if (g > 0) {
                const float m = std::clamp((w - 0.3f) / 0.8f, 0.0f, 1.0f), e = m * m * (3 - 2 * m);
                const float x = hx + (starX - hx) * e, y = hy + (starY - hy) * e;
                drawStar(c, x, y, starR * backOut(g), k);
                if (s.rnd() < dt * 6.0f) {
                    const float a = s.rnd() * 6.2832f, d = starR * (0.9f + s.rnd() * 0.8f);
                    s.sparks.push_back({x + std::cos(a) * d, y + std::sin(a) * d, 0, 0, 0, 0.6f + s.rnd() * 0.5f, STAR,
                                        30.0f, 0});
                }
            }
        }
        s.drawFireworks(c, k);   // the trail, the bursts and the sparkles
    }

private:
    // When each part happens, as a share of the story.
    static constexpr float U_IN = 0.08f;      // in from the left, to the bottom of the loop
    static constexpr float U_LOOP = 0.32f;    // round the loop
    static constexpr float U_HOLE = 0.44f;    // the black hole opens
    static constexpr float U_SPIRAL = 0.56f;  // on to the hole, blasting rocks; then pulled round it
    static constexpr float U_WARP = 0.78f;    // and in
    static constexpr float U_OUT = 0.86f;     // out of the star and away
    static constexpr float HOLE_R = 2.4f;

    struct Rock {
        float x0, y0, vx, vy, r, spinV;
        int id;
        bool target, alive, shotAt, captured;
        float cr, ca, cu0, cu1;   // where it was caught by the hole, and when it goes in
    };
    struct Shot {
        float x, y, vx, vy, life;
        int rock;
    };
    struct Crystal {
        float x, y, vx;
        RGB col;
        bool alive;
    };
    struct BgStar {
        float x, y, k, phase;
    };

    // Rocks in the ship's way, placed so it meets them on its path to the
    // hole, and a couple of big ones tumbling past for the hole to take.
    void setup() {
        ready = true;
        const int n = W >= 45 ? 2 : 1;
        for (int i = 0; i < n; ++i) {
            const float f = n == 1 ? 0.55f : 0.38f + 0.37f * i;
            const float ut = U_LOOP + f * (U_SPIRAL - U_LOOP);
            float px, py;
            shipAt(ut, px, py);
            Rock r = {};
            r.vx = -1.5f;
            r.vy = 0.3f * (i ? 1 : -1);
            r.x0 = px - r.vx * ut * lenS;   // there just as the ship gets there
            r.y0 = py - r.vy * ut * lenS;
            r.r = n == 1 ? 1.7f : 2.0f;
            r.spinV = 1.0f + i * 0.5f;
            r.id = nextId++;
            r.target = true;
            r.alive = true;
            rocks.push_back(r);
        }
        const float spots[2][2] = {{0.62f, 0.16f}, {0.95f, 0.82f}};
        for (int i = 0; i < (W >= 45 ? 2 : 1); ++i) {
            Rock r = {};
            r.x0 = W * (n == 1 ? 0.8f : spots[i][0]);
            r.y0 = H * spots[i][1];
            r.vx = -1.2f;
            r.vy = i ? -0.2f : 0.2f;
            r.r = 2.6f;
            r.spinV = -0.8f;
            r.id = nextId++;
            r.alive = true;
            rocks.push_back(r);
        }
    }

    // Free-drifting rocks: x/y in r and g. (RGB is just a handy float triple.)
    RGB rockPos(const Rock &r, float, float sec) const { return {r.x0 + r.vx * sec, r.y0 + r.vy * sec, 0}; }

    // Where a rock is: drifting, or spiralling into the hole and shrinking.
    void rockAt(const Rock &r, float u, float sec, float &x, float &y, float &scale) const {
        if (!r.captured) {
            const RGB p = rockPos(r, u, sec);
            x = p.r;
            y = p.g;
            scale = 1.0f;
            return;
        }
        const float f = std::clamp((u - r.cu0) / (r.cu1 - r.cu0), 0.0f, 1.0f);
        const float rad = r.cr * std::pow(1.0f - f, 1.2f), a = r.ca + 6.2832f * 0.8f * (0.3f * f + 0.7f * f * f);
        x = hx + std::cos(a) * rad;
        y = hy + std::sin(a) * rad;
        scale = 0.35f + 0.65f * (1.0f - f);
    }

    // The hole takes hold of a rock: from here it spirals in, one after another.
    void capture(Rock &r, const RGB &p, float u) {
        r.captured = true;
        r.cr = std::hypot(p.r - hx, p.g - hy);
        r.ca = std::atan2(p.g - hy, p.r - hx);
        r.cu0 = u;
        r.cu1 = std::min(U_WARP - 0.03f, std::max(u, U_HOLE) + 0.16f + 0.04f * nextCapture++);
        r.cu1 = std::max(r.cu1, u + 0.06f);
    }

    // A shot rock: a puff of dust, two small rocks flung off and a crystal
    // where it was, as in the game.
    void breakRock(ArcadeShow &s, Rock &r, float x, float y, float sec, float u) {
        r.alive = false;
        puff(s, x, y, rockCol, 14, 8.0f);
        static const RGB KINDS[3] = {FUEL, AMMO, PULSE};
        crystals.push_back({x, y, r.vx, KINDS[crystals.size() % 3], true});
        for (int side : {-1, 1}) {
            Rock f = {};
            f.vx = r.vx * 0.5f + side * 0.8f;
            f.vy = side * 4.0f;
            f.x0 = x + side * 0.5f - f.vx * sec;
            f.y0 = y + side * 1.0f - f.vy * sec;
            f.r = 1.0f;
            f.spinV = 2.0f * side;
            f.id = nextId++;
            f.alive = true;
            if (u >= U_HOLE) {
                capture(f, rockPos(f, u, sec), u);
            }
            rocks.push_back(f);
        }
    }

    // Dust, sparks and crystal flashes: like the game's bursts, no gravity.
    static void puff(ArcadeShow &s, float x, float y, const RGB &col, int n, float speed) {
        for (int i = 0; i < n; ++i) {
            const float a = s.rnd() * 6.2832f, v = speed * (0.4f + s.rnd() * 0.8f);
            s.sparks.push_back({x, y, std::cos(a) * v, std::sin(a) * v, 0, 0.4f + s.rnd() * 0.5f,
                                ArcadeCanvas::mix(col, ArcadeCanvas::WHITE, 0.3f), 0, 0});
        }
    }

    // The flight: in along the bottom of the loop, once round it, a curve on
    // to the hole (smoothly, so it heads into its orbit), then the spiral in;
    // after the warp, out of the star and off the top right.
    void shipAt(float u, float &x, float &y) const {
        if (u < U_IN) {
            const float f = u / U_IN;
            x = -3.0f + (cx + 3.0f) * f;
            y = cy + R;
        } else if (u < U_LOOP) {
            const float p = 6.2832f * (u - U_IN) / (U_LOOP - U_IN);
            x = cx + R * std::sin(p);
            y = cy + R * std::cos(p);
        } else if (u < U_SPIRAL) {
            const float f = (u - U_LOOP) / (U_SPIRAL - U_LOOP), span = U_SPIRAL - U_LOOP;
            // Hermite, from the bottom of the loop to the start of the orbit,
            // with the speeds it has at either end.
            const float t0x = R * 6.2832f / (U_LOOP - U_IN) * span, t0y = 0;
            float ax, ay, bx, by;
            spiralAt(0, ax, ay);
            spiralAt(0.01f, bx, by);
            const float t1x = (bx - ax) / 0.01f / (U_WARP - U_SPIRAL) * span * 0.7f;
            const float t1y = (by - ay) / 0.01f / (U_WARP - U_SPIRAL) * span * 0.7f;
            const float f2 = f * f, f3 = f2 * f;
            const float h00 = 2 * f3 - 3 * f2 + 1, h10 = f3 - 2 * f2 + f, h01 = -2 * f3 + 3 * f2, h11 = f3 - f2;
            x = h00 * cx + h10 * t0x * 0.7f + h01 * ax + h11 * t1x;
            y = h00 * (cy + R) + h10 * t0y + h01 * ay + h11 * t1y;
        } else if (u < U_WARP) {
            spiralAt((u - U_SPIRAL) / (U_WARP - U_SPIRAL), x, y);
        } else {
            const float f = std::clamp((u - U_OUT) / (0.97f - U_OUT), 0.0f, 1.0f), e = f * f;
            x = starX + (W + 4 - starX) * e;
            y = starY + (-6 - starY) * e;
        }
    }

    // Round the hole, closer and faster, from its left.
    void spiralAt(float f, float &x, float &y) const {
        const float rad = r0 * std::pow(1.0f - f, 1.3f), a = 3.1416f + 6.2832f * 1.6f * (0.35f * f + 0.65f * f * f);
        x = hx + std::cos(a) * rad;
        y = hy + std::sin(a) * rad;
    }

    static float backOut(float x) {
        const float c1 = 1.70158f, c3 = c1 + 1;
        return 1 + c3 * std::pow(x - 1, 3.0f) + c1 * std::pow(x - 1, 2.0f);
    }

    // The game's starfield, drifting left as the ship flies.
    void drawStars(ArcadeCanvas::Canvas &c, float dt, float k) {
        clock += dt;
        for (auto &st : stars) {
            st.x -= 1.5f * dt;
            if (st.x < 0) {
                st.x += W;
            }
            c.add((int)st.x, (int)st.y, {0.6f, 0.7f, 1.0f}, st.k * (0.6f + 0.4f * std::sin(clock * 1.7f + st.phase)) * k);
        }
    }

    // The game's ship: a body, a white nose and a flickering flame.
    void drawShip(ArcadeCanvas::Canvas &c, float x, float y, float dx, float dy, double ms, float k) {
        auto at = [&](float px, float py, const RGB &col, float kk) {
            c.add((int)std::floor(px), (int)std::floor(py), col, kk * k);
        };
        at(x, y, SHIP, 0.85f);
        at(x + dx * 1.1f, y + dy * 1.1f, ArcadeCanvas::WHITE, 1.0f);
        if (std::fmod(ms, 100.0) < 70.0) {
            at(x - dx * 1.1f, y - dy * 1.1f, FLAME, 0.9f);
        }
    }

    // The game's rock: a ball lit from the top left with turning darker
    // patches; edge LEDs lit by how much of them it covers.
    void drawRock(ArcadeCanvas::Canvas &c, float x, float y, float r, float spin, int id, float k) {
        const float L[3] = {-0.5f, -0.6f, 0.62f};
        for (int py = (int)std::floor(y - r - 1); py <= (int)std::ceil(y + r + 1); ++py) {
            for (int px = (int)std::floor(x - r - 1); px <= (int)std::ceil(x + r + 1); ++px) {
                const float dx = px + 0.5f - x, dy = py + 0.5f - y, d = std::hypot(dx, dy);
                const float cover = ArcadeCanvas::clamp01(r + 0.5f - d);
                if (cover <= 0) {
                    continue;
                }
                const float nx = dx / r, ny = dy / r, nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                const float lam = std::max(0.0f, nx * L[0] + ny * L[1] + nz * L[2]);
                const float pits = 0.8f + 0.2f * std::sin(3.0f * std::atan2(dy, dx) + spin) * std::sin(d * 2.1f + id);
                c.add(px, py, rockCol, (0.18f + 0.75f * lam) * pits * cover * k);
            }
        }
    }

    // The game's black hole: a dark middle with a ring of light swirling in.
    void drawHole(ArcadeCanvas::Canvas &c, float holeR, float sec, float k) {
        const float reach = holeR + 1.8f;
        for (int py = (int)std::floor(hy - reach); py <= (int)std::ceil(hy + reach); ++py) {
            for (int px = (int)std::floor(hx - reach); px <= (int)std::ceil(hx + reach); ++px) {
                const float dx = px + 0.5f - hx, dy = py + 0.5f - hy, d = std::hypot(dx, dy);
                if (d > reach) {
                    continue;
                }
                if (d < holeR * 0.55f) {
                    c.add(px, py, {-1, -1, -1}, k);   // swallows the stars behind it
                    continue;
                }
                const float swirl = 0.5f + 0.5f * std::sin(3.0f * std::atan2(dy, dx) - sec * 6.0f + d * 2.0f);
                const float edge = ArcadeCanvas::clamp01(1.0f - (d - holeR * 0.55f) / (reach - holeR * 0.55f));
                c.add(px, py, ArcadeCanvas::mix(HOLE, ArcadeCanvas::WHITE, swirl * edge * 0.4f), (0.15f + 0.85f * swirl) * edge * k);
            }
        }
    }

    // A five-pointed star, gold with a white heart, gently twinkling. Each
    // LED is lit by how much of it the star covers, so it keeps its points.
    void drawStar(ArcadeCanvas::Canvas &c, float x, float y, float r, float k) {
        if (r < 0.3f) {
            return;
        }
        float vx[10], vy[10];
        for (int i = 0; i < 10; ++i) {
            const float a = -1.5708f + i * 0.62832f, rr = i % 2 ? r * 0.45f : r;
            vx[i] = x + std::cos(a) * rr;
            vy[i] = y + std::sin(a) * rr;
        }
        auto inside = [&](float px, float py) {
            bool in = false;
            for (int i = 0, j = 9; i < 10; j = i++) {
                if ((vy[i] > py) != (vy[j] > py) && px < (vx[j] - vx[i]) * (py - vy[i]) / (vy[j] - vy[i]) + vx[i]) {
                    in = !in;
                }
            }
            return in;
        };
        const float tw = 0.85f + 0.15f * std::sin(clock * 5.0f);
        for (int py = (int)std::floor(y - r - 1); py <= (int)std::ceil(y + r + 1); ++py) {
            for (int px = (int)std::floor(x - r - 1); px <= (int)std::ceil(x + r + 1); ++px) {
                int hits = 0;
                for (int sy = 0; sy < 3; ++sy) {
                    for (int sx = 0; sx < 3; ++sx) {
                        hits += inside(px + (sx + 0.5f) / 3.0f, py + (sy + 0.5f) / 3.0f);
                    }
                }
                if (hits == 0) {
                    continue;
                }
                const float d = std::hypot(px + 0.5f - x, py + 0.5f - y);
                const float heart = ArcadeCanvas::clamp01(1.0f - d / (r * 0.45f));
                c.add(px, py, ArcadeCanvas::mix(STAR, ArcadeCanvas::WHITE, heart * 0.8f), hits / 9.0f * tw * k);
            }
        }
    }

    RGB rockCol;
    int W = 60, H = 25;
    float cx = 0, cy = 0, R = 0, hx = 0, hy = 0, r0 = 0, starX = 0, starY = 0, starR = 0, fireDist = 7;
    float lenS = 8, clock = 0, trailAcc = 0;
    bool ready = false, warped = false;
    int nextId = 1, nextCapture = 0;
    std::vector<BgStar> stars;
    std::vector<Rock> rocks;
    std::vector<Shot> shots;
    std::vector<Crystal> crystals;
};

// w: the model's width, for a title that fits; sector: the one reached.
inline ShowConfig config(int w, int score, int sector, bool newBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    cfg.stats.push_back({"SECTOR", sector, false, SHIP});
    // ASTEROID is a letter too wide for a 30-wide half on its own; split it
    // where it reads best rather than where the roll would hyphenate it.
    const bool wide = (w + 1) / 4 >= 8;
    auto &r = cfg.roll;
    if (wide) {
        r.push_back({"ASTEROID ORBIT", HEAD});
    } else {
        r.push_back({"ASTER-", HEAD});
        r.push_back({"OID", HEAD});
        r.push_back({"ORBIT", HEAD});
    }
    r.push_back({"", WHITE});
    r.push_back({"CAST", HEAD});
    r.push_back({"THE SHIP", SHIP});
    r.push_back({"THE ROCKS", rockColour(sector)});
    r.push_back({"THE BLACK HOLE", ArcadeCanvas::mix(HOLE, WHITE, 0.25f)});
    r.push_back({"", WHITE});
    r.push_back({"THANKS FOR PLAYING", WHITE});
    cfg.ending = "GAME OVER";
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(60, 4870, 5, true);
    story = std::make_unique<Story>(5);
}

}  // namespace AsteroidOrbitShow
