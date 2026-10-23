#include <fpp-pch.h>

#include "FPPShapeFit.h"
#include "ArcadeCanvas.h"
#include "KinectPeople.h"
#include "ShapeFitShow.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;

// The poses, their drawing and the game's colours are in ShapeFitShow.h, so
// the end show can dance the same outlines.
using ShapeFitShow::CLOCK;
using ShapeFitShow::INSIDE;
using ShapeFitShow::KINECT_STRETCH;
using ShapeFitShow::OUTLINE;
using ShapeFitShow::OUTSIDE;
using ShapeFitShow::POSES;
using ShapeFitShow::Pose;
using ShapeFitShow::segDist;

namespace {
constexpr int ROUNDS = 5;
}  // namespace

class ShapeFitEffect : public FPPArcadeGameEffect {
public:
    ShapeFitEffect(PixelOverlayModel *m, int sizePct, int seconds, int showMode = 1, int goodTotal = 300)
        : FPPArcadeGameEffect(m), rng(std::random_device{}()), endShow(showMode), goodGame(goodTotal) {
        model->getSize(W, H);
        W = std::max(10, W);
        H = std::max(10, H);
        N = W * H;
        canvas.resize(W, H);
        lit.assign(N, 0);
        shape.assign(N, 0);
        rawDepth.resize(KinectPeople::MAX_W * KinectPeople::MAX_H);
        rawFlags.resize(KinectPeople::MAX_W * KinectPeople::MAX_H);
        sizeK = std::clamp(sizePct, 30, 100) / 100.0f;
        roundTime = (float)std::clamp(seconds, 2, 20);
        for (int k = 0; k < (int)POSES.size(); ++k) {
            order.push_back(k);
        }
        std::shuffle(order.begin(), order.end(), rng);
        startRound();
    }

    const std::string &name() const override {
        static std::string NAME = "ShapeFit";
        return NAME;
    }

    void button(const std::string &b) override {
        // Any press skips the end show (once it's had two seconds); nothing
        // else happens until it's over.
        if (showing) {
            if (b.find("Pressed") != std::string::npos) {
                skipShow.store(true);
            }
            return;
        }
        FPPArcadeGameEffect::button(b);
    }

    void restart() override {
        round = 0;
        total = 0;
        std::shuffle(order.begin(), order.end(), rng);
        startRound();
        resetFrameTimer();
    }

    int32_t update() override {
        if (finished) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return EFFECT_DONE;
        }
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return FRAME_MS;
        }
        const double dtMs = consumeElapsedMs(FRAME_MS, 100.0);
        const float dt = (float)(dtMs / 1000.0);
        // The end show plays instead of the total; the Kinect isn't needed for
        // it, so losing it doesn't cut the show short. Drawn as the game's own
        // frames are, flushed on FPP's schedule.
        if (showing) {
            if (skipShow.exchange(false) && show.elapsedMs() > 2000.0) {
                show.finish();
            }
            show.step(dtMs, canvas);
            canvas.writeTo(model);
            model->setOverlayBufferDirty(true);
            if (show.over()) {
                finish();   // as the total screen ends
                return EFFECT_AFTER_NEXT_OUTPUT;
            }
            return FRAME_MS;
        }
        clock += dt;
        stateT += dt;
        const bool haveKinect = readKinect();
        noKinectT = haveKinect ? 0 : noKinectT + dt;

        switch (state) {
        case State::Pose: {
            // A tick as each second shown in the corner goes by, sharper for
            // the last three. None while the pose name is up: it has its own.
            const int secs = (int)std::ceil(roundTime - stateT);
            if (secs != lastTick && secs >= 1) {
                lastTick = secs;
                sfx("shapefit", secs <= 3 ? "TickUrgent" : "Tick", secs <= 3 ? 1.0f : 0.6f);
            }
            if (stateT >= roundTime) {
                scoreRound();
                state = State::Result;
                stateT = 0;
                verdictSounded = false;
                sfx("shapefit", "Snap");
            }
            break;
        }
        case State::Result:
            if (!verdictSounded && stateT > 0.4f) {   // with the verdict appearing
                verdictSounded = true;
                sfx("shapefit", verdictSound(lastScore));
            }
            if (stateT >= 3.0f) {
                if (++round >= ROUNDS) {
                    sfxStop("shapefit");
                    // A good total (or a new best) gets the end show instead
                    // of the TOTAL screen.
                    int best = 0;
                    const bool newBest = arcadeRecordBest(model->getName(), "Shape Fit", total, &best);
                    if (arcadeWantShow(endShow, total >= goodGame, newBest)) {
                        startShow(newBest, best);
                        return FRAME_MS;
                    }
                    state = State::Total;
                    stateT = 0;
                    sfx("shapefit", "Total");
                } else {
                    startRound();
                }
            }
            break;
        case State::Total:
            if (stateT >= 6.0f) {
                finish();
                return EFFECT_AFTER_NEXT_OUTPUT;
            }
            break;
        }
        if (noKinectT > 5.0f) {
            finish();
            return EFFECT_AFTER_NEXT_OUTPUT;
        }
        render(haveKinect);
        model->setOverlayBufferDirty(true);
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 33;
    enum class State { Pose, Result, Total };

    void finish() {
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();
        finished = true;
    }

    void startShow(bool newBest, int best) {
        std::vector<int> played;
        for (int k = 0; k < ROUNDS; ++k) {
            played.push_back(order[k % order.size()]);
        }
        sfx("shapefit", "EndShow");
        skipShow.store(false);
        showing = true;
        show.start(W, H, ShapeFitShow::config(total, newBest, best), std::make_unique<ShapeFitShow::Story>(played));
    }

    bool readKinect() {
        int kw = 0, kh = 0;
        if (!kinect.read(kw, kh, rawDepth.data(), rawFlags.data())) {
            std::fill(lit.begin(), lit.end(), 0);
            return false;
        }
        for (int y = 0; y < H; ++y) {
            const int sy = y * kh / H;
            for (int x = 0; x < W; ++x) {
                lit[y * W + x] = (rawFlags[sy * kw + x * kw / W] & KinectPeople::LIT) ? 1 : 0;
            }
        }
        return true;
    }

    // Rasterise the pose: feet on the bottom row, centred, with a little
    // sideways wander from round to round.
    void startRound() {
        const Pose &p = POSES[order[round % order.size()]];
        poseName = p.name;
        const float tall = sizeK * H;
        const float wide = tall * KINECT_STRETCH;
        const float cx = W / 2.0f + (std::uniform_real_distribution<float>(-0.15f, 0.15f)(rng)) * W;
        const float topY = H - tall;
        area = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const float u = (x + 0.5f - cx) / wide, v = (y + 0.5f - topY) / tall;
                bool in = false;
                for (auto &pr : p.parts) {
                    if (segDist(u, v, pr) <= pr.r * 1.15f) {
                        in = true;
                        break;
                    }
                }
                shape[y * W + x] = in ? 1 : 0;
                area += in ? 1 : 0;
            }
        }
        state = State::Pose;
        stateT = 0;
        lastTick = (int)std::ceil(roundTime);
        sfx("shapefit", "Pose");
    }

    void scoreRound() {
        int inside = 0, outside = 0;
        for (int i = 0; i < N; ++i) {
            if (lit[i]) {
                (shape[i] ? inside : outside)++;
            }
        }
        const float fill = area ? (float)inside / area : 0.0f;
        const float spill = area ? (float)outside / area : 0.0f;
        lastScore = std::clamp((int)std::lround(100.0f * fill - 50.0f * spill), 0, 100);
        total += lastScore;
    }

    static const char *verdict(int s) {
        return s >= 85 ? "PERFECT" : s >= 65 ? "GREAT" : s >= 40 ? "GOOD" : s >= 15 ? "OK" : "MISS";
    }

    static const char *verdictSound(int s) {
        return s >= 85 ? "Perfect" : s >= 65 ? "Great" : s >= 40 ? "Good" : s >= 15 ? "Ok" : "Miss";
    }

    void render(bool haveKinect) {
        canvas.clear();
        const bool result = state == State::Result;
        const float pulse = 0.75f + 0.25f * std::sin(clock * 6.0f);
        if (state != State::Total) {
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    const int i = y * W + x;
                    if (!shape[i]) {
                        continue;
                    }
                    bool edge = false;
                    for (int d = 0; d < 4 && !edge; ++d) {
                        static const int ex[4] = {-1, 1, 0, 0}, ey[4] = {0, 0, -1, 1};
                        const int nx = x + ex[d], ny = y + ey[d];
                        edge = nx < 0 || ny < 0 || nx >= W || ny >= H || !shape[ny * W + nx];
                    }
                    canvas.add(x, y, OUTLINE, edge ? 0.55f * pulse : 0.06f);
                }
            }
        }
        // Everyone in front: green inside the shape, red outside it.
        for (int i = 0; i < N; ++i) {
            if (lit[i]) {
                const bool in = shape[i] && state != State::Total;
                canvas.add(i % W, i / W, in ? INSIDE : OUTSIDE, in ? 0.8f : (state == State::Total ? 0.25f : 0.6f));
            }
        }
        if (state == State::Pose) {
            // Time left: a bar draining along the top, and the seconds.
            const float left = 1.0f - stateT / roundTime;
            for (int x = 0; x < (int)std::ceil(left * W); ++x) {
                canvas.add(x, 0, CLOCK, 0.35f);
            }
        }
        if (result && std::fmod(stateT, 0.4f) < 0.2f) {
            canvas.scale(1.3f);
        }
        canvas.writeTo(model);

        if (!haveKinect && noKinectT > 1.0f) {
            textBox({"NO", "KINECT"}, 255, 80, 40);
        } else if (state == State::Pose && stateT < 1.0f) {
            textBox({poseName}, 120, 230, 255);   // what to do this round
        } else if (state == State::Pose) {
            const std::string s = std::to_string((int)std::ceil(roundTime - stateT));
            PanelTransform pt(this);
            outputString(s, W - (int)s.size() * 4, 2, 255, 220, 60, 1);
        } else if (result && stateT > 0.4f) {
            textBox({verdict(lastScore), std::to_string(lastScore)}, 255, 255, 255);
        } else if (state == State::Total) {
            textBox({"TOTAL", std::to_string(total)}, 255, 220, 0);
        }
    }

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    KinectPeople::Reader kinect;
    int W = 0, H = 0, N = 0;
    std::vector<uint8_t> lit, shape, rawFlags;
    std::vector<uint16_t> rawDepth;
    std::vector<int> order;

    float sizeK = 0.85f, roundTime = 6.0f;
    State state = State::Pose;
    float stateT = 0, clock = 0, noKinectT = 0;
    int round = 0, total = 0, lastScore = 0, area = 0;
    std::string poseName;
    bool finished = false;
    int lastTick = 0;
    bool verdictSounded = false;

    int endShow = 1, goodGame = 300;   // the End Show and Good Game settings
    ArcadeShow show;
    bool showing = false;
    std::atomic<bool> skipShow{false};
};


const std::string &FPPShapeFit::getName() {
    static const std::string name = "Shape Fit";
    return name;
}

void FPPShapeFit::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    ShapeFitEffect *effect = dynamic_cast<ShapeFitEffect *>(m->getRunningEffect());
    if (effect) {
        effect->button(button);   // Start pauses, as in every game
        return;
    }
    if (findOption("overlay", "Overwrite") == "Transparent") {
        m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
    } else {
        m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
    }
    m->setRunningEffect(new ShapeFitEffect(m, findIntOption("Shape Size", 85), findIntOption("Seconds", 6),
                                           endShowOption(), goodGameOption(300)),
                        33);
}
