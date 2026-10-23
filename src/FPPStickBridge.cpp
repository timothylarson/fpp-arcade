#include <fpp-pch.h>

#include "FPPStickBridge.h"
#include "ArcadeCanvas.h"
#include "ArcadeShow.h"
#include "StickBridgeShow.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;

// Gift boxes stand on the bottom of a snowy night with gaps between. The elf
// stands on the right edge of one; holding the control grows a candy cane
// straight up beside it, and letting go swings it down across the gap. If its
// end lands on the next box the elf walks over and the view slides along; too
// short or too long and the elf walks to the end of it and falls. The red bow
// in the middle of each box is a perfect landing, worth a point more.
//
// The stick grows from the top of the boxes, so the longest that fits
// standing up (20 on a 25-row model) is also the furthest any box is from the
// elf; both halves of a two-player game get the same reach, so the race is fair.

namespace {

constexpr float JOIN_TIME = 2.5f;    // the other control can still join this long after the start (or until the first drop)
constexpr float DROP_TIME = 0.3f;    // the stick swinging down
constexpr float WALK_SPEED = 16.0f;  // LEDs a second
constexpr float WALK_PAUSE = 0.12f;  // a beat after the stick lands, before the elf sets off
constexpr float SLIDE_TIME = 0.35f;  // the view sliding along to the next box
constexpr float TIP_TIME = 0.15f;    // at the end of a stick that missed, before the fall
constexpr float SWING_TIME = 0.25f;  // a short stick swinging down into the gap
constexpr float GRAVITY = 70.0f;
constexpr float FULL_HOLD = 1.5f;    // a stick at full length drops by itself after this, so a stuck button can't stall the game
constexpr float IDLE_TIME = 30.0f;   // waiting this long for a stick ends that side: someone walked away
constexpr int HARDEST = 25;          // bridges until the boxes are as narrow and as far apart as they get
constexpr int GROW_NOTES = 20;       // Grow1..Grow20, one a semitone higher for each LED of stick

// Wrapping paper, never red: the bow is.
const RGB BOXES[] = {{0.1f, 0.75f, 0.2f}, {0.15f, 0.35f, 1.0f}, {0.6f, 0.2f, 0.9f}, {1.0f, 0.65f, 0.05f}, {0.05f, 0.75f, 0.7f}};
const RGB RIBBON = {1.0f, 0.8f, 0.25f}, RIBBON_ON_GOLD = {0.9f, 0.9f, 0.9f};
const RGB BOW = {1.0f, 0.03f, 0.03f};
const RGB CANE_RED = {1.0f, 0.08f, 0.06f}, CANE_WHITE = {0.75f, 0.75f, 0.75f};
const RGB HAT = {1.0f, 0.08f, 0.05f}, BOBBLE = {0.8f, 0.8f, 0.8f}, FACE = {0.85f, 0.5f, 0.32f};
// The elves' tunics: green on the left (or alone), blue on the right.
const RGB TUNIC[2] = {{0.1f, 0.9f, 0.2f}, {0.2f, 0.5f, 1.0f}};
const RGB GOLD = {1.0f, 0.8f, 0.2f};

}  // namespace

class StickBridgeEffect : public FPPArcadeGameEffect {
public:
    // showMode and good: the End Show and Good Game settings. createdByPress:
    // a control press created the game, and only starts it (it isn't a
    // stick); when Start created it, the first control press also plays.
    StickBridgeEffect(PixelOverlayModel *m, int showMode, int good, bool createdByPress)
        : FPPArcadeGameEffect(m), endShow(showMode), goodGame(good), firstStarts(createdByPress),
          rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(16, W);
        H = std::max(12, H);
        canvas.resize(W, H);
        // Boxes a quarter of the height, so the stick has the rest to grow in.
        boxH = std::clamp(H / 4, 3, 8);
        topY = H - boxH;
        buildSky();
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "StickBridge";
        return NAME;
    }

    void button(const std::string &b) override {
        if (show.on()) {
            if (b.find("Pressed") != std::string::npos) {
                show.skip();
            }
            return;
        }
        const bool pressed = endsWith(b, " - Pressed");
        const bool released = endsWith(b, " - Released");
        const std::string n = b.substr(0, b.find(" - "));
        // Which control (0 the directions, 1 the buttons) and which of its
        // keys, so letting go of one key while another is still down doesn't
        // drop the stick.
        int ctl = -1;
        uint32_t bits = 0;
        if (n == "Up") { ctl = 0; bits = 1; }
        else if (n == "Down") { ctl = 0; bits = 2; }
        else if (n == "Left") { ctl = 0; bits = 4; }
        else if (n == "Right") { ctl = 0; bits = 8; }
        else if (n == "Up/Left") { ctl = 0; bits = 1 | 4; }
        else if (n == "Up/Right") { ctl = 0; bits = 1 | 8; }
        else if (n == "Down/Left") { ctl = 0; bits = 2 | 4; }
        else if (n == "Down/Right") { ctl = 0; bits = 2 | 8; }
        else if (n == "A Button") { ctl = 1; bits = 1; }
        else if (n == "B Button") { ctl = 1; bits = 2; }
        else if (n == "Fire" || n == "Fire Button") { ctl = 1; bits = 4; }
        if (ctl >= 0 && pressed) {
            held[ctl].fetch_or(bits);
        } else if (ctl >= 0 && released) {
            held[ctl].fetch_and(~bits);
        }
        const bool wasPaused = paused;
        FPPArcadeGameEffect::button(b);
        if (wasPaused || paused || ctl < 0 || !pressed) {
            return;
        }
        std::lock_guard<std::mutex> l(pressLock);
        presses.push_back(ctl);
    }

    void restart() override {
        {
            std::lock_guard<std::mutex> l(pressLock);
            presses.clear();
        }
        newGame();
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
        std::vector<int> todo;
        {
            std::lock_guard<std::mutex> l(pressLock);
            todo.swap(presses);
        }
        const int cheat = takeCheat();
        if (!over) {
            if (cheat) {
                applyCheat(cheat);
            }
            for (int c : todo) {
                onPress(c);
            }
            for (int i = 0; i < laneCount(); ++i) {
                stepLane(i, dt);
            }
        } else {
            overT += dt;
            if (overT > 1.0f && overT - dt <= 1.0f) {
                sfxStop("stickbridge");
                // A good game (or a new best) gets the end show where the
                // GAME OVER box would be, once the fall has had its second.
                if (showDue) {
                    showDue = false;
                    sfx("stickbridge", "EndShow");
                    show.start(model, std::move(showConfig), std::move(showStory));
                    resetFrameTimer();
                    return FRAME_MS;
                }
                sfx("stickbridge", "GameOver");
            }
            if (overT > 5.0f) {
                return finish();
            }
        }
        stepSky(dt);
        beginFrame();   // the picture, then the text over it: see beginFrame()
        render();
        endFrame();
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 25;

    enum class Phase { Ready, Grow, Drop, Walk, Tip, Fall, Out };
    struct Box {
        int x, w;   // world columns x..x+w-1
        int colour;
    };
    struct Bridge {
        int x, len;   // a stick lying across a gap that was crossed, from column x
    };
    struct Spark {
        float x, y, vx, vy, age, life;   // x in the lane's world
        RGB col;
    };
    // One player's side: the whole wall alone, or half of it each with two.
    // Columns on the panel are x0 to x0 + w - 1; the boxes are in a world of
    // their own that slides past, cam being the world column at x0.
    struct Lane {
        int x0 = 0, w = 0;
        int edgeX = 0;   // where the edge the elf stands on sits, from x0, when the view is still
        int reach = 0;   // the longest stick: from that edge to the right side, or to the top
        float cam = 0, camFrom = 0, camTo = 0, slideT = -1;
        std::vector<Box> boxes;
        int cur = 0;     // the box the elf is on
        std::vector<Bridge> bridges;
        std::vector<Spark> sparks;
        Phase phase = Phase::Ready;
        float phaseT = 0;
        float growT = 0, fullT = 0, angle = 0;   // angle 0 standing, 90 across the gap, 180 hanging down
        int len = 0;
        float elfX = 0, elfY = 0, elfVy = 0;     // elfX the elf's front column, elfY how far it has fallen
        int target = 0;                          // where it's walking to
        bool made = false, tooShort = false;
        int score = 0, crossed = 0, perfects = 0, level = 0, drops = 0;
        float perfectT = 0;
        bool press = false, pending = false;     // a press while ready; one while walking, kept if still held
    };

    static bool endsWith(const std::string &s, const char *tail) {
        const size_t n = strlen(tail);
        return s.size() > n && s.compare(s.size() - n, n, tail) == 0;
    }
    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    int laneCount() const { return two ? 2 : 1; }
    // The control that plays a lane: alone, whoever pressed first.
    int controlOf(int i) const { return two ? i : owner; }
    bool holding(int i) const {
        const int c = controlOf(i);
        return c >= 0 && held[c].load() != 0;
    }

    // ---------------------------------------------------------------- game flow
    void newGame() {
        over = false;
        overT = 0;
        showDue = false;
        showStory.reset();
        two = false;
        startClock = clock;
        setupLane(0, 0, W);
    }

    // Alone, a press with either control starts it; the other control, before
    // the first stick has dropped or in the first couple of seconds, splits
    // the wall: the directions play on the left and the buttons on the right.
    void startTwo() {
        two = true;
        const int half = W / 2;
        setupLane(0, 0, half - 1);   // a dim line between, as in the Stacker race
        setupLane(1, half, W - half);
        sfx("stickbridge", "Join");
    }

    void onPress(int ctl) {
        if (owner < 0) {
            owner = ctl;
            startClock = clock;
            sfxStop("stickbridge");   // the title's GameSelected
            sfx("stickbridge", "Start");
            if (firstStarts) {
                return;   // the press that started the game isn't a stick
            }
        }
        if (!two && ctl != owner) {
            if (lanes[0].drops == 0 || clock - startClock < JOIN_TIME) {
                startTwo();
            }
            return;   // otherwise someone else's buttons: ignored
        }
        Lane &l = lanes[two ? ctl : 0];
        if (l.phase == Phase::Ready) {
            l.press = true;
        } else if (l.phase == Phase::Walk) {
            l.pending = true;   // a little early: it grows on arrival if still held
        }
    }

    void setupLane(int i, int x0, int w) {
        Lane &l = lanes[i];
        l = Lane();
        l.x0 = x0;
        l.w = w;
        const int tall = topY + 1;   // the longest stick that fits standing up
        l.edgeX = std::max(3, (w - tall) / 2);
        l.reach = std::max(4, std::min(tall, w - 1 - l.edgeX));
        // The first box is wide, its right edge at edgeX in the world, so the
        // view starts at 0.
        l.boxes.push_back({l.edgeX - 7, 8, (int)(rnd() * 5) % 5});
        fill(l);
        l.elfX = (float)edge(l);
    }

    int edge(const Lane &l) const { return l.boxes[l.cur].x + l.boxes[l.cur].w - 1; }
    static int bow(const Box &b) { return b.x + (b.w - 1) / 2; }
    float difficulty(const Lane &l) const { return std::min(1.0f, l.level / (float)HARDEST); }
    // How fast the stick grows, a little faster as it gets harder.
    float growSpeed(const Lane &l) const { return 13.0f + 3.0f * difficulty(l); }
    // As long as it can grow: within reach, and short of the box after next,
    // so a stick that's too long always ends over a gap.
    int maxLen(const Lane &l) const {
        return std::max(1, std::min(l.reach, l.boxes[l.cur + 2].x - edge(l) - 1));
    }

    // Boxes up to the far side of the view and a few ahead: narrower and
    // further apart as the lane's level rises, the next one always within reach.
    void fill(Lane &l) {
        const float view = std::max(l.cam, l.camTo);   // where the view is, or is sliding to
        while ((int)l.boxes.size() < l.cur + 4 || l.boxes.back().x < view + l.w + 2) {
            const Box &p = l.boxes.back();
            const float d = difficulty(l);
            const int wMax = std::max(2, std::min(9, l.reach - 2));
            const int bw = std::clamp((int)std::lround(7.0f - 4.5f * d + (rnd() * 3.0f - 1.5f)), 2, wMax);
            const float lo = 3.0f + 5.0f * d, hi = 8.0f + 6.0f * d;
            const int gap = std::clamp((int)std::lround(lo + rnd() * (hi - lo)), 2, std::max(2, l.reach - bw));
            int colour = (int)(rnd() * 5) % 5;
            if (colour == p.colour) {
                colour = (colour + 1) % 5;   // neighbours in different paper
            }
            l.boxes.push_back({p.x + p.w + gap, bw, colour});
        }
    }

    // The cheat codes: five more points and five levels harder, or straight
    // to the hardest boxes. Boxes not yet in play are made again to suit.
    void applyCheat(int cheat) {
        for (int i = 0; i < laneCount(); ++i) {
            Lane &l = lanes[i];
            if (l.phase == Phase::Out) {
                continue;
            }
            if (cheat == CHEAT_NEXT_LEVEL) {
                l.score += 5;
                l.level += 5;
            } else if (cheat == CHEAT_LAST_LEVEL) {
                l.level = std::max(l.level, HARDEST);
            }
            // The next box too, unless a stick is already on its way to it.
            const int keep = l.cur + (l.phase == Phase::Ready ? 1 : 2);
            if ((int)l.boxes.size() > keep) {
                l.boxes.erase(l.boxes.begin() + keep, l.boxes.end());
            }
            fill(l);
        }
    }

    void stepLane(int i, float dt) {
        Lane &l = lanes[i];
        l.phaseT += dt;
        l.perfectT = std::max(0.0f, l.perfectT - dt);
        if (l.slideT >= 0) {
            l.slideT += dt;
            const float f = std::min(1.0f, l.slideT / SLIDE_TIME), e = 1.0f - (1.0f - f) * (1.0f - f) * (1.0f - f);
            l.cam = l.camFrom + (l.camTo - l.camFrom) * e;
            if (f >= 1.0f) {
                l.slideT = -1;
            }
        }
        for (auto &s : l.sparks) {
            s.age += dt;
            s.vy += 12.0f * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
        }
        l.sparks.erase(std::remove_if(l.sparks.begin(), l.sparks.end(), [](const Spark &s) { return s.age >= s.life; }),
                       l.sparks.end());

        switch (l.phase) {
        case Phase::Ready: {
            const bool go = l.press || (l.pending && holding(i));
            l.press = l.pending = false;
            if (!go && l.phaseT > IDLE_TIME) {
                l.phase = Phase::Out;
                l.phaseT = 0;
                laneOut(i);
                break;
            }
            if (go && owner >= 0) {
                l.phase = Phase::Grow;
                l.phaseT = 0;
                l.growT = 0;
                l.fullT = 0;
                l.angle = 0;
                l.len = 1;
                sfx("stickbridge", "Grow1", 0.3f);
            }
            break;
        }
        case Phase::Grow: {
            // Let go: it drops at the length on the wall now, the one the
            // player saw when they let go.
            if (!holding(i) || l.fullT > FULL_HOLD) {
                l.phase = Phase::Drop;
                l.phaseT = 0;
                ++l.drops;
                sfx("stickbridge", "Drop");
                break;
            }
            l.growT += dt;
            const int top = maxLen(l);
            const int want = std::min(top, 1 + (int)(l.growT * growSpeed(l)));
            if (want > l.len) {
                l.len = want;
                sfx("stickbridge", "Grow" + std::to_string(std::min(GROW_NOTES, l.len)), 0.3f);
            }
            if (l.len >= top) {
                l.fullT += dt;
            }
            break;
        }
        case Phase::Drop: {
            // Falling over: slow to start, quick at the end, as it would.
            const float f = std::min(1.0f, l.phaseT / DROP_TIME);
            l.angle = 90.0f * f * f;
            if (f >= 1.0f) {
                landed(l);
            }
            break;
        }
        case Phase::Walk: {
            if (l.phaseT < WALK_PAUSE) {
                break;
            }
            const float before = l.elfX;
            l.elfX = std::min((float)l.target, l.elfX + WALK_SPEED * dt);
            if ((int)std::floor(l.elfX / 2) != (int)std::floor(before / 2)) {
                sfx("stickbridge", "Step", 0.25f);   // every other LED
            }
            if (l.elfX >= l.target) {
                if (l.made) {
                    arrive(l);
                } else {
                    l.phase = Phase::Tip;
                    l.phaseT = 0;
                }
            }
            break;
        }
        case Phase::Tip:
            if (l.phaseT >= TIP_TIME) {
                l.phase = Phase::Fall;
                l.phaseT = 0;
                l.elfVy = 0;
                sfx("stickbridge", "Fall");
            }
            break;
        case Phase::Fall: {
            l.elfVy += GRAVITY * dt;
            l.elfY += l.elfVy * dt;
            // A short stick swings on down into the gap; a long one rests on
            // the box it reached over.
            if (l.tooShort) {
                const float f = std::min(1.0f, l.phaseT / SWING_TIME);
                l.angle = 90.0f + 90.0f * f * f;
            }
            if (topY - 3 + l.elfY > H + 1) {
                l.phase = Phase::Out;
                l.phaseT = 0;
                laneOut(i);
            }
            break;
        }
        case Phase::Out:
            break;
        }
    }

    // The stick is down: did its end land on the next box, and on its bow?
    void landed(Lane &l) {
        const Box &n = l.boxes[l.cur + 1];
        const int end = edge(l) + l.len;
        l.angle = 90.0f;
        l.made = end >= n.x && end <= n.x + n.w - 1;
        l.tooShort = end < n.x;
        if (l.made) {
            sfx("stickbridge", "Land");
            if (end == bow(n)) {
                ++l.score;
                ++l.perfects;
                l.perfectT = 1.0f;
                sparkle(l, end + 0.5f, topY + 0.5f);
                sfx("stickbridge", "Perfect");
            }
        }
        l.target = l.made ? n.x + n.w - 1 : end;
        l.phase = Phase::Walk;
        l.phaseT = 0;
        l.pending = false;
    }

    // Across: a point, and the view slides along so this box's edge is
    // where the last one's was. The stick stays as a bridge behind.
    void arrive(Lane &l) {
        l.bridges.push_back({edge(l) + 1, l.len});
        ++l.cur;
        ++l.score;
        ++l.crossed;
        ++l.level;
        sfx("stickbridge", "Cross");
        l.camFrom = l.cam;
        l.camTo = (float)(edge(l) - l.edgeX);
        l.slideT = 0;
        l.phase = Phase::Ready;
        l.phaseT = 0;
        l.len = 0;
        l.elfX = (float)edge(l);
        // Forget what's well behind.
        while (l.cur > 4) {
            l.boxes.erase(l.boxes.begin());
            --l.cur;
        }
        l.bridges.erase(std::remove_if(l.bridges.begin(), l.bridges.end(),
                                       [&](const Bridge &b) { return b.x + b.len < l.camTo - 2; }),
                        l.bridges.end());
        fill(l);
    }

    void sparkle(Lane &l, float x, float y) {
        for (int k = 0; k < 14; ++k) {
            const float a = 6.2831853f * (k + rnd() * 0.5f) / 14, sp = 4.0f + rnd() * 7.0f;
            l.sparks.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp - 3.0f, 0, 0.4f + rnd() * 0.5f,
                                k % 2 ? GOLD : ArcadeCanvas::WHITE});
        }
    }

    // Alone, a fall ends the game. With two it puts that side out, and the
    // game ends when both are.
    void laneOut(int i) {
        if (two && lanes[1 - i].phase != Phase::Out) {
            return;
        }
        const int a = lanes[0].score, b = two ? lanes[1].score : 0;
        const std::string both = std::to_string(a) + ":" + std::to_string(b);
        if (!two) {
            overLines = {"GAME", "OVER", std::to_string(a)};
        } else if (a == b) {
            overLines = {"IT'S A", "TIE", both};
        } else {
            overLines = {a > b ? "LEFT" : "RIGHT", "WINS", both};
        }
        // The best kept is the score - the better side's, with two - and a
        // good game is a score of goodGame.
        const int best = std::max(a, b);
        int previous = 0;
        const bool newBest = arcadeRecordBest(model->getName(), "Stick Bridge", best, &previous);
        showConfig = ShowConfig();
        showStory.reset();
        showDue = arcadeWantShow(endShow, best >= goodGame, newBest);
        if (showDue) {
            if (two) {
                showConfig = StickBridgeShow::config2(a, b, newBest, previous);
                showStory = std::make_unique<StickBridgeShow::Story>(true);
            } else {
                showConfig = StickBridgeShow::config(a, lanes[0].perfects, newBest, previous);
                showStory = std::make_unique<StickBridgeShow::Story>();
            }
        }
        over = true;
        overT = 0;
    }

    // Handing the model back, after GAME OVER or the end show.
    int32_t finish() {
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();
        finished = true;
        return EFFECT_AFTER_NEXT_OUTPUT;
    }

    // ---------------------------------------------------------------- the sky
    struct Flake {
        float x, y, vy, phase;
    };
    struct Star {
        int x, y;
        float phase;
    };
    void buildSky() {
        stars.clear();
        for (int i = 0; i < std::max(6, W * H / 70); ++i) {
            stars.push_back({(int)(rnd() * W), (int)(rnd() * topY * 0.7f), rnd() * 6.28f});
        }
        flakes.clear();
        for (int i = 0; i < std::max(6, W * H / 80); ++i) {
            flakes.push_back({rnd() * W, rnd() * H, 1.5f + rnd() * 2.5f, rnd() * 6.28f});
        }
    }
    void stepSky(float dt) {
        for (auto &f : flakes) {
            f.y += f.vy * dt;
            f.x += std::sin(clock * 1.3f + f.phase) * 1.0f * dt;
            if (f.y >= H) {
                f.y -= H + 1;
                f.x = rnd() * W;
            }
        }
    }

    // ---------------------------------------------------------------- drawing
    void render() {
        canvas.clear();
        // The sky no more than 2%: a coloured fill washes out on the real wall.
        for (int y = 0; y < H; ++y) {
            const float g = 0.02f * (1.0f - (float)y / H);
            for (int x = 0; x < W; ++x) {
                canvas.add(x, y, {0.0f, 0.03f, 1.0f}, g);
            }
        }
        for (auto &s : stars) {
            canvas.add(s.x, s.y, {0.9f, 0.9f, 1.0f}, 0.1f + 0.08f * std::sin(clock * 2.5f + s.phase));
        }
        for (auto &f : flakes) {
            canvas.add((int)f.x, (int)f.y, ArcadeCanvas::WHITE, 0.22f);
        }
        if (two) {
            for (int y = 0; y < H; ++y) {
                canvas.set(W / 2 - 1, y, {0.4f, 0.4f, 0.55f}, 0.2f);
            }
        }
        for (int i = 0; i < laneCount(); ++i) {
            drawLane(i);
        }
        canvas.writeTo(model);
        drawText();
    }

    void drawLane(int i) {
        const Lane &l = lanes[i];
        const float dim = l.phase == Phase::Out ? 0.35f : 1.0f;
        const int camI = (int)std::lround(l.cam);
        auto put = [&](int wx, int y, const RGB &c, float k) {
            const int x = l.x0 + wx - camI;
            if (x >= l.x0 && x < l.x0 + l.w) {
                canvas.set(x, y, c, k * dim);
            }
        };
        // Gift boxes: a brighter lid, a ribbon down the middle and round the
        // side, and the bow - the perfect spot - on top.
        const float pulse = 0.8f + 0.2f * std::sin(clock * 5.0f);
        for (const auto &b : l.boxes) {
            const RGB &paper = BOXES[b.colour];
            const RGB &ribbon = b.colour == 3 ? RIBBON_ON_GOLD : RIBBON;
            const int mid = bow(b), band = topY + boxH / 2;
            for (int x = b.x; x < b.x + b.w; ++x) {
                for (int y = topY; y < H; ++y) {
                    if (x == mid && y == topY) {
                        put(x, y, BOW, pulse);
                    } else if (x == mid || y == band) {
                        put(x, y, ribbon, y == topY ? 0.7f : 0.45f);
                    } else {
                        put(x, y, paper, y == topY ? 0.6f : 0.3f);
                    }
                }
            }
        }
        for (const auto &br : l.bridges) {
            for (int k = 0; k < br.len; ++k) {
                put(br.x + k, topY, cane(k), 0.75f);
            }
        }
        // The stick, standing, falling or across, about the corner of the box.
        if (l.phase != Phase::Ready && l.phase != Phase::Out && l.len > 0) {
            const float a = l.angle * 3.14159265f / 180.0f;
            const float px = edge(l) + 1.5f, py = topY + 0.5f;
            for (float t = 0; t <= l.len - 1 + 0.01f; t += 0.5f) {
                put((int)std::floor(px + std::sin(a) * t), (int)std::floor(py - std::cos(a) * t), cane((int)t), 0.9f);
            }
        }
        // The elf: a Santa hat with its bobble hanging back, a face and a
        // tunic, its back foot showing on every other step.
        if (l.phase != Phase::Out) {
            const int fx = (int)std::floor(l.elfX + 0.5f), fy = topY - 1 + (int)std::lround(l.elfY);
            const RGB &tunic = TUNIC[two ? i : 0];
            put(fx, fy, tunic, 0.9f);
            put(fx, fy - 1, FACE, 0.8f);
            put(fx, fy - 2, HAT, 1.0f);
            put(fx - 1, fy - 2, BOBBLE, 0.7f);
            if (l.phase == Phase::Walk && l.phaseT >= WALK_PAUSE && ((int)std::floor(l.elfX) & 1)) {
                put(fx - 1, fy, tunic, 0.5f);
            }
        }
        for (const auto &s : l.sparks) {
            const int x = l.x0 + (int)std::floor(s.x) - camI;
            if (x >= l.x0 && x < l.x0 + l.w) {
                canvas.add(x, (int)std::floor(s.y), s.col, 1.0f - s.age / s.life);
            }
        }
    }

    static const RGB &cane(int k) { return (k / 2) % 2 ? CANE_WHITE : CANE_RED; }

    void drawText() {
        if (over && overT > 1.0f) {
            textBox(overLines, 255, 255, 255);
            return;
        }
        PanelTransform pt(this);
        for (int i = 0; i < laneCount(); ++i) {
            const Lane &l = lanes[i];
            // The score, dim, in the lane's top right corner: in the elf's
            // colour with two, so each side knows theirs.
            const std::string s = std::to_string(l.score);
            const RGB c = two ? TUNIC[i] : RGB{0.55f, 0.55f, 0.67f};
            const float k = two ? 0.55f : 1.0f;
            outputString(s, l.x0 + l.w - (int)s.size() * 4 + 1, 0, (int)(c.r * 255 * k), (int)(c.g * 255 * k),
                         (int)(c.b * 255 * k), 1);
            // Until the first stick: how to play.
            auto centred = [&](const std::string &t, int y, int r, int g, int b) {
                outputString(t, l.x0 + (l.w - ((int)t.size() * 4 - 1)) / 2, y, r, g, b, 1);
            };
            if (over) {
                continue;
            }
            if (l.drops == 0 && l.phase == Phase::Ready && std::fmod(clock, 1.0f) < 0.65f) {
                centred("HOLD", 6, 150, 150, 170);
            } else if (l.perfectT > 0) {
                centred("PERFECT", 6, 255, 205, 50);
            } else if (two && l.phase == Phase::Out) {
                centred("OUT", 6, 160, 60, 50);
            }
        }
    }

    int endShow = 1, goodGame = 10;   // the End Show and Good Game (score) settings
    bool firstStarts = true;
    ArcadeShowPlayer show;
    ShowConfig showConfig;
    std::unique_ptr<ShowStory> showStory;
    bool showDue = false;   // the game earned the show; it starts after the fall

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 0, H = 0, boxH = 6, topY = 19;
    float clock = 0, startClock = 0;

    Lane lanes[2];
    bool two = false;
    int owner = -1;   // alone: the control that plays (0 directions, 1 buttons); -1 before the first press
    bool over = false;
    float overT = 0;
    std::vector<std::string> overLines;

    std::vector<Flake> flakes;
    std::vector<Star> stars;

    std::atomic<uint32_t> held[2] = {{0}, {0}};
    std::mutex pressLock;
    std::vector<int> presses;   // control presses, handled on the effect's thread
    bool finished = false;
};


const std::string &FPPStickBridge::getName() {
    static const std::string name = "Stick Bridge";
    return name;
}

void FPPStickBridge::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    StickBridgeEffect *effect = dynamic_cast<StickBridgeEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    const bool byStart = button.compare(0, 5, "Start") == 0;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new StickBridgeEffect(m, endShowOption(), goodGameOption(10), !byStart);
        m->setRunningEffect(effect, 25);
    }
    if (!starting || !byStart) {
        effect->button(button);
    }
}
