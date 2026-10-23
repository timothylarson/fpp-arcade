#include <fpp-pch.h>

#include "FPPOrbPop.h"
#include "KinectPeople.h"
#include "OrbPopShow.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <random>
#include <vector>

#include "Timers.h"
#include "log.h"
#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

// The look follows the animated background of the Claude app: a dim field of
// characters that keep changing, with shaded 3D shapes made of brighter ones.
// On a 60x25 wall a character can't be drawn, so each LED stands for one: it
// takes a new random brightness every so often ("glyph"), and every layer is
// multiplied by it, which gives the flicker and the dithered, made-of-dots
// shading. tools/orbpop-preview.html in the MatrixPi folder runs the same steps
// in a browser, with a fake Kinect, for tuning the look.

namespace {

struct RGB {
    float r, g, b;
};
RGB mix(const RGB &a, const RGB &b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

struct Palette {
    RGB field, ambient, hi, nearC, farC, energy, text, timer;
    std::array<RGB, 4> orbs;
};
const Palette FROST = {
    {0.22f, 0.38f, 1.0f}, {0.2f, 0.45f, 1.0f}, {0.75f, 0.92f, 1.0f},
    {0.25f, 0.95f, 1.0f}, {0.4f, 0.3f, 1.0f}, {0.55f, 0.9f, 1.0f},
    {0.85f, 0.92f, 1.0f}, {0.55f, 1.0f, 0.3f},
    {{{1.0f, 0.55f, 0.15f}, {1.0f, 0.2f, 0.45f}, {0.55f, 1.0f, 0.3f}, {0.75f, 0.35f, 1.0f}}},
};
const Palette CHRISTMAS = {
    {0.15f, 0.9f, 0.3f}, {0.15f, 0.85f, 0.3f}, {1.0f, 0.95f, 0.8f},
    {1.0f, 0.8f, 0.35f}, {1.0f, 0.2f, 0.15f}, {1.0f, 0.85f, 0.5f},
    {1.0f, 0.92f, 0.75f}, {1.0f, 0.75f, 0.1f},
    {{{1.0f, 0.1f, 0.1f}, {0.15f, 1.0f, 0.25f}, {1.0f, 0.75f, 0.1f}, {0.3f, 0.6f, 1.0f}}},
};
const RGB WHITE = {1.0f, 1.0f, 1.0f};
const RGB GOLD = {1.0f, 0.85f, 0.3f};

// The arcade's 3x5 letters, for the ones this game draws. They are drawn into
// the light field rather than over it, so they flicker like everything else.
const std::map<char, const char *> FONT = {
    {'S', "111100111001111"}, {'C', "011100100100011"}, {'O', "111101101101111"},
    {'R', "111101111110101"}, {'E', "111100111100111"}, {'G', "111100100101111"},
    {'0', "111101101101111"}, {'1', "010010010010010"}, {'2', "111001111100111"},
    {'3', "111001111001111"}, {'4', "101101111001001"}, {'5', "111100111001111"},
    {'6', "111100111101111"}, {'7', "111001001001001"}, {'8', "111101111101111"},
    {'9', "111101111001001"},
};

struct OrbPopSettings {
    int brightness = 50;          // %
    const Palette *palette = &FROST;
    double roundMs = 45000;
    double startHoldMs = 2000;    // someone in view this long starts a round
    float presencePct = 3.0f;     // % of LEDs a person must cover to count
    int endShow = 1, goodGame = 50;   // the End Show and Good Game settings
};

double steadyMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

class OrbPopEffect : public FPPArcadeGameEffect {
public:
    OrbPopEffect(PixelOverlayModel *m, const OrbPopSettings &s) : FPPArcadeGameEffect(m), set(s), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(1, W);
        H = std::max(1, H);
        N = W * H;
        glyph.assign(N, 0.0f);
        glyphNext.assign(N, 0.0);
        energy.assign(N, 0.0f);
        energyTmp.assign(N, 0.0f);
        prevLit.assign(N, 0);
        dist.assign(N, 0);
        depth.assign(N, 0);
        person.assign(N, 0);
        R.assign(N, 0.0f);
        G.assign(N, 0.0f);
        B.assign(N, 0.0f);
        rawDepth.resize(KinectPeople::MAX_W * KinectPeople::MAX_H);
        rawFlags.resize(KinectPeople::MAX_W * KinectPeople::MAX_H);
        for (auto &gl : glyph) {
            gl = rnd();
        }
    }

    const std::string &name() const override {
        static std::string NAME = "OrbPop";
        return NAME;
    }

    void button(const std::string &b) override {
        // Any press skips the end show (once it's had two seconds); nothing
        // else happens until it's over.
        if (mode == Mode::Show) {
            if (b.find("Pressed") != std::string::npos) {
                skipShow.store(true);
            }
            return;
        }
        const bool wasPaused = paused;
        FPPArcadeGameEffect::button(b);
        if (wasPaused || paused) {
            return;
        }
        if ((b == "A Button - Pressed" || b == "B Button - Pressed") && mode == Mode::Mirror) {
            startCountdown();
        }
    }

    void restart() override {
        orbs.clear();
        sparks.clear();
        score = 0;
        startCountdown();
        resetFrameTimer();
    }

    int32_t update() override {
        if (ending) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return EFFECT_DONE;
        }
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return FRAME_MS;
        }
        const double dt = consumeElapsedMs(FRAME_MS);
        const double now = steadyMs();

        // The end show plays instead of the score, and the Kinect is left
        // unread, so nobody starts a round over it. Drawn the same way as the
        // game's own frames: at its Brightness, flushed on FPP's schedule.
        if (mode == Mode::Show) {
            if (skipShow.exchange(false) && show.elapsedMs() > 2000.0) {
                show.finish();
            }
            show.step(dt, showCanvas);
            showCanvas.writeTo(model, set.brightness / 100.0f);
            model->setOverlayBufferDirty(true);
            if (show.over()) {
                toMirror();   // as the score screen ends
            }
            return FRAME_MS;
        }

        if (readKinect()) {
            kinectGoneMs = 0;
        } else {
            kinectGoneMs += dt;
            std::fill(depth.begin(), depth.end(), 0);
            std::fill(person.begin(), person.end(), 0);
        }
        // With no Kinect there is nothing to play with: hand the model back
        // rather than covering the show with an empty field for good. A round
        // already under way finishes first.
        if (kinectGoneMs > 10000 && mode == Mode::Mirror) {
            LogInfo(VB_PLUGIN, "Orb Pop: no Kinect frames for 10 s, stopping\n");
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            ending = true;
            return EFFECT_AFTER_NEXT_OUTPUT;
        }

        step(dt, now);

        const float k = set.brightness / 100.0f * 255.0f;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int i = y * W + x;
                model->setOverlayPixelValue(x, y,
                                            std::min(255, (int)(R[i] * k + 0.5f)),
                                            std::min(255, (int)(G[i] * k + 0.5f)),
                                            std::min(255, (int)(B[i] * k + 0.5f)));
            }
        }
        // Let FPP flush it on its own schedule, as the Kinect picture does: a
        // flush from this thread can land between FPP flushing the models and
        // laying the Left/Right halves over this one.
        model->setOverlayBufferDirty(true);
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 33;   // 30 fps, the Kinect's own rate
    enum class Mode { Mirror, Countdown, Round, Score, Show };

    struct Orb {
        float x, y, vx, vy, rad;
        bool gold;
        float age, life, phase, alpha;
        RGB col;
        bool dying, popped;
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    void add(int i, const RGB &c, float k) {
        R[i] += c.r * k;
        G[i] += c.g * k;
        B[i] += c.b * k;
    }

    void startCountdown() {
        mode = Mode::Countdown;
        modeMs = 0;
        countBeat = -1;
    }

    void sound(const char *name, float gain = 1.0f) { sfx("orbpop", name, gain); }

    // Back to attract mode after a round, with a few seconds before anyone
    // in view can start the next.
    void toMirror() {
        mode = Mode::Mirror;
        modeMs = 0;
        cooldownMs = 3000;
    }

    void startShow(bool newBest, int best, const Palette &pal) {
        auto rgb = [](const RGB &c) { return ArcadeCanvas::RGB{c.r, c.g, c.b}; };
        const OrbPopShow::Look look = {rgb(pal.field), rgb(pal.energy),
                                       {{rgb(pal.orbs[0]), rgb(pal.orbs[1]), rgb(pal.orbs[2]), rgb(pal.orbs[3])}}};
        mode = Mode::Show;
        modeMs = 0;
        orbs.clear();
        sparks.clear();
        sfxStop("orbpop");
        sound("EndShow");
        skipShow.store(false);
        showCanvas.resize(W, H);
        show.start(W, H, OrbPopShow::config(score, newBest, best, look), std::make_unique<OrbPopShow::Story>(look));
    }

    // The Kinect's view, scaled to this model if the two differ in size.
    bool readKinect() {
        int kw = 0, kh = 0;
        if (!kinect.read(kw, kh, rawDepth.data(), rawFlags.data())) {
            return false;
        }
        for (int y = 0; y < H; ++y) {
            const int sy = y * kh / H;
            for (int x = 0; x < W; ++x) {
                const int si = sy * kw + x * kw / W;
                depth[y * W + x] = rawDepth[si];
                person[y * W + x] = (rawFlags[si] & KinectPeople::PERSON) ? 1 : 0;
            }
        }
        return true;
    }

    // Calls fn(lambert, i) for each LED inside a sphere lit from L.
    template <typename F>
    void sphere(float cx, float cy, float rad, const float L[3], F fn, float alpha = 1.0f) {
        const int x0 = std::max(0, (int)std::floor(cx - rad)), x1 = std::min(W - 1, (int)std::ceil(cx + rad));
        const int y0 = std::max(0, (int)std::floor(cy - rad)), y1 = std::min(H - 1, (int)std::ceil(cy + rad));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float nx = (x + 0.5f - cx) / rad, ny = (y + 0.5f - cy) / rad, d2 = nx * nx + ny * ny;
                if (d2 >= 1.0f) {
                    continue;
                }
                const float nz = std::sqrt(1.0f - d2);
                const float edge = clamp01((1.0f - std::sqrt(d2)) * rad);
                fn(std::max(0.0f, nx * L[0] + ny * L[1] + nz * L[2]) * edge * alpha, y * W + x);
            }
        }
    }

    static void lightFrom(float x, float y, float z, float L[3]) {
        const float l = std::sqrt(x * x + y * y + z * z);
        L[0] = x / l;
        L[1] = y / l;
        L[2] = z / l;
    }

    void step(double dt, double now) {
        const Palette &pal = *set.palette;
        modeMs += dt;
        std::fill(R.begin(), R.end(), 0.0f);
        std::fill(G.begin(), G.end(), 0.0f);
        std::fill(B.begin(), B.end(), 0.0f);

        // Characters changing: each LED takes a new random weight now and then.
        for (int i = 0; i < N; ++i) {
            if (now >= glyphNext[i]) {
                glyph[i] = std::pow(rnd(), 1.4f);
                glyphNext[i] = now + 90.0 + rnd() * 650.0;
            }
        }

        // Presence, and moving between attract mode and a round.
        int present = 0, seen = 0;
        for (int i = 0; i < N; ++i) {
            present += person[i];
            seen += (person[i] || depth[i] > 0) ? 1 : 0;
        }
        const bool someone = present * 100.0f >= N * set.presencePct;
        // Once a round is going, anyone still in view keeps it going: a player
        // standing still, or close to the wall behind, can drop below the
        // start threshold (the Kinect learns a still body into the background
        // after its Relearn time), and ending then left the timer bar unfinished.
        const bool stillHere = someone || seen >= std::max(4, N / 200);
        cooldownMs = std::max(0.0, cooldownMs - dt);
        switch (mode) {
        case Mode::Mirror:
            heldMs = (someone && cooldownMs <= 0) ? heldMs + dt : std::max(0.0, heldMs - dt * 2);
            if (heldMs >= set.startHoldMs) {
                heldMs = 0;
                startCountdown();
            }
            break;
        case Mode::Countdown: {
            // A chime as each of 3, 2, 1 shows, and a brighter one for GO. The
            // first is also the only sound attract mode makes: someone was seen.
            const int beat = std::min(3, (int)(modeMs / 1000));
            if (beat != countBeat) {
                countBeat = beat;
                sound(beat < 3 ? "Count" : "Go");
            }
            if (modeMs >= 3600) {
                mode = Mode::Round;
                modeMs = 0;
                score = 0;
                orbs.clear();
                spawnInMs = 300;
                awayMs = 0;
                lastTick = -1;
            }
            break;
        }
        case Mode::Round: {
            awayMs = stillHere ? 0 : awayMs + dt;
            // A soft tick for each of the last three seconds; the timer bar
            // along the top is easy to miss while reaching for orbs.
            const int secsLeft = (int)std::ceil((set.roundMs - modeMs) / 1000.0);
            if (secsLeft <= 3 && secsLeft >= 1 && secsLeft != lastTick) {
                lastTick = secsLeft;
                sound("Tick", 0.6f);
            }
            if (modeMs >= set.roundMs || awayMs >= 15000) {
                // A good round (or a new best) gets the end show instead of
                // the score. Never for a round nobody played, even when
                // End Show is set to every game.
                int best = 0;
                const bool newBest = arcadeRecordBest(model->getName(), "Orb Pop", score, &best);
                if (score > 0 && arcadeWantShow(set.endShow, score >= set.goodGame, newBest)) {
                    startShow(newBest, best, pal);
                    return;
                }
                mode = Mode::Score;
                modeMs = 0;
                scoreSounded = false;
                for (auto &o : orbs) {
                    o.dying = true;
                }
                sfxStop("orbpop");
                sound("RoundEnd");
            }
            break;
        }
        case Mode::Score:
            // The reveal jingle as the number fades in, after the round-end
            // chime has had its moment. Nothing for a round nobody played.
            if (!scoreSounded && modeMs >= 700) {
                scoreSounded = true;
                if (score > 0) {
                    sound("Score");
                }
            }
            if (modeMs >= 6000) {
                toMirror();
            }
            break;
        case Mode::Show:
            break;   // update() runs it
        }
        const float wantAmbient = mode == Mode::Mirror ? 1.0f : 0.0f;
        const float stepA = (float)(dt / 600.0);
        ambientAlpha += std::clamp(wantAmbient - ambientAlpha, -stepA, stepA);

        // 1. The field: dim characters, with slow brighter clouds drifting through.
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int i = y * W + x;
                const float cloud = (0.5f + 0.5f * std::sin(x * 0.21f + now * 0.00031 + std::sin(y * 0.3f + now * 0.0002) * 1.7f))
                                  * (0.5f + 0.5f * std::sin(y * 0.37f - now * 0.00023 + x * 0.05f));
                add(i, pal.field, (0.035f + 0.07f * cloud * cloud) * (0.25f + 0.75f * glyph[i]));
            }
        }

        // 2. Attract mode: two big shaded spheres of brighter characters.
        if (ambientAlpha > 0.01f) {
            const double a = now * 0.0004;
            float L[3];
            lightFrom(std::cos(a) * 0.7f, -0.45f, 0.55f, L);
            for (int k = 0; k < 2; ++k) {
                const float cx = W / 2.0f + W * 0.3f * std::sin(now * 0.00011 + k * 2.6);
                const float cy = H / 2.0f + H * 0.15f * std::sin(now * 0.00017 + k * 1.3);
                const float rad = (7.5f + 1.5f * std::sin(now * 0.0005 + k * 2.0)) * std::min((float)W, H * 2.4f) / 60.0f;
                sphere(cx, cy, rad, L, [&](float lam, int i) {
                    const float kk = (0.06f + 0.5f * std::pow(lam, 1.4f)) * (0.3f + 0.7f * glyph[i]) * ambientAlpha;
                    add(i, mix(pal.ambient, pal.hi, lam * lam), kk);
                });
            }
        }

        // 3. People: brighter characters, rounded off toward their edges, and
        // brighter the nearer they are. Dimmer during a round so the orbs stand out.
        distanceToEdge();
        const float peopleK = mode == Mode::Round ? 0.35f : 0.8f;
        for (int i = 0; i < N; ++i) {
            if (!depth[i]) {
                continue;
            }
            const float f = clamp01((depth[i] - 500) / 3500.0f);
            const float shade = 0.3f + 0.7f * std::sqrt(std::min<int>(dist[i], 3) / 3.0f);
            add(i, mix(pal.nearC, pal.farC, f), (1.0f - 0.5f * f) * shade * (0.5f + 0.5f * glyph[i]) * peopleK);
        }

        // 4. Movement leaves glowing ripples that spread out and fade. Kept
        // short: something that never stops moving (a spinning ceiling fan, a
        // crowd waving) feeds it every frame, and with a longer fade the glow
        // spread over half the wall and hid the people making it.
        for (int i = 0; i < N; ++i) {
            const uint8_t lit = depth[i] ? 1 : 0;
            if (lit != prevLit[i]) {
                energy[i] = std::min(1.0f, energy[i] + 0.35f);
            }
            prevLit[i] = lit;
        }
        const float keep = std::exp((float)(-dt / 250.0));
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int i = y * W + x;
                const float n = (x > 0 ? energy[i - 1] : 0) + (x < W - 1 ? energy[i + 1] : 0)
                              + (y > 0 ? energy[i - W] : 0) + (y < H - 1 ? energy[i + W] : 0);
                energyTmp[i] = (energy[i] * 0.6f + n * 0.08f) * keep;
            }
        }
        energy.swap(energyTmp);
        for (int i = 0; i < N; ++i) {
            if (energy[i] > 0.02f) {
                add(i, pal.energy, energy[i] * 0.6f * (0.35f + 0.65f * glyph[i]));
            }
        }

        // 5. The round: orbs to pop, and the sparks they burst into.
        if (mode == Mode::Round || mode == Mode::Score) {
            stepOrbs(dt, now, pal);
        }
        stepSparks(dt);

        // 6. Words and numbers, made of the same flickering characters.
        if (mode == Mode::Countdown) {
            const std::string s = modeMs < 3000 ? std::to_string(3 - (int)(modeMs / 1000)) : "GO";
            const int scl = std::max(1, std::min((s.size() == 1 ? 4 : 3) * H / 25, W / (int)(s.size() * 4)));
            text(s, (W - ((int)s.size() * 4 - 1) * scl) / 2, (H - 5 * scl) / 2, scl, pal.text, 1.0f);
        } else if (mode == Mode::Round) {
            const float left = 1.0f - (float)(modeMs / set.roundMs);
            for (int x = 0; x < (int)std::ceil(left * W); ++x) {
                add(x, pal.timer, 0.28f);
            }
            const std::string s = std::to_string(score);
            text(s, W - ((int)s.size() * 4 - 1) - 1, 2, 1, pal.text, 0.45f);
        } else if (mode == Mode::Score) {
            const std::string s = std::to_string(score);
            const float fade = clamp01((float)((6000 - modeMs) / 600)) * clamp01((float)(modeMs / 400));
            const int scl = std::max(1, std::min(H / 12, W / 19));
            text("SCORE", (W - 19 * scl) / 2, H / 2 - 6 * scl + scl / 2 + 1, scl, pal.text, 0.9f * fade);
            text(s, (W - ((int)s.size() * 4 - 1) * scl) / 2, H / 2 + scl, scl, pal.orbs[0], fade);
        }
    }

    void stepOrbs(double dt, double now, const Palette &pal) {
        const float t = (float)(modeMs / set.roundMs);
        const float unit = std::min((float)W, H * 2.4f) / 60.0f;   // sizes are tuned on a 60x25 wall
        if (mode == Mode::Round) {
            spawnInMs -= dt;
            const int maxOrbs = (int)std::lround(3 + 3 * t);
            int alive = 0;
            for (auto &o : orbs) {
                alive += o.dying ? 0 : 1;
            }
            if (spawnInMs <= 0 && alive < maxOrbs) {
                const bool gold = rnd() < 0.12f;
                const float rad = (gold ? 2.2f : 2.6f + rnd() * 1.6f) * unit;
                const float sp = (gold ? 7.0f : 2.5f + rnd() * 3.0f) * unit / 1000.0f;
                const float ang = rnd() * 6.2831853f;
                // Appear in open space, two LEDs clear of everyone, so an orb
                // has to be reached for rather than popping as it shows up.
                for (int tries = 0; tries < 12; ++tries) {
                    const float x = rad + 1 + rnd() * (W - 2 * rad - 2);
                    const float y = 3 + rad + rnd() * (H - 2 * rad - 4);
                    int busy = 0;
                    const float flat[3] = {0, 0, 1};
                    sphere(x, y, rad + 2, flat, [&](float, int i) { busy += person[i]; });
                    if (busy) {
                        continue;
                    }
                    Orb o;
                    o.x = x;
                    o.y = y;
                    o.vx = std::cos(ang) * sp;
                    o.vy = std::sin(ang) * sp * 0.6f;
                    o.rad = rad;
                    o.gold = gold;
                    o.age = 0;
                    o.life = gold ? 6000.0f : 9000.0f + rnd() * 4000.0f;
                    o.phase = rnd() * 6.0f;
                    o.alpha = 0;
                    o.col = gold ? GOLD : pal.orbs[(size_t)(rnd() * 3.999f)];
                    o.dying = false;
                    o.popped = false;
                    orbs.push_back(o);
                    break;
                }
                spawnInMs = 1300 - 800 * t;
            }
        }
        const double a = now * 0.0007;
        float L[3];
        lightFrom(std::cos(a) * 0.6f, -0.55f, 0.6f, L);
        const float flat[3] = {0, 0, 1};
        for (auto &o : orbs) {
            o.age += dt;
            if (o.age > o.life) {
                o.dying = true;
            }
            o.alpha = o.dying ? o.alpha - (float)(dt / 500.0) : std::min(1.0f, o.alpha + (float)(dt / 400.0));
            o.x += o.vx * dt;
            o.y += o.vy * dt + std::sin(o.age * 0.002f + o.phase) * 0.004f * unit * dt;
            if (o.x < o.rad) { o.x = o.rad; o.vx = std::fabs(o.vx); }
            if (o.x > W - o.rad) { o.x = W - o.rad; o.vx = -std::fabs(o.vx); }
            if (o.y < 1 + o.rad) { o.y = 1 + o.rad; o.vy = std::fabs(o.vy); }
            if (o.y > H - o.rad) { o.y = H - o.rad; o.vy = -std::fabs(o.vy); }
            // Touched? Count the person's LEDs inside it.
            int hit = 0, area = 0;
            sphere(o.x, o.y, o.rad, flat, [&](float, int i) { ++area; hit += person[i]; });
            if (mode == Mode::Round && !o.dying && o.alpha > 0.5f && hit >= std::max(1.0f, area * 0.12f)) {
                pop(o);
                continue;
            }
            const float rad = o.rad * (0.92f + 0.08f * std::sin(o.age * 0.006f + o.phase));
            sphere(o.x, o.y, rad, L, [&](float lam, int i) {
                const float k = (0.35f + 1.3f * std::pow(lam, 1.2f)) * (0.75f + 0.25f * glyph[i]);
                add(i, mix(o.col, WHITE, lam * lam * 0.6f), k);
                if (o.gold && rnd() < 0.06f) {
                    add(i, WHITE, 0.8f);
                }
            }, std::max(0.0f, o.alpha));
        }
        orbs.erase(std::remove_if(orbs.begin(), orbs.end(), [](const Orb &o) { return o.popped || o.alpha <= 0.0f; }),
                   orbs.end());
    }

    void pop(Orb &o) {
        o.popped = true;
        score += o.gold ? 5 : 1;
        popSound(o.gold);
        const int n = (int)std::lround(10 + o.rad * 4);
        for (int k = 0; k < n; ++k) {
            const float ang = rnd() * 6.2831853f, sp = (8.0f + rnd() * 16.0f) / 1000.0f;
            sparks.push_back({o.x, o.y, std::cos(ang) * sp, std::sin(ang) * sp, 0.0f, 500.0f + rnd() * 500.0f, o.col});
        }
        for (int k = 0; k < 24; ++k) {   // a ring of ripple where the orb was
            const float ang = k / 24.0f * 6.2831853f;
            const int x = (int)std::lround(o.x + std::cos(ang) * (o.rad + 1));
            const int y = (int)std::lround(o.y + std::sin(ang) * (o.rad + 1));
            if (x >= 0 && x < W && y >= 0 && y < H) {
                energy[y * W + x] = std::min(1.5f, energy[y * W + x] + 0.6f);
            }
        }
    }

    // Pops in quick succession climb a pentatonic scale (Pop1..Pop8), so a
    // group popping a lot hears a run of notes rather than one sound over and
    // over; after a pause it starts again low, at a random step. Several orbs
    // popped in the same instant play once. The gold orb always sparkles.
    void popSound(bool gold) {
        const double now = steadyMs();
        if (gold) {
            sound("GoldPop");
            lastPopMs = now;
            return;
        }
        if (now - lastPopMs < 70) {
            return;
        }
        popNote = now - lastPopMs < 1200 ? (popNote + 1) % 8 : (int)(rnd() * 2.999f);
        lastPopMs = now;
        sound(POP_NAMES[popNote]);
    }
    static constexpr const char *POP_NAMES[8] = {"Pop1", "Pop2", "Pop3", "Pop4", "Pop5", "Pop6", "Pop7", "Pop8"};

    void stepSparks(double dt) {
        const float drag = std::pow(0.5f, (float)(dt / 400.0));
        for (auto &s : sparks) {
            s.age += dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.vx *= drag;
            s.vy *= drag;
            const int x = (int)std::lround(s.x), y = (int)std::lround(s.y);
            const float k = 1.0f - s.age / s.life;
            if (k > 0 && x >= 0 && x < W && y >= 0 && y < H) {
                add(y * W + x, mix(s.col, WHITE, k * 0.5f), 1.1f * k);
            }
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark &s) { return s.age >= s.life; }),
                     sparks.end());
    }

    void text(const std::string &s, int x, int y, int scl, const RGB &col, float k) {
        for (char ch : s) {
            auto it = FONT.find(ch);
            if (it != FONT.end()) {
                for (int fy = 0; fy < 5; ++fy) {
                    for (int fx = 0; fx < 3; ++fx) {
                        if (it->second[fy * 3 + fx] != '1') {
                            continue;
                        }
                        for (int sy = 0; sy < scl; ++sy) {
                            for (int sx = 0; sx < scl; ++sx) {
                                const int px = x + fx * scl + sx, py = y + fy * scl + sy;
                                if (px >= 0 && px < W && py >= 0 && py < H) {
                                    const int i = py * W + px;
                                    add(i, col, k * (0.75f + 0.25f * glyph[i]));
                                }
                            }
                        }
                    }
                }
            }
            x += 4 * scl;
        }
    }

    // City-block distance from each lit LED to the nearest unlit one, capped at 4.
    void distanceToEdge() {
        for (int i = 0; i < N; ++i) {
            dist[i] = depth[i] ? 4 : 0;
        }
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int i = y * W + x;
                if (dist[i]) {
                    dist[i] = std::min<int>({dist[i], 1 + (x > 0 ? dist[i - 1] : 0), 1 + (y > 0 ? dist[i - W] : 0)});
                }
            }
        }
        for (int y = H - 1; y >= 0; --y) {
            for (int x = W - 1; x >= 0; --x) {
                const int i = y * W + x;
                if (dist[i]) {
                    dist[i] = std::min<int>({dist[i], 1 + (x < W - 1 ? dist[i + 1] : 0), 1 + (y < H - 1 ? dist[i + W] : 0)});
                }
            }
        }
    }

    OrbPopSettings set;
    std::mt19937 rng;
    KinectPeople::Reader kinect;
    int W = 0, H = 0, N = 0;

    std::vector<float> glyph, energy, energyTmp, R, G, B;
    std::vector<double> glyphNext;
    std::vector<uint8_t> prevLit, dist, person, rawFlags;
    std::vector<uint16_t> depth, rawDepth;

    Mode mode = Mode::Mirror;
    double modeMs = 0, heldMs = 0, awayMs = 0, cooldownMs = 0, spawnInMs = 0, kinectGoneMs = 0;
    float ambientAlpha = 1.0f;
    std::vector<Orb> orbs;
    std::vector<Spark> sparks;
    int score = 0;
    bool ending = false;
    int countBeat = -1, lastTick = -1, popNote = 0;
    double lastPopMs = -1e9;
    bool scoreSounded = false;
    ArcadeShow show;
    ArcadeCanvas::Canvas showCanvas;
    std::atomic<bool> skipShow{false};
};


FPPOrbPop::FPPOrbPop(Json::Value &config) : FPPArcadeGame(config) {
    if (findOption("Start Automatically", "0") == "1") {
        char buf[64];
        snprintf(buf, sizeof(buf), "ArcadeOrbPopAutoStart-%p", (void *)this);
        timerName = buf;
        Timers::INSTANCE.addPeriodicTimer(timerName, 1000, [this]() { autoStartTick(); });
    }
}

FPPOrbPop::~FPPOrbPop() {
    release();
}

void FPPOrbPop::release() {
    if (!timerName.empty()) {
        Timers::INSTANCE.stopPeriodicTimer(timerName);
        timerName.clear();
    }
}

const std::string &FPPOrbPop::getName() {
    static const std::string name = "Orb Pop";
    return name;
}

// Take the model over whenever it is showing the plain Kinect picture (or
// nothing) and the Kinect is sending frames. Another game, or its title
// screen, is left alone; when it ends the Kinect plugin puts its picture back
// and Orb Pop takes over again a second later.
void FPPOrbPop::autoStartTick() {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (!m) {
        return;
    }
    {
        std::lock_guard<std::recursive_mutex> l(m->getRunningEffectMutex());
        RunningEffect *re = m->getRunningEffect();
        if (re && re->name() != "KinectBlit") {
            return;
        }
    }
    static KinectPeople::Reader probe;
    int w, h;
    static std::vector<uint16_t> d(KinectPeople::MAX_W * KinectPeople::MAX_H);
    static std::vector<uint8_t> f(KinectPeople::MAX_W * KinectPeople::MAX_H);
    if (!probe.read(w, h, d.data(), f.data())) {
        return;
    }
    LogInfo(VB_PLUGIN, "Orb Pop: Kinect is streaming and %s is free, starting\n", modelName.c_str());
    startEffect();
}

void FPPOrbPop::startEffect() {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (!m) {
        return;
    }
    if (findOption("overlay", "Overwrite") == "Transparent") {
        m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
    } else {
        m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
    }
    OrbPopSettings s;
    s.brightness = std::clamp(findIntOption("Brightness", 50), 1, 100);
    s.palette = findOption("Palette", "Frost") == "Christmas" ? &CHRISTMAS : &FROST;
    s.roundMs = std::clamp(findIntOption("Round Length", 45), 10, 600) * 1000.0;
    s.startHoldMs = std::clamp(findIntOption("Start After", 2), 0, 60) * 1000.0;
    s.presencePct = (float)std::clamp(findIntOption("Sensitivity", 3), 1, 100);
    s.endShow = endShowOption();
    s.goodGame = goodGameOption(50);
    m->setRunningEffect(new OrbPopEffect(m, s), 1);
}

void FPPOrbPop::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    OrbPopEffect *effect = dynamic_cast<OrbPopEffect *>(m->getRunningEffect());
    if (effect) {
        effect->button(button);
        return;
    }
    // Not running: any button starts it, going straight to a round's
    // countdown, like pressing a button starts the other games.
    startEffect();
    if ((effect = dynamic_cast<OrbPopEffect *>(m->getRunningEffect())) != nullptr) {
        effect->button("A Button - Pressed");
    }
}
