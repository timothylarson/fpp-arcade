#include <fpp-pch.h>

#include "FPPSimpleGames.h"
#include "ArcadeCanvas.h"
#include "ArcadeShow.h"
#include "FlapShow.h"
#include "LightMemoryShow.h"
#include "StackerShow.h"

#include <algorithm>
#include <cctype>
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
// one place; the pause menu, the GAME OVER screen or the end show, and handing
// the model back.
class SimpleGameEffect : public FPPArcadeGameEffect {
public:
    // showMode and good: the End Show and Good Game settings.
    SimpleGameEffect(PixelOverlayModel *m, int showMode, int good)
        : FPPArcadeGameEffect(m), rng(std::random_device{}()), endShow(showMode), goodGame(good) {
        model->getSize(W, H);
        W = std::max(8, W);
        H = std::max(8, H);
        canvas.resize(W, H);
    }

    void button(const std::string &b) override {
        // During the end show a press skips it, and does nothing else.
        if (show.on()) {
            if (b.find("Pressed") != std::string::npos) {
                show.skip();
            }
            return;
        }
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
        showDue = false;
        showStory.reset();
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
        if (show.on()) {
            if (show.step(model, consumeElapsedMs(FRAME_MS, 100.0))) {
                return FRAME_MS;
            }
            return finish();   // over: the game ends as it does after GAME OVER
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
        const int cheat = takeCheat();
        if (!over) {
            if (cheat == CHEAT_LAST_LEVEL) {
                lastLevel();
            } else if (cheat == CHEAT_NEXT_LEVEL) {
                nextLevel();
            }
            for (auto &p : todo) {
                onPress(p);
            }
            stepGame(dt);
        } else {
            overT += dt;
            if (overT > 1.0f && overT - dt <= 1.0f) {
                sfxStop(folder());
                // A good game (or a new best) gets the end show where the
                // GAME OVER box would be, once the miss has had its second.
                if (showDue) {
                    showDue = false;
                    sound("EndShow");
                    show.start(model, std::move(showConfig), std::move(showStory));
                    resetFrameTimer();
                    return FRAME_MS;
                }
                sound("GameOver");   // with the GAME OVER box
            }
            stepOver(dt);
            if (overT > 5.0f) {
                return finish();
            }
        }
        canvas.clear();
        draw();
        beginFrame();   // the picture, then the score over it: see beginFrame()
        canvas.writeTo(model);
        if (over && overT > 1.0f) {
            textBox({overTitle[0], overTitle[1], overLine}, 255, 255, 255);
        } else {
            drawText();
        }
        endFrame();
        return FRAME_MS;
    }

protected:
    static constexpr int FRAME_MS = 25;

    virtual void reset() = 0;
    virtual void onPress(const std::string &name) = 0;   // "A Button", "Up", ...
    virtual void stepGame(float dt) = 0;
    // The cheat codes; only Stacker has levels.
    virtual void lastLevel() {}
    virtual void nextLevel() {}
    virtual void stepOver(float dt) {}
    virtual void draw() = 0;
    virtual void drawText() {}
    // The end show, asked once as the game ends while its numbers are still
    // as they finished: record the best (with earnsShow()) and, if it earns
    // the show, fill in cfg and story and return true.
    virtual bool endShowFor(ShowConfig &/*cfg*/, std::unique_ptr<ShowStory> &/*story*/) { return false; }

    // scoreText, if given, shows in the box instead of the score (Flap's
    // two players show both scores).
    void gameOver(int score, const char *line1 = "GAME", const char *line2 = "OVER", const std::string &scoreText = "") {
        // Only the first time: two presses in one frame can both miss, and
        // recording the best twice would make the second look like no new best.
        if (!over) {
            showConfig = ShowConfig();
            showStory.reset();
            showDue = endShowFor(showConfig, showStory);
        }
        over = true;
        overT = 0;
        overScore = score;
        overLine = scoreText.empty() ? std::to_string(score) : scoreText;
        overTitle[0] = line1;
        overTitle[1] = line2;
    }
    // Records best under key in the best scores, and whether the game gets
    // the end show: good is whether it reached the Good Game mark (goodGame).
    bool earnsShow(const char *key, int best, bool good, bool *newBest, int *previousBest) {
        *newBest = arcadeRecordBest(model->getName(), key, best, previousBest);
        return arcadeWantShow(endShow, good, *newBest);
    }
    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    // sounds/<game>/<n>.wav, the folder being the effect's name in lower case.
    std::string folder() const {
        std::string f = name();
        for (auto &ch : f) ch = (char)std::tolower((unsigned char)ch);
        return f;
    }
    void sound(const char *n, float gain = 1.0f) { sfx(folder(), n, gain); }

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 0, H = 0;
    float clock = 0;
    bool over = false;
    float overT = 0;
    int endShow = 1, goodGame = 0;   // the End Show and Good Game settings

private:
    // Handing the model back, after GAME OVER or the end show.
    int32_t finish() {
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();
        finished = true;
        return EFFECT_AFTER_NEXT_OUTPUT;
    }

    ArcadeShowPlayer show;
    ShowConfig showConfig;
    std::unique_ptr<ShowStory> showStory;
    bool showDue = false;   // the game earned the show; it starts after the miss
    std::mutex pressLock;
    std::vector<std::string> presses;
    int overScore = 0;
    std::string overTitle[2], overLine;
    bool finished = false;
};

// Starting any of them: the first press only starts it (so it can't, say, drop
// the first Stacker row before you've seen it move); after that presses play.
// showMode and good are the End Show and Good Game settings.
template <typename EFFECT>
static void startOrForward(const std::string &modelName, const std::string &overlay, const std::string &button,
                           int showMode, int good) {
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
    m->setRunningEffect(new EFFECT(m, showMode, good), 25);
}

// ======================================================================== Stacker

class StackerEffect : public SimpleGameEffect {
public:
    StackerEffect(PixelOverlayModel *m, int showMode, int good) : SimpleGameEffect(m, showMode, good) {
        layerH = H >= 20 ? 2 : 1;
        reset();
    }
    const std::string &name() const override {
        static std::string NAME = "Stacker";
        return NAME;
    }

private:
    enum class Phase { Intro, Play, Clear };
    struct Chunk {
        float x0, x1, y, vy;
        RGB col;
    };

    void reset() override {
        level = 1;
        score = 0;
        startLevel();
    }

    // The cheat code: the first level whose tower is the tallest (60 rows);
    // the speed has topped out well before it.
    void lastLevel() override {
        const int base = H * 2 / (layerH * 5) + 6;
        level = 1 + std::max(0, (60 - base + 3) / 4);
        startLevel();
    }
    void nextLevel() override {
        ++level;
        startLevel();
    }

    // Each level is a new tower from a full-width base: taller, and faster
    // from the first row.
    void startLevel() {
        stack.clear();
        chunks.clear();
        width = std::max(4, W / 4);
        camY = 0;
        goal = std::min(60, H * 2 / (layerH * 5) + 6 + 4 * (level - 1));   // 11, 15, 19, ... rows on 25 high
        newRow();
        phase = Phase::Intro;
        phaseT = 0;
        perfectT = 0;
        sfxStop("stacker");
        sound("LevelStart");
    }

    void newRow() {
        dir = rnd() < 0.5f ? 1 : -1;
        x = dir > 0 ? 0.0f : (float)(W - width);
    }
    // LEDs per second: a bit faster each row, and each level starts faster
    // and tops out higher.
    float speed() const {
        const float start = 7.0f + 2.5f * (level - 1);
        const float cap = std::min(45.0f, 26.0f + 4.0f * (level - 1));
        return std::min(cap, start + 0.8f * stack.size());
    }
    // Rows in tower coordinates: the bottom row sits on the bottom of the
    // panel and higher rows go negative. camY shifts the view down onto them.
    int rowY(int layer) const { return H - (layer + 1) * layerH; }
    int screenY(float y) const { return (int)std::lround(y + camY); }
    // Keep the sliding row at least a third of the way down the panel.
    void followCamera(float dt) {
        const float want = std::max(0.0f, H * 0.35f - rowY((int)stack.size()));
        camY += (want - camY) * std::min(1.0f, dt * 4.0f);
    }
    RGB layerColour(int layer) const { return hue(layer / (float)goal * 0.85f + 0.1f * (level - 1)); }

    // Scoring: 10 a row and 10 more for a perfect drop, both times the level,
    // and on reaching the top 25 for every block still in the row, times the
    // level - so finishing wide is worth a lot.
    void onPress(const std::string &name) override {
        // The buttons drop a row; the directions don't, so the cheat code's
        // can't knock the tower down.
        if (phase != Phase::Play || (name != "A Button" && name != "B Button" && name != "Fire" && name != "Fire Button")) {
            return;
        }
        const int x0 = (int)std::lround(x), x1 = x0 + width - 1;
        int lo = x0, hi = x1;
        if (!stack.empty()) {
            lo = std::max(x0, stack.back().first);
            hi = std::min(x1, stack.back().second);
        }
        const int layer = (int)stack.size();
        const RGB col = layerColour(layer);
        if (hi < lo) {
            chunks.push_back({(float)x0, (float)x1, (float)rowY(layer), 0, col});
            sound("Miss");
            gameOver(score);
            return;
        }
        // The overhang drops off.
        if (x0 < lo) chunks.push_back({(float)x0, (float)lo - 1, (float)rowY(layer), 0, col});
        if (x1 > hi) chunks.push_back({(float)hi + 1, (float)x1, (float)rowY(layer), 0, col});
        score += 10 * level;
        // The drop climbs a scale as the tower nears the top.
        const int note = std::min(15, layer * 16 / std::max(1, goal));
        sound(("Drop" + std::to_string(note + 1)).c_str());
        if (!stack.empty() && lo == x0 && hi == x1) {
            score += 10 * level;
            perfectT = 0.6f;
            sound("Perfect");
        } else if (x0 < lo || x1 > hi) {
            sound("Chop", 0.8f);
        }
        stack.push_back({lo, hi});
        width = hi - lo + 1;
        if ((int)stack.size() >= goal) {
            score += 25 * width * level;
            phase = Phase::Clear;
            phaseT = 0;
            sfxStop("stacker");
            sound("LevelClear");
            return;
        }
        newRow();
        waitT = 0.25f;   // a moment between rows, so one press can't drop two
    }

    void stepChunks(float dt) {
        for (auto &c : chunks) {
            c.vy += 40.0f * dt;
            c.y += c.vy * dt;
        }
        chunks.erase(std::remove_if(chunks.begin(), chunks.end(), [&](const Chunk &c) { return screenY(c.y) > H; }), chunks.end());
    }

    void stepGame(float dt) override {
        phaseT += dt;
        perfectT = std::max(0.0f, perfectT - dt);
        stepChunks(dt);
        if (phase == Phase::Intro) {
            if (phaseT > 1.5f) {
                phase = Phase::Play;
                phaseT = 0;
                waitT = 0.2f;
            }
            return;
        }
        if (phase == Phase::Clear) {
            // Show off the tower, then the next one.
            if (phaseT > 2.5f) {
                ++level;
                startLevel();
            }
            return;
        }
        waitT = std::max(0.0f, waitT - dt);
        x += dir * speed() * dt;
        if (x <= 0) {
            x = 0;
            dir = 1;
        } else if (x >= W - width) {
            x = (float)(W - width);
            dir = -1;
        }
        followCamera(dt);
    }
    void stepOver(float dt) override { stepChunks(dt); }

    // A good game reaches level goodGame; the best kept is the score.
    bool endShowFor(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) override {
        bool newBest = false;
        int best = 0;
        if (!earnsShow("Stacker", score, level >= goodGame, &newBest, &best)) {
            return false;
        }
        cfg = StackerShow::config(score, level, newBest);
        story = std::make_unique<StackerShow::Story>();
        return true;
    }

    void draw() override {
        const bool party = phase == Phase::Clear;
        for (int k = 0; k < (int)stack.size(); ++k) {
            const RGB col = party ? hue(clock * 0.6f + k * 0.06f) : layerColour(k);
            const float bright = (perfectT > 0 && k == (int)stack.size() - 1) ? 1.0f : 0.7f;
            for (int xx = stack[k].first; xx <= stack[k].second; ++xx) {
                for (int dy = 0; dy < layerH; ++dy) {
                    canvas.add(xx, screenY(rowY(k) + dy), perfectT > 0 && k == (int)stack.size() - 1 ? ArcadeCanvas::WHITE : col, bright);
                }
            }
        }
        // The goal: a dim line where the top row goes, once it's in view.
        // Level with the counters in the top corners it stops short of them,
        // so it never runs behind the numbers.
        const int gy = screenY(rowY(goal - 1)) - 1;
        if (gy >= 0 && !party) {
            int x0 = 0, x1 = W - 1;
            if (gy < 5 && phase != Phase::Intro) {
                x0 = (int)std::to_string(goal - (int)stack.size()).size() * 4 + 2;
                x1 = W - (int)std::to_string(score).size() * 4 - 2;
            }
            for (int xx = x0; xx <= x1; xx += 2) {
                canvas.add(xx, gy, {1.0f, 0.85f, 0.2f}, 0.3f);
            }
        }
        if (!over && phase == Phase::Play) {
            const int layer = (int)stack.size(), x0 = (int)std::lround(x);
            for (int xx = x0; xx < x0 + width; ++xx) {
                for (int dy = 0; dy < layerH; ++dy) {
                    canvas.add(xx, screenY(rowY(layer) + dy), ArcadeCanvas::mix(layerColour(layer), ArcadeCanvas::WHITE, 0.3f), 1.0f);
                }
            }
        }
        for (auto &c : chunks) {
            for (int xx = (int)c.x0; xx <= (int)c.x1; ++xx) {
                for (int dy = 0; dy < layerH; ++dy) {
                    canvas.add(xx, screenY(c.y + dy), c.col, 0.5f);
                }
            }
        }
    }

    void drawText() override {
        if (phase == Phase::Intro) {
            textBox({"LEVEL", std::to_string(level)}, 255, 255, 255);
        } else if (phase == Phase::Clear && phaseT > 0.8f) {
            textBox({std::to_string(score)}, 255, 220, 0);
        } else {
            // Rows left and the score, small, in the top corners.
            PanelTransform pt(this);
            const std::string left = std::to_string(goal - (int)stack.size()), s = std::to_string(score);
            outputString(left, 0, 0, 255, 200, 60, 1);
            outputString(s, W - (int)s.size() * 4 + 1, 0, 140, 140, 170, 1);
        }
    }

    Phase phase = Phase::Intro;
    int layerH = 2, goal = 11, width = 7, dir = 1, level = 1, score = 0;
    float x = 0, waitT = 0, camY = 0, phaseT = 0, perfectT = 0;
    std::vector<std::pair<int, int>> stack;
    std::vector<Chunk> chunks;
};

const std::string &FPPStacker::getName() {
    static const std::string name = "Stacker";
    return name;
}
void FPPStacker::button(const std::string &button) {
    startOrForward<StackerEffect>(modelName, findOption("overlay", "Overwrite"), button, endShowOption(), goodGameOption(3));
}

// ======================================================================== Flap

class FlapEffect : public SimpleGameEffect {
public:
    FlapEffect(PixelOverlayModel *m, int showMode, int good) : SimpleGameEffect(m, showMode, good) { reset(); }
    const std::string &name() const override {
        static std::string NAME = "Flap";
        return NAME;
    }

private:
    struct Pipe {
        float x;
        int gapTop, gap;
        bool passed;
        bool passedBy[2];
    };
    // Two birds, one per control: Up flies the yellow one, A or B (or Fire)
    // the blue one. Whoever flaps first starts the game; the other bird joins
    // whenever its control is first pressed, in a safe spot, blinking while
    // it can't be hit. Each has its own score, and the game is over when
    // every bird that joined has crashed. One bird plays exactly as Flap
    // always has.
    struct Bird {
        bool joined = false, alive = false;
        float y = 0, vy = 0, flapT = 0, safeT = 0, deadT = 0;
        int x = 0, score = 0;
        ArcadeCanvas::RGB body, wing;
    };

    void reset() override {
        for (int i = 0; i < 2; ++i) {
            birds[i] = Bird();
            birds[i].x = i == 0 ? W / 4 : std::max(2, W / 4 - 4);
            birds[i].y = H / 2.0f;
        }
        birds[0].body = {1.0f, 0.85f, 0.1f};
        birds[0].wing = {1.0f, 0.45f, 0.05f};
        birds[1].body = {0.3f, 0.75f, 1.0f};
        birds[1].wing = {0.1f, 0.35f, 1.0f};
        passed = 0;
        lastOut = 0;
        pipes.clear();
        nextPipe = (float)W;
    }

    bool flying() const { return birds[0].joined || birds[1].joined; }

    void onPress(const std::string &name) override {
        int who = -1;
        if (name == "Up") {
            who = 0;
        } else if (name == "A Button" || name == "B Button" || name == "Fire" || name == "Fire Button") {
            who = 1;
        }
        if (who < 0) {
            return;
        }
        Bird &b = birds[who];
        if (!b.joined) {
            // The first bird starts the game where it bobs; a later one
            // arrives in the middle, out of harm's way for a moment.
            const bool first = !flying();
            b.joined = b.alive = true;
            b.safeT = first ? 0.0f : 1.5f;
            if (!first) {
                b.y = H / 2.0f;
            }
            sound("Start");
        } else if (!b.alive) {
            return;   // crashed: out until the next game
        } else if (clock - flapSoundT > 0.08f) {   // mashing shouldn't stack up flaps
            flapSoundT = clock;
            sound("Flap", 0.7f);
        }
        b.vy = -11.0f;
        b.flapT = 0.15f;
    }

    // Harder as the pipes go by, whoever passes them.
    float speed() const { return std::min(16.0f, 9.0f + 0.2f * passed); }

    void crash(Bird &b) {
        b.alive = false;
        b.deadT = 0;
        lastOut = &b == &birds[0] ? 0 : 1;
        sound("Crash");
        if (!birds[0].alive && !birds[1].alive) {
            const int top = std::max(birds[0].score, birds[1].score);
            if (birds[0].joined && birds[1].joined) {
                gameOver(top, "GAME", "OVER", std::to_string(birds[0].score) + ":" + std::to_string(birds[1].score));
            } else {
                gameOver(top);
            }
        }
    }

    void stepGame(float dt) override {
        for (auto &b : birds) {
            b.flapT = std::max(0.0f, b.flapT - dt);
            b.safeT = std::max(0.0f, b.safeT - dt);
            b.deadT += dt;
        }
        if (!flying()) {
            for (auto &b : birds) {
                b.y = H / 2.0f + std::sin(clock * 4.0f);   // bob until the first flap
            }
            return;
        }
        for (auto &b : birds) {
            if (b.joined && b.alive) {
                b.vy = std::min(18.0f, b.vy + 42.0f * dt);
                b.y = std::max(0.0f, b.y + b.vy * dt);
            } else if (!b.joined) {
                b.y = H / 2.0f + std::sin(clock * 4.0f);   // waiting to join
            }
        }
        nextPipe -= speed() * dt;
        if (nextPipe <= W) {
            const int gap = std::max(5, std::min(H / 3, 8) - passed / 6);
            const int gapTop = 1 + (int)(rnd() * (H - gap - 2));
            pipes.push_back({nextPipe, gapTop, gap, false, {false, false}});
            nextPipe += std::max(14, W / 3);
        }
        for (auto &p : pipes) {
            p.x -= speed() * dt;
            for (int i = 0; i < 2; ++i) {
                Bird &b = birds[i];
                if (b.alive && !p.passedBy[i] && p.x + 1 < b.x) {
                    p.passedBy[i] = true;
                    ++b.score;
                    sound("Score");
                }
            }
            if (!p.passed && p.x + 1 < birds[0].x) {
                p.passed = true;
                ++passed;
            }
        }
        pipes.erase(std::remove_if(pipes.begin(), pipes.end(), [](const Pipe &p) { return p.x < -3; }), pipes.end());
        for (auto &b : birds) {
            if (!b.alive || over) {
                continue;
            }
            const int by = (int)b.y;
            if (by >= H) {
                crash(b);
                continue;
            }
            if (b.safeT > 0) {
                continue;
            }
            for (auto &p : pipes) {
                const int px = (int)std::floor(p.x);
                if (b.x >= px && b.x <= px + 1 && (by < p.gapTop || by >= p.gapTop + p.gap)) {
                    crash(b);
                    break;
                }
            }
        }
    }

    // A good game passes goodGame pipes, and the best kept is the pipes - the
    // better bird's, with two.
    bool endShowFor(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) override {
        const int score = std::max(birds[0].score, birds[1].score);
        bool newBest = false;
        int best = 0;
        if (!earnsShow("Flap", score, score >= goodGame, &newBest, &best)) {
            return false;
        }
        if (birds[0].joined && birds[1].joined) {
            cfg = FlapShow::config2(birds[0].score, birds[1].score, newBest, best);
            story = std::make_unique<FlapShow::Story>(true);
        } else {
            cfg = FlapShow::config(score, newBest, best);
            story = std::make_unique<FlapShow::Story>();
        }
        return true;
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
        for (int i = 0; i < 2; ++i) {
            const Bird &b = birds[i];
            // Before the game: the yellow bird alone, as it always was. During
            // it: the birds in play, a joining one blinking while it's safe,
            // and a crashed one blinking out.
            bool show = flying() ? b.joined : i == 0;
            if (b.joined && !b.alive) {
                show = b.deadT < 0.9f && std::fmod(b.deadT, 0.3f) < 0.15f;
            } else if (b.safeT > 0) {
                show = std::fmod(b.safeT, 0.24f) < 0.14f;
            }
            // GAME OVER blinks the bird that just crashed, where it went down.
            // One that crashed earlier stays gone: the pipes have moved on
            // since, so where it hit isn't anywhere any more.
            if (over) {
                show = i == lastOut && std::fmod(overT, 0.3f) < 0.15f;
            }
            if (!show) {
                continue;
            }
            const int by = std::min(H - 1, (int)b.y);
            canvas.add(b.x, by, b.body, 1.0f);
            canvas.add(b.x - 1, by + (b.flapT > 0 ? -1 : 0), b.wing, 0.8f);   // the wing
            canvas.add(b.x + 1, by, {1.0f, 0.3f, 0.1f}, 0.5f);                 // the beak
        }
    }

    void drawText() override {
        PanelTransform pt(this);
        if (!flying()) {
            outputString("FLAP", centerTextX("FLAP", 1), 2, 255, 220, 60, 1);
            return;
        }
        // Each bird's score in its colour: yellow top right, blue top left.
        if (birds[0].joined) {
            const std::string s = std::to_string(birds[0].score);
            outputString(s, W - (int)s.size() * 4, 1, 200, 170, 40, 1);
        }
        if (birds[1].joined) {
            outputString(std::to_string(birds[1].score), 1, 1, 70, 150, 220, 1);
        }
    }

    Bird birds[2];
    int lastOut = 0;  // the bird that crashed last
    int passed = 0;   // pipes gone by, for how fast and tight they get
    float nextPipe = 0, flapSoundT = -1;
    std::vector<Pipe> pipes;
};

const std::string &FPPFlap::getName() {
    static const std::string name = "Flap";
    return name;
}
void FPPFlap::button(const std::string &button) {
    startOrForward<FlapEffect>(modelName, findOption("overlay", "Overwrite"), button, endShowOption(), goodGameOption(15));
}

// ======================================================================== Light Memory

class LightMemoryEffect : public SimpleGameEffect {
public:
    LightMemoryEffect(PixelOverlayModel *m, int showMode, int good) : SimpleGameEffect(m, showMode, good) {
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
    // Simon's four notes, one per triangle: up, right, down, left.
    void tone(int r) {
        static const char *TONES[4] = {"Up", "Right", "Down", "Left"};
        sound(TONES[r]);
    }

    void reset() override {
        seq.clear();
        phase = Phase::Wait;
        phaseT = 0;
        lit = -1;
        litT = 0;
        cheer = false;
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
            sound("Wrong");
            gameOver((int)seq.size() - 1);
            return;
        }
        tone(r);
        if (++pos >= (int)seq.size()) {
            addStep();
            cheer = true;
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
            // After the last note of the round has rung, not over it.
            if (cheer && phaseT > 0.3f) {
                cheer = false;
                sound("RoundClear");
            }
            if (phaseT > 0.9f) {
                phase = Phase::Show;
                phaseT = 0;
                pos = 0;
                shown = -1;
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
                if (k != shown) {
                    shown = k;
                    tone(seq[k]);
                }
            }
            break;
        }
        case Phase::Input:
            if (phaseT > 6.0f) {
                wrong = seq[pos];
                sound("Wrong");
                gameOver((int)seq.size() - 1);   // too slow
            }
            break;
        }
    }

    // A good game repeats goodGame rounds, and the best kept is the rounds.
    bool endShowFor(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) override {
        const int rounds = (int)seq.size() - 1;
        bool newBest = false;
        int best = 0;
        if (!earnsShow("Light Memory", rounds, rounds >= goodGame, &newBest, &best)) {
            return false;
        }
        cfg = LightMemoryShow::config(rounds, newBest, best);
        story = std::make_unique<LightMemoryShow::Story>();
        return true;
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
    int lit = -1, pos = 0, wrong = -1, shown = -1;
    bool cheer = false;   // a round was just completed
};

const std::string &FPPLightMemory::getName() {
    static const std::string name = "Light Memory";
    return name;
}
void FPPLightMemory::button(const std::string &button) {
    startOrForward<LightMemoryEffect>(modelName, findOption("overlay", "Overwrite"), button, endShowOption(),
                                      goodGameOption(10));
}
