#include <fpp-pch.h>

#include "FPPSimpleGames.h"
#include "ArcadeCanvas.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <random>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;

namespace {

RGB hue(float h) {
    h = std::fmod(h, 1.0f) * 6.0f;
    const float x = 1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f);
    switch ((int)h) {
    case 0: return {1, x, 0};
    case 1: return {x, 1, 0};
    case 2: return {0, 1, x};
    case 3: return {0, x, 1};
    case 4: return {x, 0, 1};
    default: return {1, 0, x};
    }
}

}  // namespace

// What the three games share: presses are queued from the command thread and
// handled on the effect's own thread, so game state is only ever touched in
// one place; the pause menu, the GAME OVER screen, and handing the model back.
class SimpleGameEffect : public FPPArcadeGameEffect {
public:
    SimpleGameEffect(PixelOverlayModel *m) : FPPArcadeGameEffect(m), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(8, W);
        H = std::max(8, H);
        canvas.resize(W, H);
    }

    void button(const std::string &b) override {
        const bool wasPaused = paused;
        FPPArcadeGameEffect::button(b);
        if (wasPaused || paused || b == "Start - Pressed") {
            return;
        }
        static const std::string PRESSED = " - Pressed";
        if (b.size() > PRESSED.size() && b.compare(b.size() - PRESSED.size(), PRESSED.size(), PRESSED) == 0) {
            std::lock_guard<std::mutex> l(pressLock);
            presses.push_back(b.substr(0, b.size() - PRESSED.size()));
        }
    }

    void restart() override {
        over = false;
        overT = 0;
        {
            std::lock_guard<std::mutex> l(pressLock);
            presses.clear();
        }
        reset();
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
        const float dt = (float)(consumeElapsedMs(FRAME_MS, 100.0) / 1000.0);
        clock += dt;
        std::vector<std::string> todo;
        {
            std::lock_guard<std::mutex> l(pressLock);
            todo.swap(presses);
        }
        if (!over) {
            for (auto &p : todo) {
                onPress(p);
            }
            stepGame(dt);
        } else {
            overT += dt;
            stepOver(dt);
            if (overT > 5.0f) {
                model->clearOverlayBuffer();
                model->flushOverlayBuffer();
                finished = true;
                return EFFECT_AFTER_NEXT_OUTPUT;
            }
        }
        canvas.clear();
        draw();
        canvas.writeTo(model);
        if (over && overT > 1.0f) {
            textBox({overTitle[0], overTitle[1], std::to_string(overScore)}, 255, 255, 255);
        } else {
            drawText();
        }
        model->setOverlayBufferDirty(true);
        return FRAME_MS;
    }

protected:
    static constexpr int FRAME_MS = 25;

    virtual void reset() = 0;
    virtual void onPress(const std::string &name) = 0;   // "A Button", "Up", ...
    virtual void stepGame(float dt) = 0;
    virtual void stepOver(float dt) {}
    virtual void draw() = 0;
    virtual void drawText() {}

    void gameOver(int score, const char *line1 = "GAME", const char *line2 = "OVER") {
        over = true;
        overT = 0;
        overScore = score;
        overTitle[0] = line1;
        overTitle[1] = line2;
    }
    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 0, H = 0;
    float clock = 0;
    bool over = false;
    float overT = 0;

private:
    std::mutex pressLock;
    std::vector<std::string> presses;
    int overScore = 0;
    std::string overTitle[2];
    bool finished = false;
};

// Starting any of them: the first press only starts it (so it can't, say, drop
// the first Stacker row before you've seen it move); after that presses play.
template <typename EFFECT>
static void startOrForward(const std::string &modelName, const std::string &overlay, const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    EFFECT *effect = dynamic_cast<EFFECT *>(m->getRunningEffect());
    if (effect) {
        effect->button(button);
        return;
    }
    if (overlay == "Transparent") {
        m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
    } else {
        m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
    }
    m->setRunningEffect(new EFFECT(m), 25);
}

// ======================================================================== Stacker

class StackerEffect : public SimpleGameEffect {
public:
    StackerEffect(PixelOverlayModel *m) : SimpleGameEffect(m) {
        layerH = H >= 20 ? 2 : 1;
        layers = H / layerH;
        reset();
    }
    const std::string &name() const override {
        static std::string NAME = "Stacker";
        return NAME;
    }

private:
    struct Chunk {
        float x0, x1, y, vy;
        RGB col;
    };

    void reset() override {
        stack.clear();
        chunks.clear();
        width = std::max(3, W / 8);
        newRow();
        waitT = 0.6f;
    }

    void newRow() {
        dir = rnd() < 0.5f ? 1 : -1;
        x = dir > 0 ? 0.0f : (float)(W - width);
    }
    float speed() const { return std::min(45.0f, 9.0f + 1.6f * stack.size()); }
    int rowY(int layer) const { return H - (layer + 1) * layerH; }
    RGB layerColour(int layer) const { return hue(layer / (float)layers * 0.85f); }

    void onPress(const std::string &) override {
        if (waitT > 0) {
            return;   // a moment between rows, so one press can't drop two
        }
        const int x0 = (int)std::lround(x), x1 = x0 + width - 1;
        int lo = x0, hi = x1;
        if (!stack.empty()) {
            lo = std::max(x0, stack.back().first);
            hi = std::min(x1, stack.back().second);
        }
        const int layer = (int)stack.size();
        const RGB col = layerColour(layer);
        // The overhang drops off.
        if (hi < lo) {
            chunks.push_back({(float)x0, (float)x1, (float)rowY(layer), 0, col});
            gameOver(layer);
            return;
        }
        if (x0 < lo) chunks.push_back({(float)x0, (float)lo - 1, (float)rowY(layer), 0, col});
        if (x1 > hi) chunks.push_back({(float)hi + 1, (float)x1, (float)rowY(layer), 0, col});
        stack.push_back({lo, hi});
        width = hi - lo + 1;
        if ((int)stack.size() >= layers) {
            gameOver((int)stack.size(), "YOU", "WIN");
            return;
        }
        newRow();
        waitT = 0.25f;
    }

    void stepChunks(float dt) {
        for (auto &c : chunks) {
            c.vy += 40.0f * dt;
            c.y += c.vy * dt;
        }
        chunks.erase(std::remove_if(chunks.begin(), chunks.end(), [&](const Chunk &c) { return c.y > H; }), chunks.end());
    }

    void stepGame(float dt) override {
        waitT = std::max(0.0f, waitT - dt);
        x += dir * speed() * dt;
        if (x <= 0) {
            x = 0;
            dir = 1;
        } else if (x >= W - width) {
            x = (float)(W - width);
            dir = -1;
        }
        stepChunks(dt);
    }
    void stepOver(float dt) override { stepChunks(dt); }

    void draw() override {
        const bool won = over && (int)stack.size() >= layers;
        for (int k = 0; k < (int)stack.size(); ++k) {
            const RGB col = won ? hue(clock * 0.5f + k * 0.05f) : layerColour(k);
            for (int xx = stack[k].first; xx <= stack[k].second; ++xx) {
                for (int dy = 0; dy < layerH; ++dy) {
                    canvas.add(xx, rowY(k) + dy, col, 0.7f);
                }
            }
        }
        if (!over) {
            const int layer = (int)stack.size(), x0 = (int)std::lround(x);
            for (int xx = x0; xx < x0 + width; ++xx) {
                for (int dy = 0; dy < layerH; ++dy) {
                    canvas.add(xx, rowY(layer) + dy, ArcadeCanvas::mix(layerColour(layer), ArcadeCanvas::WHITE, 0.3f), 1.0f);
                }
            }
        }
        for (auto &c : chunks) {
            for (int xx = (int)c.x0; xx <= (int)c.x1; ++xx) {
                for (int dy = 0; dy < layerH; ++dy) {
                    canvas.add(xx, (int)c.y + dy, c.col, 0.5f);
                }
            }
        }
    }

    int layerH = 2, layers = 12, width = 7, dir = 1;
    float x = 0, waitT = 0;
    std::vector<std::pair<int, int>> stack;
    std::vector<Chunk> chunks;
};

const std::string &FPPStacker::getName() {
    static const std::string name = "Stacker";
    return name;
}
void FPPStacker::button(const std::string &button) {
    startOrForward<StackerEffect>(modelName, findOption("overlay", "Overwrite"), button);
}

// ======================================================================== Flap

class FlapEffect : public SimpleGameEffect {
public:
    FlapEffect(PixelOverlayModel *m) : SimpleGameEffect(m) { reset(); }
    const std::string &name() const override {
        static std::string NAME = "Flap";
        return NAME;
    }

private:
    struct Pipe {
        float x;
        int gapTop, gap;
        bool passed;
    };

    void reset() override {
        birdX = W / 4;
        y = H / 2.0f;
        vy = 0;
        flying = false;
        score = 0;
        pipes.clear();
        nextPipe = (float)W;
    }

    void onPress(const std::string &name) override {
        if (name == "A Button" || name == "B Button" || name == "Up" || name == "Fire") {
            flying = true;
            vy = -11.0f;
            flapT = 0.15f;
        }
    }

    float speed() const { return std::min(16.0f, 9.0f + 0.2f * score); }

    void stepGame(float dt) override {
        flapT = std::max(0.0f, flapT - dt);
        if (!flying) {
            y = H / 2.0f + std::sin(clock * 4.0f);   // bob until the first flap
            return;
        }
        vy = std::min(18.0f, vy + 42.0f * dt);
        y = std::max(0.0f, y + vy * dt);
        nextPipe -= speed() * dt;
        if (nextPipe <= W) {
            const int gap = std::max(5, std::min(H / 3, 8) - score / 6);
            const int gapTop = 1 + (int)(rnd() * (H - gap - 2));
            pipes.push_back({nextPipe, gapTop, gap, false});
            nextPipe += std::max(14, W / 3);
        }
        for (auto &p : pipes) {
            p.x -= speed() * dt;
            if (!p.passed && p.x + 1 < birdX) {
                p.passed = true;
                ++score;
            }
        }
        pipes.erase(std::remove_if(pipes.begin(), pipes.end(), [](const Pipe &p) { return p.x < -3; }), pipes.end());
        const int by = (int)y;
        if (by >= H) {
            gameOver(score);
            return;
        }
        for (auto &p : pipes) {
            const int px = (int)std::floor(p.x);
            if (birdX >= px && birdX <= px + 1 && (by < p.gapTop || by >= p.gapTop + p.gap)) {
                gameOver(score);
                return;
            }
        }
    }

    void draw() override {
        for (int yy = 0; yy < H; ++yy) {
            for (int xx = 0; xx < W; ++xx) {
                canvas.add(xx, yy, {0.1f, 0.2f, 0.6f}, 0.03f * yy / H);   // a little night sky
            }
        }
        for (auto &p : pipes) {
            const int px = (int)std::floor(p.x);
            for (int yy = 0; yy < H; ++yy) {
                if (yy >= p.gapTop && yy < p.gapTop + p.gap) {
                    continue;
                }
                const bool lip = yy == p.gapTop - 1 || yy == p.gapTop + p.gap;
                canvas.add(px, yy, {0.1f, 0.85f, 0.2f}, lip ? 0.8f : 0.4f);
                canvas.add(px + 1, yy, {0.1f, 0.85f, 0.2f}, lip ? 0.8f : 0.25f);
            }
        }
        const int by = std::min(H - 1, (int)y);
        if (!over || std::fmod(overT, 0.3f) < 0.15f) {
            canvas.add(birdX, by, {1.0f, 0.85f, 0.1f}, 1.0f);
            canvas.add(birdX - 1, by + (flapT > 0 ? -1 : 0), {1.0f, 0.45f, 0.05f}, 0.8f);   // the wing
            canvas.add(birdX + 1, by, {1.0f, 0.3f, 0.1f}, 0.5f);                               // the beak
        }
    }

    void drawText() override {
        PanelTransform pt(this);
        if (!flying) {
            outputString("FLAP", centerTextX("FLAP", 1), 2, 255, 220, 60, 1);
        } else {
            const std::string s = std::to_string(score);
            outputString(s, W - (int)s.size() * 4, 1, 120, 120, 160, 1);
        }
    }

    int birdX = 15, score = 0;
    float y = 0, vy = 0, nextPipe = 0, flapT = 0;
    bool flying = false;
    std::vector<Pipe> pipes;
};

const std::string &FPPFlap::getName() {
    static const std::string name = "Flap";
    return name;
}
void FPPFlap::button(const std::string &button) {
    startOrForward<FlapEffect>(modelName, findOption("overlay", "Overwrite"), button);
}

// ======================================================================== Light Memory

class LightMemoryEffect : public SimpleGameEffect {
public:
    LightMemoryEffect(PixelOverlayModel *m) : SimpleGameEffect(m) {
        // Which triangle each LED belongs to: up, right, down, left - or none,
        // on the dark lines between them.
        region.assign(W * H, -1);
        for (int yy = 0; yy < H; ++yy) {
            for (int xx = 0; xx < W; ++xx) {
                const float nx = (xx + 0.5f - W / 2.0f) / (W / 2.0f), ny = (yy + 0.5f - H / 2.0f) / (H / 2.0f);
                if (std::fabs(std::fabs(nx) - std::fabs(ny)) < 0.08f || nx * nx + ny * ny < 0.04f) {
                    continue;
                }
                region[yy * W + xx] = std::fabs(nx) > std::fabs(ny) ? (nx > 0 ? 1 : 3) : (ny < 0 ? 0 : 2);
            }
        }
        reset();
    }
    const std::string &name() const override {
        static std::string NAME = "LightMemory";
        return NAME;
    }

private:
    enum class Phase { Wait, Show, Input };

    void reset() override {
        seq.clear();
        phase = Phase::Wait;
        phaseT = 0;
        lit = -1;
        litT = 0;
        addStep();
    }

    void addStep() {
        seq.push_back((int)(rnd() * 4) % 4);
        phase = Phase::Wait;
        phaseT = 0;
        pos = 0;
    }

    float onTime() const { return std::max(0.22f, 0.6f - 0.03f * seq.size()); }

    void onPress(const std::string &name) override {
        if (phase != Phase::Input) {
            return;
        }
        int r = -1;
        if (name == "Up") r = 0;
        else if (name == "Right") r = 1;
        else if (name == "Down") r = 2;
        else if (name == "Left") r = 3;
        if (r < 0) {
            return;
        }
        lit = r;
        litT = 0.25f;
        phaseT = 0;
        if (r != seq[pos]) {
            wrong = seq[pos];
            gameOver((int)seq.size() - 1);
            return;
        }
        if (++pos >= (int)seq.size()) {
            addStep();
        }
    }

    void stepGame(float dt) override {
        phaseT += dt;
        litT = std::max(0.0f, litT - dt);
        if (litT == 0 && phase != Phase::Show) {
            lit = -1;
        }
        switch (phase) {
        case Phase::Wait:
            if (phaseT > 0.9f) {
                phase = Phase::Show;
                phaseT = 0;
                pos = 0;
            }
            break;
        case Phase::Show: {
            // Each step lit for onTime(), with a short dark gap after it.
            const float slot = onTime() + 0.15f;
            const int k = (int)(phaseT / slot);
            if (k >= (int)seq.size()) {
                phase = Phase::Input;
                phaseT = 0;
                pos = 0;
                lit = -1;
            } else {
                lit = (phaseT - k * slot) < onTime() ? seq[k] : -1;
            }
            break;
        }
        case Phase::Input:
            if (phaseT > 6.0f) {
                wrong = seq[pos];
                gameOver((int)seq.size() - 1);   // too slow
            }
            break;
        }
    }

    void draw() override {
        static const RGB COLOURS[4] = {{0.1f, 1.0f, 0.2f}, {1.0f, 0.1f, 0.1f}, {1.0f, 0.85f, 0.0f}, {0.1f, 0.3f, 1.0f}};
        for (int i = 0; i < W * H; ++i) {
            const int r = region[i];
            if (r < 0) {
                continue;
            }
            float k = r == lit ? 0.95f : 0.07f;
            if (over && r == wrong) {
                k = std::fmod(overT, 0.4f) < 0.2f ? 0.95f : 0.1f;   // the one you should have pressed
            }
            canvas.add(i % W, i / W, COLOURS[r], k);
        }
        // Your turn: a soft white dot in the middle.
        if (phase == Phase::Input && !over) {
            canvas.add(W / 2, H / 2, ArcadeCanvas::WHITE, 0.5f + 0.3f * std::sin(clock * 6.0f));
        }
    }

    std::vector<int> region, seq;
    Phase phase = Phase::Wait;
    float phaseT = 0, litT = 0;
    int lit = -1, pos = 0, wrong = -1;
};

const std::string &FPPLightMemory::getName() {
    static const std::string name = "Light Memory";
    return name;
}
void FPPLightMemory::button(const std::string &button) {
    startOrForward<LightMemoryEffect>(modelName, findOption("overlay", "Overwrite"), button);
}
