#include <fpp-pch.h>

#include "FPPBreakoutVersus.h"
#include "ArcadeCanvas.h"
#include "BreakoutVersusShow.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;

// A wall of three-LED bricks across the middle, a paddle on the bottom and top
// rows, and a ball for each player in their colour. A ball that gets past a
// paddle scores for the other player, whoever's ball it was, so each defends
// their edge from both. Bricks are in the way: a ball (or a laser) breaks the
// brick it hits and bounces, and now and then a brick drops a capsule toward
// whoever broke it - a wider paddle, or a laser. When the wall is gone a new
// one goes up.

namespace {
const RGB PLAYER[2] = {{1.0f, 0.85f, 0.1f}, {0.3f, 0.6f, 1.0f}};   // bottom yellow, top blue
const RGB LASER_RED = {1.0f, 0.1f, 0.05f};
const RGB WIDE_BLUE = {0.0f, 0.3f, 1.0f};   // the capsule colours as in Breakout
}

class BreakoutVersusEffect : public FPPArcadeGameEffect {
public:
    BreakoutVersusEffect(PixelOverlayModel *m, int goals, float ballSpeed, float paddleSpeed, int showMode, int goodMargin)
        : FPPArcadeGameEffect(m), toWin(std::clamp(goals, 1, 15)), speedSetting(std::clamp(ballSpeed, 0.3f, 3.0f)),
          paddleSpeed(std::clamp(paddleSpeed, 0.3f, 3.0f)),
          endShow(showMode), goodGame(goodMargin), rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(16, W);
        H = std::max(12, H);
        canvas.resize(W, H);
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "BreakoutVersus";
        return NAME;
    }

    void button(const std::string &b) override {
        if (show.on()) {
            if (b.find("Pressed") != std::string::npos) {
                show.skip();
            }
            return;
        }
        // Moving is holding: the bottom paddle on Left/Right, the top one on
        // B (left) and A (right).
        const bool pressed = b.size() > 8 && b.compare(b.size() - 8, 8, " Pressed") == 0;
        const bool released = b.size() > 9 && b.compare(b.size() - 9, 9, " Released") == 0;
        if (pressed || released) {
            const std::string n = b.substr(0, b.find(" - "));
            uint32_t bits = 0;
            if (n == "Left" || n == "Up/Left" || n == "Down/Left") bits = P1_LEFT;
            else if (n == "Right" || n == "Up/Right" || n == "Down/Right") bits = P1_RIGHT;
            else if (n == "B Button") bits = P2_LEFT;
            else if (n == "A Button") bits = P2_RIGHT;
            if (pressed) {
                held.fetch_or(bits);
                tapped.fetch_or(bits);   // even if it's let go before a frame sees it
            } else {
                held.fetch_and(~bits);
            }
        }
        const bool wasPaused = paused;
        FPPArcadeGameEffect::button(b);
        if (wasPaused || paused) {
            return;
        }
        if (b == "Up - Pressed" || b == "Fire - Pressed") {
            upNow.store(true);   // the bottom player's launch, or laser
        }
    }

    void restart() override {
        newGame();
        resetFrameTimer();
    }

    int32_t update() override {
        if (finished) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return EFFECT_DONE;
        }
        if (show.on()) {
            if (show.step(model, consumeElapsedMs(50.0))) {
                return 50;
            }
            // Over: the game ends as it would have after its result.
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            finished = true;
            return EFFECT_AFTER_NEXT_OUTPUT;
        }
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return FRAME_MS;
        }
        const float dt = (float)(consumeElapsedMs(FRAME_MS, 100.0) / 1000.0);
        clock += dt;
        stateT += dt;
        takeCheat();   // no levels here: a cheat code does nothing
        step(dt);
        if (show.on()) {
            resetFrameTimer();   // a good game: the show starts next frame
            return 50;
        }
        if (state == State::Over && stateT > 5.0f) {
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            finished = true;
            return EFFECT_AFTER_NEXT_OUTPUT;
        }
        beginFrame();   // drawn in passes, the text last: see beginFrame()
        render();
        endFrame();
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 25;
    static constexpr uint32_t P1_LEFT = 1, P1_RIGHT = 2, P2_LEFT = 4, P2_RIGHT = 8;
    enum class State { Intro, Play, Over };

    struct Paddle {
        float x = 0;
        float wideT = 0, laserT = 0, fireT = 0;   // power-ups' time left, and the laser's cooldown
        int moving = 0;                           // the way it went last frame
        float from = 0;                           // and the LED it set off from
    };
    struct Ball {
        int owner;        // 0 bottom, 1 top
        float x, y, vx, vy;
        bool stuck;       // waiting on its owner's paddle
        float stuckT;
    };
    struct Brick {
        int x, y;
        bool alive;
        RGB col;
    };
    struct Capsule {
        float x, y, vy;
        int kind;   // 0 wider paddle, 1 laser
        int to;     // the player it's for
    };
    struct Beam {
        float x, y, vy;
        int owner;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    void sound(const char *n, float gain = 1.0f) { sfx("breakoutversus", n, gain); }

    int paddleRow(int p) const { return p == 0 ? H - 1 : 0; }
    int paddleWidth(int p) const {
        const int base = std::max(4, W / 8);
        return paddles[p].wideT > 0 ? (int)std::lround(base * 1.6f) : base;
    }
    // The ball gets a little quicker with every goal, so a long match speeds up.
    float ballSpeed() const { return (9.0f + 0.25f * H) * speedSetting * (1.0f + 0.04f * (score[0] + score[1])); }

    void newGame() {
        score[0] = score[1] = 0;
        for (int p = 0; p < 2; ++p) {
            paddles[p] = Paddle();
            paddles[p].x = W / 2.0f - paddleWidth(p) / 2.0f;
        }
        balls.clear();
        balls.push_back({0, 0, 0, 0, 0, true, 0});
        balls.push_back({1, 0, 0, 0, 0, true, 0});
        capsules.clear();
        beams.clear();
        tapped = 0;
        buildWall();
        state = State::Intro;
        stateT = 0;
        flashT = 0;
        sfxStop("breakoutversus");
        sound("Start");
    }

    // Rows of three-LED bricks across the middle, every other row offset, in
    // colours from warm at one side to cool at the other.
    void buildWall() {
        bricks.clear();
        wallRows = std::max(2, H / 6);
        wallTop = H / 2 - wallRows / 2;
        for (int r = 0; r < wallRows; ++r) {
            const RGB col = hue(0.02f + 0.55f * r / std::max(1, wallRows - 1));
            for (int x = 1 + (r & 1) * 2; x + 2 < W - 1; x += 4) {
                bricks.push_back({x, wallTop + r, true, col});
            }
        }
        newWallT = -1;
    }
    static RGB hue(float h) { return ArcadeShow::hue(h); }

    // Off the paddle at an angle, never straight: 17 to 27 degrees, either way.
    void launch(Ball &b) {
        const float v = ballSpeed();
        b.vx = v * (0.3f + 0.15f * rnd()) * (rnd() < 0.5f ? -1.0f : 1.0f);
        b.vy = (b.owner == 0 ? -1.0f : 1.0f) * std::sqrt(std::max(1.0f, v * v - b.vx * b.vx));
        b.stuck = false;
        sound("Launch", 0.8f);
    }

    Brick *brickAt(float x, float y) {
        const int cx = (int)std::floor(x), cy = (int)std::floor(y);
        for (auto &b : bricks) {
            if (b.alive && b.y == cy && cx >= b.x && cx < b.x + 3) {
                return &b;
            }
        }
        return nullptr;
    }
    // A brick broken by player p: now and then a capsule falls toward them.
    // As in Arkanoid: one at a time for each player, and never the power
    // their paddle already has.
    void breakBrick(Brick &b, int p) {
        b.alive = false;
        sound("Brick", 0.8f);
        const bool falling = std::any_of(capsules.begin(), capsules.end(), [p](const Capsule &c) { return c.to == p; });
        if (!falling && rnd() < 0.12f) {
            const int kind = paddles[p].wideT > 0 ? 1 : paddles[p].laserT > 0 ? 0 : (rnd() < 0.5f ? 0 : 1);
            capsules.push_back({b.x + 1.0f, (float)b.y, p == 0 ? 9.0f : -9.0f, kind, p});
        }
    }

    void goal(Ball &b, int scorer) {
        ++score[scorer];
        flashT = 0.6f;
        flashEdge = 1 - scorer;   // the edge the ball got past
        sound("Goal");
        b.stuck = true;
        b.stuckT = 0;
        if (score[scorer] >= toWin) {
            gameOver();
        }
    }

    void gameOver() {
        state = State::Over;
        stateT = 0;
        sfxStop("breakoutversus");
        // A clear win gets the end show; there's no best score here, as in Pong.
        const bool good = std::abs(score[0] - score[1]) >= goodGame;
        if (arcadeWantShow(endShow, good, false)) {
            sound("EndShow");
            show.start(model, BreakoutVersusShow::config(score[0], score[1]),
                       std::make_unique<BreakoutVersusShow::Story>(score[1] > score[0]));
            return;
        }
        sound("GameOver");
    }

    void step(float dt) {
        flashT = std::max(0.0f, flashT - dt);
        if (state == State::Intro) {
            if (stateT > 1.6f) {
                state = State::Play;
                stateT = 0;
            }
            positionStuck();
            return;
        }
        if (state == State::Over) {
            return;
        }

        // Paddles.
        // Paddle speed is in Breakout's units: 1 is 20 LEDs a second. When a
        // paddle stops it stays on the LED it's drawn at, but at least one
        // from where the press began, so a quick tap moves it one (a frame's
        // move is under an LED); a press let go before any frame saw it moves
        // it one too.
        const uint32_t h = held.load(), tap = tapped.exchange(0);
        const float move = 20.0f * paddleSpeed * dt;
        auto way = [](uint32_t bits, uint32_t left, uint32_t right) { return ((bits & right) ? 1 : 0) - ((bits & left) ? 1 : 0); };
        const int dir[2] = {way(h, P1_LEFT, P1_RIGHT), way(h, P2_LEFT, P2_RIGHT)};
        const int tapDir[2] = {way(tap, P1_LEFT, P1_RIGHT), way(tap, P2_LEFT, P2_RIGHT)};
        for (int p = 0; p < 2; ++p) {
            Paddle &pd = paddles[p];
            if (dir[p] != 0) {
                if (pd.moving != dir[p]) {
                    pd.from = std::round(pd.x);
                }
                pd.x += dir[p] * move;
                pd.moving = dir[p];
            } else if (pd.moving != 0) {
                const float shown = std::round(pd.x);   // drawn at lround
                pd.x = pd.moving > 0 ? std::max(pd.from + 1.0f, shown) : std::min(pd.from - 1.0f, shown);
                pd.moving = 0;
            } else if (tapDir[p] != 0) {
                pd.x = std::round(pd.x) + tapDir[p];
            }
            pd.x = std::clamp(pd.x, 0.0f, (float)(W - paddleWidth(p)));
            pd.wideT = std::max(0.0f, pd.wideT - dt);
            pd.laserT = std::max(0.0f, pd.laserT - dt);
            pd.fireT = std::max(0.0f, pd.fireT - dt);
        }
        positionStuck();

        // Launching and the lasers: Up for the bottom player; the top one's
        // ball goes by itself after a moment and its laser fires by itself.
        const bool up = upNow.exchange(false);
        for (auto &b : balls) {
            if (!b.stuck) {
                continue;
            }
            b.stuckT += dt;
            if ((b.owner == 0 && (up || b.stuckT > 2.5f)) || (b.owner == 1 && b.stuckT > 1.2f)) {
                launch(b);
            }
        }
        if (up && paddles[0].laserT > 0 && !balls[0].stuck && paddles[0].fireT <= 0) {
            fire(0);
        }
        if (paddles[1].laserT > 0 && paddles[1].fireT <= 0) {
            fire(1);
        }

        // Balls, in steps of at most half an LED so none can skip a brick.
        for (auto &b : balls) {
            if (b.stuck) {
                continue;
            }
            const int steps = std::max(1, (int)std::ceil(std::hypot(b.vx, b.vy) * dt / 0.5f));
            for (int i = 0; i < steps && !b.stuck && state == State::Play; ++i) {
                moveBall(b, dt / steps);
            }
        }

        // Laser beams.
        for (size_t i = 0; i < beams.size();) {
            Beam &bm = beams[i];
            bm.y += bm.vy * dt;
            Brick *br = brickAt(bm.x, bm.y);
            if (br) {
                breakBrick(*br, bm.owner);
            }
            if (br || bm.y < 0 || bm.y >= H) {
                beams.erase(beams.begin() + i);
            } else {
                ++i;
            }
        }

        // Capsules, caught by the paddle they fall toward.
        for (size_t i = 0; i < capsules.size();) {
            Capsule &c = capsules[i];
            c.y += c.vy * dt;
            const int row = paddleRow(c.to);
            const bool reached = c.to == 0 ? c.y >= row : c.y < row + 1;
            if (reached) {
                const float px = paddles[c.to].x;
                if (c.x + 1 >= px && c.x <= px + paddleWidth(c.to)) {
                    // One power at a time: wide ends the laser, the laser ends wide.
                    Paddle &pd = paddles[c.to];
                    if (c.kind == 0) {
                        pd.wideT = 10.0f;
                        pd.laserT = 0.0f;
                    } else {
                        pd.laserT = 8.0f;
                        pd.wideT = 0.0f;
                        pd.x = std::min(pd.x, (float)(W - paddleWidth(c.to)));
                    }
                    sound("PowerUp");
                }
                capsules.erase(capsules.begin() + i);
            } else {
                ++i;
            }
        }

        // A new wall once this one's gone.
        if (std::none_of(bricks.begin(), bricks.end(), [](const Brick &b) { return b.alive; })) {
            if (newWallT < 0) {
                newWallT = 1.2f;
            } else if ((newWallT -= dt) <= 0) {
                buildWall();
                sound("NewWall");
            }
        }
    }

    // Stuck balls ride on their owner's paddle.
    void positionStuck() {
        for (auto &b : balls) {
            if (b.stuck) {
                b.x = paddles[b.owner].x + paddleWidth(b.owner) / 2.0f;
                b.y = b.owner == 0 ? H - 1.5f : 1.5f;
            }
        }
    }

    void fire(int p) {
        const float x0 = std::floor(paddles[p].x), x1 = x0 + paddleWidth(p) - 1;
        const float y = p == 0 ? H - 2.0f : 1.0f, vy = p == 0 ? -30.0f : 30.0f;
        beams.push_back({x0, y, vy, p});
        beams.push_back({x1, y, vy, p});
        paddles[p].fireT = p == 0 ? 0.25f : 0.45f;
        sound("Laser", 0.7f);
    }

    void moveBall(Ball &b, float dt) {
        float nx = b.x + b.vx * dt, ny = b.y + b.vy * dt;
        if (nx < 0) {
            nx = -nx;
            b.vx = std::fabs(b.vx);
            sound("Wall", 0.5f);
        } else if (nx >= W) {
            nx = 2.0f * W - nx - 0.01f;
            b.vx = -std::fabs(b.vx);
            sound("Wall", 0.5f);
        }
        if (Brick *br = brickAt(nx, ny)) {
            // Into it from above or below bounces up or down; from the side, sideways.
            if ((int)std::floor(b.y) != br->y) {
                b.vy = -b.vy;
            } else {
                b.vx = -b.vx;
            }
            breakBrick(*br, b.owner);
            return;
        }
        // The paddles: where it lands on one sets the angle it leaves at.
        for (int p = 0; p < 2; ++p) {
            const int row = paddleRow(p);
            const bool coming = p == 0 ? (b.vy > 0 && ny >= row && b.y < row) : (b.vy < 0 && ny < row + 1 && b.y >= row + 1);
            const float px = paddles[p].x, w = (float)paddleWidth(p);
            if (coming && nx >= px - 0.5f && nx <= px + w + 0.5f) {
                const float v = ballSpeed();
                const float off = std::clamp((nx - (px + w / 2.0f)) / (w / 2.0f), -1.0f, 1.0f);
                b.vx = v * 0.75f * off;
                b.vy = (p == 0 ? -1.0f : 1.0f) * std::sqrt(std::max(1.0f, v * v - b.vx * b.vx));
                b.x = nx;
                b.y = p == 0 ? row - 0.01f : row + 1.0f;
                sound("Paddle", 0.8f);
                return;
            }
        }
        b.x = nx;
        b.y = ny;
        if (b.y >= H) {
            goal(b, 1);   // past the bottom paddle: a goal for the top
        } else if (b.y < 0) {
            goal(b, 0);
        }
    }

    void render() {
        canvas.clear();
        for (const auto &b : bricks) {
            if (b.alive) {
                for (int i = 0; i < 3; ++i) {
                    canvas.add(b.x + i, b.y, b.col, 0.6f);
                }
            }
        }
        // A goal lights the edge the ball got past in the scorer's colour.
        if (flashT > 0) {
            for (int x = 0; x < W; ++x) {
                canvas.add(x, paddleRow(flashEdge), PLAYER[1 - flashEdge], 0.5f * flashT / 0.6f);
            }
        }
        for (int p = 0; p < 2; ++p) {
            const int x0 = (int)std::lround(paddles[p].x), w = paddleWidth(p);
            // Red cannons at the ends while the laser is on, blinking as it runs out.
            const bool cannons = paddles[p].laserT > 0 && !(paddles[p].laserT < 2.0f && std::fmod(paddles[p].laserT, 0.3f) < 0.15f);
            for (int i = 0; i < w; ++i) {
                const bool end = i == 0 || i == w - 1;
                canvas.set(x0 + i, paddleRow(p), cannons && end ? LASER_RED : PLAYER[p], 0.9f);
            }
        }
        for (const auto &c : capsules) {
            const RGB col = c.kind == 0 ? WIDE_BLUE : LASER_RED;
            const float k = std::fmod(clock, 0.3f) < 0.2f ? 1.0f : 0.5f;
            canvas.set((int)c.x, (int)c.y, col, k);
            canvas.set((int)c.x + 1, (int)c.y, col, k);
        }
        for (const auto &bm : beams) {
            canvas.set((int)bm.x, (int)bm.y, LASER_RED, 1.0f);
            canvas.set((int)bm.x, (int)bm.y + (bm.vy < 0 ? 1 : -1), LASER_RED, 0.5f);
        }
        for (const auto &b : balls) {
            canvas.set((int)std::floor(b.x), (int)std::floor(b.y), ArcadeCanvas::mix(PLAYER[b.owner], ArcadeCanvas::WHITE, 0.4f), 1.0f);
        }
        canvas.writeTo(model);

        if (state == State::Intro) {
            textBox({"FIRST", "TO " + std::to_string(toWin)}, 255, 255, 255);
        } else if (state == State::Over && stateT > 0.6f) {
            const int w = score[0] > score[1] ? 0 : 1;
            textBox({w == 0 ? "YELLOW" : "BLUE", "WINS", std::to_string(score[0]) + ":" + std::to_string(score[1])},
                    255, 255, 255);
        } else {
            // Each side's goals, dim, on its own side of the wall at the left.
            PanelTransform pt(this);
            const int aboveY = wallTop - 6, belowY = wallTop + wallRows + 1;
            if (aboveY >= 2) {
                outputString(std::to_string(score[1]), 1, aboveY, 50, 90, 160, 1);
            }
            if (belowY + 5 <= H - 2) {
                outputString(std::to_string(score[0]), 1, belowY, 160, 135, 20, 1);
            }
        }
    }

    int W = 60, H = 25;
    const int toWin;
    const float speedSetting;
    const float paddleSpeed;
    int endShow = 1, goodGame = 3;
    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    ArcadeShowPlayer show;

    State state = State::Intro;
    float clock = 0, stateT = 0, flashT = 0, newWallT = -1;
    int flashEdge = 0;
    int score[2] = {0, 0};
    int wallTop = 10, wallRows = 4;
    Paddle paddles[2];
    std::vector<Ball> balls;
    std::vector<Brick> bricks;
    std::vector<Capsule> capsules;
    std::vector<Beam> beams;
    std::atomic<uint32_t> held{0};
    std::atomic<uint32_t> tapped{0};   // presses, until a frame has moved the paddle for them
    std::atomic<bool> upNow{false};
    bool finished = false;
};

const std::string &FPPBreakoutVersus::getName() {
    static const std::string name = "Breakout Versus";
    return name;
}

void FPPBreakoutVersus::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    BreakoutVersusEffect *effect = dynamic_cast<BreakoutVersusEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        float speed = 1.0f;
        try {
            speed = std::stof(findOption("Ball Speed", "1"));
        } catch (...) {
        }
        float paddle = 1.5f;
        try {
            paddle = std::stof(findOption("Paddle Speed", "1.5"));
        } catch (...) {
        }
        effect = new BreakoutVersusEffect(m, findIntOption("Goals", 5), speed, paddle, endShowOption(), goodGameOption(3));
        m->setRunningEffect(effect, 25);
    }
    // The press that starts it only starts it.
    if (!starting) {
        effect->button(button);
    }
}
