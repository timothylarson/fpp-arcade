#include <fpp-pch.h>

#include "FPPSkyJump.h"
#include "ArcadeCanvas.h"
#include "SkyJumpShow.h"

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

// An elf with a rocket pack climbs the night sky. Gravity always pulls it
// down; every coin it touches gives it a little hop and every blast a big
// one, so a run of coins is a ladder and a blast skips a stretch of it. A
// star makes it invincible for a few seconds, smashing anything it touches.
// Without one, an enemy knocks it back down. The camera only ever goes up:
// fall off the bottom and that's the end (or a rescue, with lives to spare).
//
// The wall is short (25 rows) and wide, so the camera keeps the elf a little
// below the middle - 15 rows of warning above it - and lower still while it
// climbs fast. The elf wraps round the sides, as in the phone games, so
// nothing on a wide field is ever too far away.
//
// The course is laid out ahead of the camera as a "lifeline": a chain of
// coins (and the odd blast), each within a hop and a sideways steer of the
// last, so there is always a way up. Coin patterns, spare blasts, stars and
// enemies fill in around it. Higher up the lifeline coins get further apart,
// the extras thin out and enemies are more common; the sky darkens, the moon
// comes by, Santa crosses in the distance and, at the top, the aurora.
//
// Two players: the D-pad plays the left half and A/B (B steers left, A right)
// the right, each with its own climber on the same course.

namespace {

// Rows (LEDs) and seconds. A coin's hop lifts the elf 7 rows, a blast 15 -
// over half the screen - and launch 18. That much hang time is what makes it
// playable on 25 rows: each hop is a second or more to see the next coin and
// steer to it.
constexpr float GRAVITY = 13.0f;
constexpr float COIN_V = 13.5f;
constexpr float BLAST_V = 20.0f;
constexpr float LAUNCH_V = 22.0f;
constexpr float MAX_FALL = 11.0f;
constexpr float STEER_V = 18.0f;     // across a 60-wide wall in just over 3 seconds
constexpr float WALK_V = 9.0f;       // on the snow before launch
constexpr float STEER_ACC = 110.0f;  // to full speed in about a sixth of a second
constexpr float STAR_TIME = 5.0f;
constexpr float DAZED_TIME = 1.2f;
constexpr float RESCUE_SAFE = 2.0f;
constexpr float JOIN_WINDOW = 2.5f;  // after launch, the other control can still start a race
constexpr float GROUND_WAIT = 3.0f;  // then it launches by itself
constexpr float COUNT_STEP = 0.8f;   // the race's 3, 2, 1
constexpr float BAND_H = 250.0f;     // the difficulty steps up every 250 rows...
constexpr int TOP_BAND = 4;          // ...and is at its hardest from 1000
constexpr int COIN_PTS = 2, SMASH_PTS = 15;

enum Kind { COIN, BLAST, STAR, CLOUD, BAT, METEOR };

// Held buttons, by control: the D-pad's left and right, and B (left) and A (right).
enum : uint32_t { D_LEFT = 1, D_RIGHT = 2, B_LEFT = 4, B_RIGHT = 8 };
// One-shot presses since the last frame.
enum : uint32_t { P_DPAD = 1, P_UP = 2, P_BUTTONS = 4 };

const RGB GOLD = {1.0f, 0.72f, 0.08f};
const RGB BLAST_COL = {0.2f, 0.85f, 1.0f};
const RGB CLOUD_COL = {0.55f, 0.6f, 0.72f};
const RGB EYE_COL = {1.0f, 0.1f, 0.05f};
const RGB BAT_COL = {0.65f, 0.15f, 1.0f};
const RGB METEOR_COL = {1.0f, 0.45f, 0.1f};
const RGB SNOW = {0.85f, 0.9f, 1.0f};
const RGB FLAME = {1.0f, 0.55f, 0.05f};
const RGB HUD = {0.35f, 0.35f, 0.45f};

// Cheered as the climb passes each 250 rows; the last one repeats.
const char *CHEERS[] = {"NICE!", "GREAT!", "WOW!", "SUPER!", "ACE!"};

RGB hue(float h) {
    h -= std::floor(h);
    return {ArcadeCanvas::clamp01(std::fabs(h * 6 - 3) - 1), ArcadeCanvas::clamp01(2 - std::fabs(h * 6 - 2)),
            ArcadeCanvas::clamp01(2 - std::fabs(h * 6 - 4))};
}

}  // namespace

class SkyJumpEffect : public FPPArcadeGameEffect {
public:
    // lives: falls each climber can take (all but the last are rescued);
    // speedPct scales the whole game's pace; showMode and good are the End
    // Show and Good Game settings.
    SkyJumpEffect(PixelOverlayModel *m, int lives, int speedPct, int showMode, int good)
        : FPPArcadeGameEffect(m), startLives(std::clamp(lives, 1, 9)),
          speed(std::clamp(speedPct, 50, 200) / 100.0f), endShow(showMode), goodGame(good),
          rng(std::random_device{}()) {
        model->getSize(W, H);
        W = std::max(16, W);
        H = std::max(12, H);
        canvas.resize(W, H);
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "SkyJump";
        return NAME;
    }

    void button(const std::string &b) override {
        if (show.on()) {
            if (b.find("Pressed") != std::string::npos) {
                show.skip();
            }
            return;
        }
        const bool pressed = b.size() > 10 && b.compare(b.size() - 10, 10, " - Pressed") == 0;
        const bool released = b.size() > 11 && b.compare(b.size() - 11, 11, " - Released") == 0;
        const std::string n = b.substr(0, b.find(" - "));
        const bool dpad = n == "Up" || n == "Down" || n == "Left" || n == "Right" || n == "Up/Left" ||
                          n == "Up/Right" || n == "Down/Left" || n == "Down/Right";
        const bool buttons = n == "A Button" || n == "B Button" || n == "Fire" || n == "Fire Button";
        // Held steering is tracked even while paused, so a release isn't lost.
        if (pressed || released) {
            uint32_t bits = 0;
            if (dpad && n.find("Left") != std::string::npos) bits |= D_LEFT;
            if (dpad && n.find("Right") != std::string::npos) bits |= D_RIGHT;
            if (n == "B Button") bits |= B_LEFT;
            if (buttons && n != "B Button") bits |= B_RIGHT;
            if (pressed) {
                held.fetch_or(bits);
            } else {
                held.fetch_and(~bits);
            }
        }
        const bool wasPaused = paused;
        FPPArcadeGameEffect::button(b);
        if (wasPaused || paused || !pressed) {
            return;
        }
        uint32_t p = 0;
        if (dpad) p |= P_DPAD;
        if (n == "Up" || n == "Up/Left" || n == "Up/Right") p |= P_UP;
        if (buttons) p |= P_BUTTONS;
        presses.fetch_or(p);
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
        const float real = (float)(consumeElapsedMs(FRAME_MS, 100.0) / 1000.0);
        const float dt = real * speed;
        clock += dt;
        if (!over) {
            const int cheat = takeCheat();
            if (cheat == CHEAT_NEXT_LEVEL) {
                heightBonus = std::min(BAND_H * TOP_BAND, heightBonus + BAND_H);
            } else if (cheat == CHEAT_LAST_LEVEL) {
                // The hardest the course gets, with lives topped up and a star to start on.
                heightBonus = BAND_H * TOP_BAND;
                for (auto &p : pl) {
                    if (!p.out) {
                        p.lives = 9;
                        p.starT = STAR_TIME;
                    }
                }
            }
            onPresses(presses.exchange(0));
            step(dt);
        } else {
            presses.store(0);
            overT += real;
            if (overT > 1.0f && overT - real <= 1.0f) {
                sfxStop("skyjump");
                // A good game (or a new best) gets the end show where the
                // GAME OVER box would be, once the fall has had its second.
                if (showDue) {
                    showDue = false;
                    sfxEndShow("skyjump");
                    show.start(model, std::move(showConfig), std::move(showStory));
                    resetFrameTimer();
                    return FRAME_MS;
                }
                sfx("skyjump", "GameOver");
            }
            for (int i = 0; i < lanes(); ++i) {
                stepVisuals(pl[i], dt);
            }
            if (overT > 5.0f) {
                return finish();
            }
        }
        beginFrame();   // drawn in passes, the text last: see beginFrame()
        render();
        endFrame();
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 25;
    enum class Phase { Ground, Countdown, Play };

    struct Thing {
        int kind;
        float x, y;            // column in the lane, and height (the bottom row of its sprite)
        float baseX, baseY;    // where a cloud or bat moves about
        float vx, vy, phase;
        float hitT;            // an enemy that just knocked the elf flashes
    };
    struct Spark {
        float x, y, vx, vy, age, life;
        RGB col;
    };
    struct Flake {
        float x, y, vy, phase;
    };
    struct SkyStar {
        int x;
        float y, phase;
    };
    struct Spot {
        float x, y;
    };
    // One climber and its half (or all) of the wall. Its x is in the lane's
    // own columns, 0 to w - 1, and heights go up from the snow at 0.
    struct Player {
        int x0 = 0, w = 60;
        RGB suit, hat;
        std::mt19937 course;   // lays out the course: a race's two get the same one
        float x = 0, y = 1, vx = 0, vy = 0;
        bool launched = false, out = false;
        int lives = 1, coins = 0, smashed = 0, chain = 0, band = 0;
        float maxH = 0, starT = 0, dazedT = 0, safeT = 0, flameT = 0, chainT = 0, outT = 0;
        float cam = 0, camV = 0;   // the height at the bottom row, and how fast it's going up
        float lifeX = 0, lifeY = 0;
        int lifeKind = COIN;
        std::vector<Thing> things;
        std::vector<Spot> placed;  // recent placements, for spacing (collected or not)
        std::vector<Spark> sparks;
        std::vector<Flake> flakes;
        std::vector<SkyStar> stars;
        float sleighX = 0, sleighDue = 20;
        int sleighRow = 2, sleighDir = 1;
        bool sleigh = false;
        std::string word;
        float wordT = 0;
        int score() const { return (int)maxH + COIN_PTS * coins + SMASH_PTS * smashed; }
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    // The course's own random numbers, so both sides of a race get the same one.
    static float range(Player &p, float a, float b) {
        return a + (b - a) * std::uniform_real_distribution<float>(0.0f, 1.0f)(p.course);
    }
    static float wrapX(const Player &p, float x) {
        x = std::fmod(x, (float)p.w);
        return x < 0 ? x + p.w : x;
    }
    // The shorter way from a to b round the lane, either direction.
    static float wrapDelta(const Player &p, float d) {
        d = std::fmod(d, (float)p.w);
        if (d > p.w / 2.0f) d -= p.w;
        if (d < -p.w / 2.0f) d += p.w;
        return d;
    }
    int lanes() const { return split ? 2 : 1; }

    // ---------------------------------------------------------------- game flow
    void newGame() {
        split = false;
        owner = -1;
        heightBonus = 0;
        over = false;
        overT = 0;
        showDue = false;
        showStory.reset();
        phase = Phase::Ground;
        phaseT = 0;
        playT = 0;
        presses.store(0);
        layOut((uint32_t)rng());
        sfxStop("skyjump");   // cuts off the title's GameSelected
    }

    // One lane across the whole wall, or two equal ones with a two-LED gap
    // between - equal, so both sides of a race run the very same course.
    void layOut(uint32_t seed) {
        if (split) {
            const int w = std::max(6, (W - 2) / 2);
            pl[0].x0 = 0;
            pl[1].x0 = W - w;
            pl[0].w = pl[1].w = w;
            pl[0].suit = {0.1f, 0.95f, 0.2f};
            pl[0].hat = {1.0f, 0.1f, 0.05f};
            pl[1].suit = {0.2f, 0.55f, 1.0f};
            pl[1].hat = {1.0f, 1.0f, 1.0f};
        } else {
            pl[0].x0 = 0;
            pl[0].w = W;
            pl[0].suit = {0.1f, 0.95f, 0.2f};
            pl[0].hat = {1.0f, 0.1f, 0.05f};
        }
        for (int i = 0; i < lanes(); ++i) {
            resetPlayer(pl[i], seed);
        }
    }

    void resetPlayer(Player &p, uint32_t seed) {
        p.course.seed(seed);
        p.x = p.w / 2.0f;
        p.y = 1;
        p.vx = p.vy = 0;
        p.launched = p.out = false;
        p.lives = startLives;
        p.coins = p.smashed = p.chain = p.band = 0;
        p.maxH = p.starT = p.dazedT = p.safeT = p.flameT = p.chainT = p.outT = 0;
        p.cam = p.camV = 0;
        p.lifeX = p.x;
        p.lifeY = 3;
        p.lifeKind = COIN;
        p.things.clear();
        p.placed.clear();
        p.sparks.clear();
        p.flakes.clear();
        p.stars.clear();
        for (int i = 0; i < std::max(3, p.w * H / 150); ++i) {
            p.flakes.push_back({rnd() * p.w, rnd() * H, 1.5f + rnd() * 2.5f, rnd() * 6.28f});
        }
        for (int i = 0; i < std::max(4, p.w * H / 90); ++i) {
            p.stars.push_back({(int)(rnd() * p.w), rnd() * H, rnd() * 6.28f});
        }
        p.sleigh = false;
        p.sleighDue = 8 + rnd() * 10;
        p.word.clear();
        p.wordT = 0;
        generate(p, H + 12.0f);
    }

    // The first press picks the control that plays; the other one, before
    // launch or in the first moments after, makes it a race.
    void onPresses(uint32_t bits) {
        if (!bits || split) {
            return;   // in a race both climbers launch together and only steer
        }
        const bool dpad = bits & P_DPAD, buttons = bits & P_BUTTONS;
        if (owner < 0) {
            owner = dpad ? 0 : (buttons ? 1 : -1);
        }
        const bool other = (owner == 0 && buttons) || (owner == 1 && dpad);
        if (other && (phase == Phase::Ground || (phase == Phase::Play && playT < JOIN_WINDOW && !pl[0].out))) {
            startRace();
            return;
        }
        const bool launchPress = owner == 0 ? (bits & P_UP) != 0 : buttons;
        if (phase == Phase::Ground && launchPress) {
            launch(pl[0]);
            phase = Phase::Play;
            playT = 0;
        }
    }

    void startRace() {
        split = true;
        layOut((uint32_t)rng());
        phase = Phase::Countdown;
        phaseT = 0;
        lastCount = -1;
        sfxStop("skyjump");
        sfx("skyjump", "Join");
    }

    void launch(Player &p) {
        p.launched = true;
        p.vy = LAUNCH_V;
        p.flameT = 0.8f;
        burst(p, p.x, 0.5f, {1, 1, 1}, 10, 6.0f);   // a puff of snow
        sfx("skyjump", "Launch");
    }

    // Steering for lane i: its own control in a race; alone, the one that
    // started the game (either, if it began from Start).
    int steer(int i) const {
        const uint32_t h = held.load();
        const int ctl = split ? i : owner;
        int d = 0;
        if (ctl != 1) d += ((h & D_RIGHT) ? 1 : 0) - ((h & D_LEFT) ? 1 : 0);
        if (ctl != 0) d += ((h & B_RIGHT) ? 1 : 0) - ((h & B_LEFT) ? 1 : 0);
        return std::clamp(d, -1, 1);
    }

    void step(float dt) {
        phaseT += dt;
        if (phase == Phase::Ground && phaseT > GROUND_WAIT) {
            launch(pl[0]);   // nobody pressed launch: off it goes anyway
            phase = Phase::Play;
            playT = 0;
        } else if (phase == Phase::Countdown) {
            const int n = (int)(phaseT / COUNT_STEP);
            if (n != lastCount && n < 3) {
                lastCount = n;
                sfx("skyjump", "Count");
            }
            if (phaseT >= 3 * COUNT_STEP) {
                launch(pl[0]);
                launch(pl[1]);
                phase = Phase::Play;
                playT = 0;
            }
        } else if (phase == Phase::Play) {
            playT += dt;
        }
        for (int i = 0; i < lanes(); ++i) {
            stepPlayer(pl[i], steer(i), dt);
        }
        if (phase == Phase::Play && !over && pl[0].out && (!split || pl[1].out)) {
            endGame();
        }
    }

    void stepPlayer(Player &p, int dir, float dt) {
        stepVisuals(p, dt);
        if (p.out) {
            p.outT += dt;
            return;
        }
        // Steering eases in and out, so a tap is a nudge and holding it a dash.
        const float want = dir * (p.launched ? STEER_V : WALK_V);
        const float acc = STEER_ACC * dt;
        p.vx += std::clamp(want - p.vx, -acc, acc);
        p.x = wrapX(p, p.x + p.vx * dt);
        p.flameT = std::max(0.0f, p.flameT - dt);
        if (!p.launched) {
            return;   // on the snow
        }
        p.starT = std::max(0.0f, p.starT - dt);
        p.dazedT = std::max(0.0f, p.dazedT - dt);
        p.safeT = std::max(0.0f, p.safeT - dt);
        p.chainT = std::max(0.0f, p.chainT - dt);
        const bool starred = p.starT > 0;
        p.vy = std::max(-MAX_FALL, p.vy - GRAVITY * dt);
        const float y0 = p.y;
        p.y += p.vy * dt;
        if (p.y < 1 && p.cam < 1) {
            // Back on the snow before the climb got going: it bounces off again.
            p.y = 1;
            launch(p);
        }
        if (starred && p.starT <= 0) {
            sfx("skyjump", "StarOut", 0.6f);
        }
        p.maxH = std::max(p.maxH, p.y - 1);
        moveThings(p, dt);
        collide(p, y0);
        followCamera(p, dt);
        generate(p, p.cam + H + 12.0f);
        prune(p);
        // Cheer each 250 rows climbed.
        if ((int)(p.maxH / BAND_H) > p.band) {
            p.word = CHEERS[std::min(p.band, 4)];
            ++p.band;
            p.wordT = 1.6f;
            sfx("skyjump", "Higher", 0.8f);
        }
        if (p.y < p.cam - 2) {
            fall(p);
        }
    }

    // The camera keeps the elf about 9 rows up from the bottom (15 rows of
    // sky above it on a 25-row wall), and up to 4 rows lower while it's
    // climbing fast, so there's more warning of what's coming. It never comes
    // back down.
    void followCamera(Player &p, float dt) {
        const float anchor = (H - 1) * 0.36f;
        const float look = std::clamp((p.vy - COIN_V * 0.5f) * 0.3f, 0.0f, 4.0f);
        const float want = p.y - anchor + look;
        const float before = p.cam;
        if (want > p.cam) {
            p.cam += (want - p.cam) * std::min(1.0f, dt * 10.0f);
        }
        p.camV = dt > 0 ? (p.cam - before) / dt : 0;
    }

    // Off the bottom: rescued if there's a life to spare - blasted back up
    // from below, blinking and safe for a moment - or out.
    void fall(Player &p) {
        if (p.lives > 1) {
            --p.lives;
            p.y = p.cam - 1.5f;
            p.vy = LAUNCH_V;
            p.flameT = 1.0f;
            p.safeT = RESCUE_SAFE;
            p.dazedT = 0;
            burst(p, p.x, p.cam, {1, 0.8f, 0.3f}, 12, 7.0f);
            sfx("skyjump", "Rescue");
            return;
        }
        p.lives = 0;
        p.out = true;
        p.outT = 0;
        sfx("skyjump", "Fall");
    }

    // Everyone has fallen: record the best, and get the end show ready (it
    // starts a second later, where GAME OVER would be).
    void endGame() {
        over = true;
        overT = 0;
        const int s0 = pl[0].score(), s1 = split ? pl[1].score() : 0;
        const int best = std::max(s0, s1);
        int previous = 0;
        const bool newBest = arcadeRecordBest(model->getName(), "Sky Jump", best, &previous);
        showDue = arcadeWantShow(endShow, best >= goodGame, newBest);
        if (split) {
            const std::string both = std::to_string(s0) + ":" + std::to_string(s1);
            if (s0 == s1) {
                overLines = {"TIE!", both};
            } else {
                overLines = {s0 > s1 ? "LEFT" : "RIGHT", "WINS!", both};
            }
        } else {
            overLines = {"GAME", "OVER", std::to_string(s0)};
        }
        if (showDue) {
            if (split) {
                showConfig = SkyJumpShow::config2(s0, s1, newBest, previous);
                showStory = std::make_unique<SkyJumpShow::Story>(true, s0 >= s1);
            } else {
                showConfig = SkyJumpShow::config(s0, (int)pl[0].maxH, pl[0].coins, newBest, previous);
                showStory = std::make_unique<SkyJumpShow::Story>();
            }
        }
    }

    int32_t finish() {
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();
        finished = true;
        return EFFECT_AFTER_NEXT_OUTPUT;
    }

    // ---------------------------------------------------------------- the course
    // How hard it is at a height: 0 at the snow, 1 from 1000 rows up (or
    // sooner, after the cheat codes).
    float difficulty(float h) const { return std::clamp((h + heightBonus) / (BAND_H * TOP_BAND), 0.0f, 1.0f); }

    void generate(Player &p, float upTo) {
        while (p.lifeY < upTo) {
            stepCourse(p);
        }
    }

    // The next link of the lifeline, and whatever goes around it. Every
    // random number comes from the course in the same order whatever the
    // player has done, so a race's two courses stay the same.
    void stepCourse(Player &p) {
        const float he = p.lifeY + heightBonus;
        const float d = difficulty(p.lifeY);
        const float wide = std::clamp(p.w / 60.0f, 0.5f, 1.0f);   // a half has half the extras
        // From a coin the next one is well within a hop (7 rows) and a short
        // steer; from a blast, within most of its rise and further across.
        const bool fromBlast = p.lifeKind != COIN;
        const float dy = fromBlast ? range(p, 6.0f, 8.0f + 2.0f * d) : range(p, 2.0f + 1.2f * d, 2.8f + 1.1f * d);
        const float reach = fromBlast ? std::min(13.0f, p.w * 0.4f) : std::min(3.5f + 4.0f * d, p.w * 0.3f);
        const float nx = wrapX(p, std::round(p.lifeX + range(p, -reach, reach)));
        const float ny = p.lifeY + dy;
        const int kind = range(p, 0, 1) < 0.10f - 0.03f * d ? BLAST : COIN;
        add(p, kind, nx, ny, true);

        // Coins in patterns: plenty near the snow, fewer higher up.
        const float py = p.lifeY;
        const float fill = he < 60 ? 0.85f : 0.6f - 0.35f * d;
        if (range(p, 0, 1) < fill * wide) {
            pattern(p, py, ny);
        }
        // A spare blast now and then.
        if (range(p, 0, 1) < 0.07f * wide) {
            add(p, BLAST, std::round(range(p, 0, p.w)), range(p, py, ny), false);
        }
        // Stars once there are enemies to smash.
        if (he > 120 && range(p, 0, 1) < 0.012f + 0.01f * d) {
            add(p, STAR, std::round(range(p, 0, p.w)), range(p, py, ny), false);
        }
        if (he > 60 && range(p, 0, 1) < (0.05f + 0.32f * d) * wide) {
            enemy(p, he, d, py, ny, nx);
        }
        p.lifeX = nx;
        p.lifeY = ny;
        p.lifeKind = kind;
    }

    void pattern(Player &p, float y0, float y1) {
        const int type = std::min(3, (int)range(p, 0, 4));
        const float x = std::round(range(p, 0, p.w)), y = range(p, y0, y1);
        const int n = 3 + std::min(2, (int)range(p, 0, 3));
        const float side = range(p, 0, 1) < 0.5f ? -1.0f : 1.0f;
        switch (type) {
        case 0:   // a column: a short ladder straight up
            for (int i = 0; i < n; ++i) add(p, COIN, x, y + 2.2f * i, false);
            break;
        case 1:   // a row: points, and one hop
            for (int i = 0; i < n; ++i) add(p, COIN, wrapX(p, x + 2 * i), y, false);
            break;
        case 2:   // a diagonal: a ladder that leans
            for (int i = 0; i < n; ++i) add(p, COIN, wrapX(p, x + side * 2 * i), y + 2.0f * i, false);
            break;
        default:   // an arch
            for (int i = -2; i <= 2; ++i) add(p, COIN, wrapX(p, x + 2 * i), y + 2.0f - 0.5f * i * i, false);
            break;
        }
    }

    // An enemy somewhere off the lifeline: clouds from the start, bats from
    // 350 rows and meteors from 700.
    void enemy(Player &p, float he, float d, float y0, float y1, float lifeX) {
        const float r = range(p, 0, 1);
        int kind = CLOUD;
        if (he > 700 && r < 0.3f) {
            kind = METEOR;
        } else if (he > 350 && r < 0.65f) {
            kind = BAT;
        }
        const float y = range(p, y0 + 0.5f, y1 + 0.5f);
        // Six tries at a spot well clear of the lifeline coins above and below.
        float x = -1;
        for (int i = 0; i < 6; ++i) {
            const float c = std::round(range(p, 0, p.w));
            if (x < 0 && std::fabs(wrapDelta(p, c - lifeX)) >= 5 && std::fabs(wrapDelta(p, c - p.lifeX)) >= 5) {
                x = c;
            }
        }
        const float dirn = range(p, 0, 1) < 0.5f ? -1.0f : 1.0f;
        const float phase = range(p, 0, 6.28f);
        const float drift = range(p, -2, 2);
        if (x < 0) {
            return;
        }
        Thing t{kind, x, y, x, y, 0, 0, phase, 0};
        if (kind == BAT) {
            t.vx = dirn * (4.0f + 5.0f * d);
        } else if (kind == METEOR) {
            t.y += 8;   // it falls into place
            t.vx = drift;
            t.vy = -(1.5f + 2.5f * d);
        }
        p.things.push_back(t);
    }

    // Adds a thing unless something was put there already (forced for the
    // lifeline, which must always be there).
    void add(Player &p, int kind, float x, float y, bool force) {
        for (const Spot &s : p.placed) {
            if (std::fabs(s.y - y) < 1.6f && std::fabs(wrapDelta(p, s.x - x)) < 2.5f) {
                if (!force) {
                    return;
                }
            }
        }
        p.things.push_back({kind, x, y, x, y, 0, 0, 0, 0});
        p.placed.push_back({x, y});
        if (p.placed.size() > 48) {
            p.placed.erase(p.placed.begin());
        }
    }

    // Anything below the screen is gone for good: the camera never comes back
    // down (and meteors only fall further).
    void prune(Player &p) {
        const float below = p.cam - 6;
        p.things.erase(std::remove_if(p.things.begin(), p.things.end(), [&](const Thing &t) { return t.y < below; }),
                       p.things.end());
    }

    void moveThings(Player &p, float dt) {
        for (auto &t : p.things) {
            t.hitT = std::max(0.0f, t.hitT - dt);
            switch (t.kind) {
            case CLOUD:   // drifts to and fro
                t.x = wrapX(p, t.baseX + 2.0f * std::sin(t.phase + clock * 0.8f));
                break;
            case BAT:     // flits across, bobbing, round and round the lane
                t.x = wrapX(p, t.x + t.vx * dt);
                t.y = t.baseY + 0.8f * std::sin(t.phase + clock * 3.0f);
                break;
            case METEOR:
                t.x = wrapX(p, t.x + t.vx * dt);
                t.y += t.vy * dt;
                break;
            default:
                break;
            }
        }
    }

    // ---------------------------------------------------------------- touching
    // Pickups are generous (a column either side, more for the big ones);
    // enemies are only what you can see, so they're fair to dodge.
    void collide(Player &p, float y0) {
        const float lo = std::min(y0, p.y) - 0.5f, hi = std::max(y0, p.y) + 1.5f;   // body to hat, over the frame
        const float pc = std::floor(p.x);
        for (size_t i = 0; i < p.things.size();) {
            Thing &t = p.things[i];
            const float dx = wrapDelta(p, std::round(t.x) - pc);
            int left = 0, right = 0, rows = 1;
            float give = 0;
            switch (t.kind) {
            case COIN: left = right = 1; give = 0.5f; break;
            case BLAST: left = right = 2; rows = 2; give = 0.5f; break;
            case STAR: left = right = 2; rows = 3; give = 0.5f; break;
            case CLOUD: left = right = 2; rows = 2; break;
            case BAT: left = right = 1; rows = 3; break;   // wings up or down
            case METEOR: left = right = 0; rows = 2; break;
            }
            if (p.starT > 0 && t.kind >= CLOUD) {
                ++left;   // a star smashes anything close
                ++right;
            }
            const float bottom = t.y - 0.5f - give, top = t.y + rows - 0.5f + give;
            if (dx < -left || dx > right || top < lo || bottom > hi) {
                ++i;
                continue;
            }
            if (touch(p, t, dx)) {
                p.things.erase(p.things.begin() + i);
            } else {
                ++i;
            }
        }
    }

    // What touching it does; true if it's used up.
    bool touch(Player &p, Thing &t, float dx) {
        switch (t.kind) {
        case COIN: {
            ++p.coins;
            p.vy = std::max(p.vy, COIN_V);
            // Coins in quick succession climb a scale.
            p.chain = p.chainT > 0 ? std::min(8, p.chain + 1) : 1;
            p.chainT = 0.7f;
            sfx("skyjump", "Coin" + std::to_string(p.chain), 0.35f);
            burst(p, t.x, t.y, GOLD, 3, 3.0f);
            return true;
        }
        case BLAST:
            p.vy = std::max(p.vy, BLAST_V);
            p.flameT = 0.8f;
            sfx("skyjump", "Blast");
            burst(p, t.x, t.y, BLAST_COL, 10, 6.0f);
            return true;
        case STAR:
            p.starT = STAR_TIME;
            p.vy = std::max(p.vy, BLAST_V);
            p.flameT = 0.8f;
            sfx("skyjump", "Star");
            for (int k = 0; k < 4; ++k) burst(p, t.x, t.y + 1, hue(k * 0.25f), 4, 6.0f);
            return true;
        default:
            break;
        }
        // An enemy.
        if (p.starT > 0) {
            ++p.smashed;
            p.vy = std::max(p.vy, COIN_V);
            sfx("skyjump", "Smash");
            burst(p, t.x, t.y + 0.5f, enemyColour(t.kind), 12, 7.0f);
            return true;
        }
        if (p.dazedT > 0 || p.safeT > 0 || t.hitT > 0) {
            return false;
        }
        // Knocked aside: the climb stops dead and the elf blinks, safe from
        // another knock for a moment, to find something on the way down.
        p.vy = std::min(p.vy, 0.0f);
        p.vx = dx > 0 ? -10.0f : 10.0f;
        p.dazedT = DAZED_TIME;
        t.hitT = 0.4f;
        sfx("skyjump", "Hit");
        burst(p, p.x, p.y + 0.5f, {1, 1, 1}, 5, 4.0f);
        return false;
    }

    static RGB enemyColour(int kind) { return kind == BAT ? BAT_COL : (kind == METEOR ? METEOR_COL : CLOUD_COL); }

    // ---------------------------------------------------------------- the sky
    void burst(Player &p, float x, float y, const RGB &col, int n, float sp) {
        for (int k = 0; k < n; ++k) {
            const float a = rnd() * 6.2832f, v = sp * (0.4f + 0.6f * rnd());
            p.sparks.push_back({x, y, std::cos(a) * v, std::sin(a) * v, 0, 0.3f + 0.4f * rnd(), col});
        }
    }

    void stepVisuals(Player &p, float dt) {
        p.wordT = std::max(0.0f, p.wordT - dt);
        for (auto &s : p.sparks) {
            s.age += dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.vy -= 6.0f * dt;
        }
        p.sparks.erase(std::remove_if(p.sparks.begin(), p.sparks.end(), [](const Spark &s) { return s.age >= s.life; }),
                       p.sparks.end());
        // Snow falls, and rushes by as the camera climbs.
        const float climb = over ? 0.0f : p.camV;
        for (auto &f : p.flakes) {
            f.y += (f.vy + climb * 0.5f) * dt;
            f.x += std::sin(clock * 1.3f + f.phase) * 0.8f * dt;
            if (f.y >= H) {
                f.y -= H;
                f.x = rnd() * p.w;
            }
        }
        // Now and then, high up, Santa crosses far off in the sky.
        if (p.sleigh) {
            p.sleighX += p.sleighDir * 5.0f * dt;
            if (p.sleighX < -12 || p.sleighX > p.w + 12) {
                p.sleigh = false;
                p.sleighDue = 18 + rnd() * 15;
            }
        } else if (p.cam + heightBonus > 400 && (p.sleighDue -= dt) <= 0) {
            p.sleigh = true;
            p.sleighDir = rnd() < 0.5f ? 1 : -1;
            p.sleighX = p.sleighDir > 0 ? -10.0f : p.w + 10.0f;
            p.sleighRow = 1 + (int)(rnd() * 4);
        }
    }

    int row(const Player &p, float h) const { return (int)std::lround((H - 1) - (h - p.cam)); }
    // Draws in the lane, wrapping round its sides.
    void plot(const Player &p, int x, int y, const RGB &c, float k) {
        x %= p.w;
        if (x < 0) x += p.w;
        canvas.add(p.x0 + x, y, c, k);
    }
    void put(const Player &p, int x, int y, const RGB &c, float k) {
        x %= p.w;
        if (x < 0) x += p.w;
        canvas.set(p.x0 + x, y, c, k);
    }

    // The sky at a height: the faintest blue dusk over the snow (about 1%,
    // 3 of 255 at most) fading to black by 500 rows. A brighter sky washed
    // out on the wall and hid what's in it.
    RGB skyAt(float h, float &k) const {
        k = 0.012f * (1.0f - ArcadeCanvas::clamp01(h / 500.0f));
        return {0.3f, 0.5f, 1.0f};
    }

    void drawSky(Player &p) {
        const float lift = heightBonus;   // the cheat codes show off where they took you
        for (int y = 0; y < H; ++y) {
            const float h = p.cam + (H - 1 - y);
            if (h < 0.5f) {
                continue;   // the snow
            }
            float k;
            const RGB c = skyAt(h + lift, k);
            if (k <= 0) {
                continue;
            }
            for (int x = 0; x < p.w; ++x) {
                plot(p, x, y, c, k);
            }
        }
        const float he = p.cam + lift;
        // Stars come out as it gets dark, and scroll slowly, far away.
        const float starK = ArcadeCanvas::clamp01((he - 80.0f) / 400.0f);
        if (starK > 0) {
            for (auto &s : p.stars) {
                const float sy = std::fmod(s.y + p.cam * 0.25f, (float)H);
                const float tw = 0.5f + 0.5f * std::sin(clock * 2.0f + s.phase);
                plot(p, s.x, (int)sy, {0.9f, 0.9f, 1.0f}, (0.03f + 0.07f * tw) * starK);
            }
        }
        // The aurora, at the very top.
        const float auroraK = ArcadeCanvas::clamp01((he - 900.0f) / 250.0f);
        if (auroraK > 0) {
            for (int x = 0; x < p.w; ++x) {
                const float wave = 2.5f + 1.5f * std::sin(x * 0.25f + clock * 0.6f) + std::sin(x * 0.11f - clock * 0.4f);
                for (int y = 0; y < 7; ++y) {
                    const float f = std::max(0.0f, 1.0f - std::fabs(y - wave) / 2.0f);
                    plot(p, x, y, ArcadeCanvas::mix({0.1f, 1.0f, 0.4f}, {0.2f, 0.6f, 1.0f}, y / 7.0f), 0.035f * f * auroraK);
                }
            }
        }
        // The moon passes on the way up, slowly, being far off.
        const float my = -6.0f + (he - 250.0f) * 0.05f;
        if (my > -5 && my < H + 5) {
            const float mx = p.w * 0.72f, r = std::clamp(p.w / 14.0f, 1.8f, 3.0f);
            for (int y = (int)(my - r - 1); y <= (int)(my + r + 1); ++y) {
                for (int x = (int)(mx - r - 1); x <= (int)(mx + r + 1); ++x) {
                    const float d = std::hypot(x + 0.5f - mx, y + 0.5f - my);
                    const float in = ArcadeCanvas::clamp01(r - d + 0.5f);
                    const float spot = std::hypot(x + 0.5f - (mx - r * 0.3f), y + 0.5f - (my - r * 0.25f)) < r * 0.35f ? 0.7f : 1.0f;
                    if (y >= 0 && y < H) {
                        plot(p, x, y, {1.0f, 0.93f, 0.7f}, 0.16f * in * spot);
                    }
                }
            }
        }
        if (p.sleigh) {
            drawSleigh(p, (int)std::lround(p.sleighX), p.sleighRow, p.sleighDir);
        }
        const float snowK = 1.0f - ArcadeCanvas::clamp01((he - 500.0f) / 300.0f);
        if (snowK > 0) {
            for (auto &f : p.flakes) {
                plot(p, (int)f.x, (int)f.y, {1, 1, 1}, 0.09f * snowK);
            }
        }
    }

    // Santa far off: a dim little sleigh and two reindeer, the lead one's
    // nose blinking. Not wrapped - it flies off the side.
    void drawSleigh(const Player &p, int sx, int sy, int dir) {
        static const char *ROWS[2] = {"R....A.A", "RRHHBHBN"};
        for (int y = 0; y < 2; ++y) {
            for (int i = 0; i < 8; ++i) {
                const int x = dir > 0 ? sx + i : sx + 7 - i;
                if (x < 0 || x >= p.w) {
                    continue;
                }
                RGB c;
                float k = 0.3f;
                switch (ROWS[y][i]) {
                case 'R': c = {1, 0.1f, 0.05f}; break;
                case 'A': c = {0.8f, 0.6f, 0.35f}; k = 0.2f; break;
                case 'H': c = {0.7f, 0.5f, 0.1f}; k = 0.15f; break;
                case 'B': c = {0.6f, 0.3f, 0.1f}; break;
                case 'N': c = {1, 0.1f, 0.05f}; k = std::fmod(clock, 0.6f) < 0.3f ? 0.5f : 0.15f; break;
                default: continue;
                }
                canvas.add(p.x0 + x, sy + y, c, k);
            }
        }
    }

    // The snow the elf starts on, with a little town on it.
    void drawGround(Player &p) {
        const int top = row(p, 0);
        if (top - 6 >= H) {
            return;
        }
        for (int y = std::max(0, top); y < H; ++y) {
            for (int x = 0; x < p.w; ++x) {
                plot(p, x, y, SNOW, y == top ? 0.35f : 0.12f);
            }
        }
        // Trees and houses along the snow, kept clear of the elf in the middle.
        const RGB TREE = {0.05f, 0.6f, 0.15f}, WALL = {0.6f, 0.35f, 0.2f}, ROOF = {0.75f, 0.1f, 0.08f},
                  WINDOW = {1.0f, 0.8f, 0.3f};
        auto tree = [&](int x) {
            plot(p, x, top - 3, TREE, 0.35f);
            for (int dx = -1; dx <= 1; ++dx) plot(p, x + dx, top - 2, TREE, 0.35f);
            for (int dx = -1; dx <= 1; ++dx) plot(p, x + dx, top - 1, TREE, 0.3f);
            plot(p, x, top - 4, GOLD, std::fmod(clock, 1.0f) < 0.5f ? 0.5f : 0.25f);   // its star
        };
        auto house = [&](int x) {
            for (int dx = 0; dx < 5; ++dx) plot(p, x + dx, top - 3, ROOF, 0.35f);
            for (int dx = 1; dx < 4; ++dx) plot(p, x + dx, top - 4, ROOF, 0.35f);
            for (int dx = 0; dx < 5; ++dx) {
                for (int y = top - 2; y < top; ++y) {
                    const bool window = (dx == 1 || dx == 3) && y == top - 2;
                    plot(p, x + dx, y, window ? WINDOW : WALL, window ? 0.5f : 0.25f);
                }
            }
        };
        const int w = p.w;
        house(1);
        tree(w / 4 + 1);
        if (w >= 40) {
            house(w * 3 / 4 - 2);
            tree(w / 2 - 9);
            tree(w / 2 + 9);
        }
        tree(w - 3);
    }

    void drawThing(Player &p, const Thing &t) {
        const int x = (int)std::lround(t.x), y = row(p, t.y);
        if (y < -3 || y > H + 1) {
            return;
        }
        switch (t.kind) {
        case COIN: {
            // A glint runs over the coins now and then.
            const bool glint = std::fmod(clock * 0.7f + x * 0.05f + t.y * 0.03f, 1.0f) < 0.08f;
            plot(p, x, y, glint ? ArcadeCanvas::mix(GOLD, {1, 1, 1}, 0.6f) : GOLD, glint ? 1.0f : 0.8f);
            break;
        }
        case BLAST: {
            // An up arrow, its tip pulsing white.
            const float pulse = 0.5f + 0.5f * std::sin(clock * 10.0f + t.y);
            plot(p, x, y - 1, ArcadeCanvas::mix(BLAST_COL, {1, 1, 1}, pulse), 1.0f);
            for (int dx = -1; dx <= 1; ++dx) plot(p, x + dx, y, BLAST_COL, dx ? 0.7f : 1.0f);
            break;
        }
        case STAR: {
            const RGB c = hue(clock * 0.8f);
            plot(p, x, y - 1, {1, 1, 1}, 1.0f);
            plot(p, x, y - 2, c, 0.9f);
            plot(p, x, y, c, 0.9f);
            plot(p, x - 1, y - 1, c, 0.9f);
            plot(p, x + 1, y - 1, c, 0.9f);
            break;
        }
        case CLOUD: {
            // A grumpy grey cloud with red eyes.
            const bool flash = t.hitT > 0 && std::fmod(t.hitT, 0.1f) < 0.05f;
            const RGB c = flash ? RGB{1, 1, 1} : CLOUD_COL;
            for (int dx = -2; dx <= 2; ++dx) plot(p, x + dx, y, c, 0.45f);
            plot(p, x - 1, y - 1, EYE_COL, 0.8f);
            plot(p, x, y - 1, c, 0.45f);
            plot(p, x + 1, y - 1, EYE_COL, 0.8f);
            break;
        }
        case BAT: {
            const bool up = std::fmod(clock * 6.0f + t.phase, 2.0f) < 1.0f;
            const RGB c = t.hitT > 0 ? RGB{1, 1, 1} : BAT_COL;
            plot(p, x, y - 1, c, 0.9f);                     // the body
            plot(p, x - 1, up ? y - 2 : y, c, 0.75f);       // the wings
            plot(p, x + 1, up ? y - 2 : y, c, 0.75f);
            break;
        }
        case METEOR: {
            // A fireball, its tail streaming up behind it.
            plot(p, x, y, {1, 0.9f, 0.6f}, 1.0f);
            for (int i = 1; i <= 3; ++i) {
                plot(p, (int)std::lround(t.x - t.vx * 0.12f * i), y - i, METEOR_COL, 0.7f / i);
            }
            break;
        }
        }
    }

    void drawElf(Player &p) {
        if (p.out) {
            return;
        }
        // Blinks while dazed or just rescued; in a star it runs through the rainbow.
        if ((p.dazedT > 0 && std::fmod(p.dazedT, 0.2f) < 0.08f) || (p.safeT > 0 && std::fmod(p.safeT, 0.24f) < 0.1f)) {
            return;
        }
        const int x = (int)std::floor(p.x), y = row(p, p.y);
        RGB suit = p.suit, hat = p.hat;
        if (p.starT > 0 && (p.starT > 1.0f || std::fmod(p.starT, 0.2f) < 0.12f)) {
            suit = hue(clock * 2.0f);
            hat = GOLD;
        }
        // The rocket pack's flame: big after a boost, small while climbing,
        // a flicker on the snow as it gets ready.
        const float flick = 0.6f + 0.4f * rnd();
        if (p.flameT > 0) {
            put(p, x, y + 1, ArcadeCanvas::mix(FLAME, {1, 1, 0.5f}, 0.5f), flick);
            plot(p, x, y + 2, FLAME, 0.6f * flick);
        } else if (p.launched && p.vy > 0) {
            put(p, x, y + 1, FLAME, 0.7f * flick);
        } else if (!p.launched && (phase == Phase::Countdown || phaseT > GROUND_WAIT - 1.0f)) {
            put(p, x, y + 1, FLAME, 0.4f * flick);
        }
        put(p, x, y, suit, 1.0f);
        put(p, x, y - 1, hat, 1.0f);
    }

    void drawLane(Player &p) {
        drawSky(p);
        drawGround(p);
        for (const auto &t : p.things) {
            drawThing(p, t);
        }
        for (const auto &s : p.sparks) {
            plot(p, (int)std::lround(s.x), row(p, s.y), s.col, 1.0f - s.age / s.life);
        }
        drawElf(p);
    }

    // ---------------------------------------------------------------- drawing
    void text(const std::string &s, int x, int y, int scl, const RGB &c, float k) {
        for (size_t i = 0; i < s.size(); ++i) {
            const int lx = x + (int)i * 4 * scl, w = arcadeGlyphWidth(s[i], scl);
            for (int py = 0; py < 5 * scl; ++py) {
                for (int px = 0; px < w; ++px) {
                    if (arcadeGlyphScaled(s[i], px, py, scl)) {
                        canvas.set(lx + px, y + py, c, k);
                    }
                }
            }
        }
    }
    static int textW(const std::string &s, int scl = 1) { return s.empty() ? 0 : (int)s.size() * 4 * scl - scl; }
    void centred(const Player &p, const std::string &s, int y, int scl, const RGB &c, float k) {
        text(s, p.x0 + (p.w - textW(s, scl)) / 2, y, scl, c, k);
    }

    void render() {
        canvas.clear();
        for (int i = 0; i < lanes(); ++i) {
            drawLane(pl[i]);
        }
        if (split) {
            for (int y = 0; y < H; ++y) {
                for (int x = pl[0].w; x < pl[1].x0; ++x) {
                    canvas.add(x, y, {0.4f, 0.4f, 0.55f}, 0.15f);   // the line between the halves
                }
            }
        }
        if (!(over && overT > 1.0f)) {
            drawHud();
        }
        canvas.writeTo(model);
        if (over && overT > 1.0f) {
            textBox(overLines, 255, 255, 255);
        }
    }

    // The score small and dim in the top corner (each side's outer one in a
    // race), spare lives as red dots in the other, and the moment's words.
    void drawHud() {
        for (int i = 0; i < lanes(); ++i) {
            Player &p = pl[i];
            const std::string s = std::to_string(p.score());
            const bool right = !split || i == 1;
            if (p.out) {
                // Out of a race that goes on: that side's score, where the climb was.
                if (split) {
                    centred(p, s, H / 2 - 2, 1, ArcadeCanvas::mix(p.suit, {1, 1, 1}, 0.5f), 0.7f);
                }
                continue;
            }
            if (phase == Phase::Play) {
                text(s, right ? p.x0 + p.w - textW(s) : p.x0, 0, 1, HUD, 1.0f);
            }
            for (int k = 0; k < p.lives - 1; ++k) {
                const int x = right ? p.x0 + 2 * k : p.x0 + p.w - 1 - 2 * k;
                canvas.set(x, 0, {1, 0.15f, 0.1f}, 0.4f);
            }
            if (p.wordT > 0) {
                centred(p, p.word, 3, 1, GOLD, std::min(1.0f, p.wordT * 2.0f) * 0.8f);
            }
        }
        if (phase == Phase::Ground) {
            centred(pl[0], "SKY JUMP", 1, 1, GOLD, 0.6f);
            // What launches: Up on the D-pad, either button on A/B (which
            // has launched already, if it started the game).
            if (std::fmod(clock, 0.8f) < 0.5f) {
                centred(pl[0], owner == 0 ? "UP!" : "UP OR A", 8, 1, {1, 1, 1}, 0.7f);
            }
        } else if (phase == Phase::Countdown) {
            const std::string n = std::to_string(3 - std::min(2, (int)(phaseT / COUNT_STEP)));
            const int scl = H >= 20 ? 2 : 1;
            for (int i = 0; i < 2; ++i) {
                centred(pl[i], n, H / 2 - 5 * scl / 2 - 3, scl, {1, 1, 1}, 0.9f);
            }
        }
    }

    // Settings.
    int startLives = 1;
    float speed = 1.0f;
    int endShow = 1, goodGame = 600;

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int W = 0, H = 0;
    float clock = 0;

    Phase phase = Phase::Ground;
    float phaseT = 0, playT = 0;
    int lastCount = -1;
    bool split = false;
    int owner = -1;           // alone: the control that plays (0 D-pad, 1 A/B), -1 not yet known
    float heightBonus = 0;    // the cheat codes' head start on the difficulty
    Player pl[2];

    bool over = false;
    float overT = 0;
    std::vector<std::string> overLines;
    ArcadeShowPlayer show;
    ShowConfig showConfig;
    std::unique_ptr<ShowStory> showStory;
    bool showDue = false;

    std::atomic<uint32_t> held{0}, presses{0};
    bool finished = false;
};


const std::string &FPPSkyJump::getName() {
    static const std::string name = "Sky Jump";
    return name;
}

void FPPSkyJump::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    SkyJumpEffect *effect = dynamic_cast<SkyJumpEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new SkyJumpEffect(m, findIntOption("Lives", 1), findIntOption("Speed", 100), endShowOption(),
                                   goodGameOption(600));
        m->setRunningEffect(effect, 25);
    }
    // The first press is passed on too: it picks the control that plays,
    // and A or B launches straight away. Start only starts.
    if (!starting || button.compare(0, 5, "Start") != 0) {
        effect->button(button);
    }
}
