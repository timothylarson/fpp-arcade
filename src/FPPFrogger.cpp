#include <fpp-pch.h>
#include "FPPFrogger.h"
#include "FroggerShow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <list>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"
#include "log.h"

// ------------------------------------------------------------
// FPP Frogger (grid-based; settings-aware)
// ------------------------------------------------------------
// Settings (from plugin_setup.php):
//   - Pixel Scaling: 1..20  -> each logical cell renders as SxS pixels
//   - Lanes:        1..20   -> number of river lanes and road lanes
//   - River Speed:  1..10   -> river band speed (logs/turtles)
//   - Road  Speed:  1..10   -> road band speed (cars)
//   - Speed Variability (%): 0..100 -> per-lane baseline jitter
//
// Notes:
//   * Logical grid size = (panelW / scale) x (panelH / scale)
//   * Lanes are single-row bands (river lanes, safe median, road lanes)
//   * “Turtles” are a river mover variant (visual only), width fixed to 2
//   * HUD (score/lives) draws in 1px raw physical pixels (unscaled)
//   * All movers in the same lane share identical speed (no in-lane collisions)
// ------------------------------------------------------------

// --- speed mapping helper (1..10 -> ~0.6x..1.8x) ---
static inline float mapSpeedInt(int s) {
    s = std::max(1, std::min(10, s));
    return 0.6f + (s - 1) * (1.2f / 9.0f);
}

struct FGEntity {
    int gx = 0, gy = 0;  // grid position
    int gw = 1, gh = 1;  // size in grid cells
};

struct FGLaneMover {
    float gx = 0.0f;       // head X (float for smooth wrap)
    int   gy = 0;          // lane row
    int   gw = 2;          // width (cells)
    int   dir = 1;         // +1 right, -1 left
    float speed = 0.5f;    // cells/sec
    bool  isLog = false;   // road=false, river=true
    bool  isTurtle = false;// visual subtype of log (fixed width=2)
    int   r=255, g=255, b=255;
};

class FroggerEffect : public FPPArcadeGameEffect {
public:
    struct Options {
        int   scale        = 1;
        int   lanes        = 5;
        int   playfieldW   = 0;    // 0 = fill the panel
        int   playfieldH   = 0;
        float riverMult    = 1.0f; // river (logs/turtles)
        float roadMult     = 1.0f; // road (cars)
        float variability  = 0.20f; // 0..1 lane-to-lane speed spread
        int   endShow      = 1;    // the End Show setting
        int   goodGame     = 4;    // the Good Game setting: the level reached
        int   extraLife    = 3;    // the Extra Life setting: a frog every so many levels
    };

    explicit FroggerEffect(PixelOverlayModel *m, const Options &opts)
        : FPPArcadeGameEffect(m), opt(opts)
    {
        opt.scale = std::clamp(opt.scale, 1, 20);
        opt.lanes = std::clamp(opt.lanes, 1, 20);
        opt.variability = std::clamp(opt.variability, 0.0f, 1.0f);

        rng.seed((unsigned)time(nullptr) ^ (uintptr_t)this);
        configureGrid();
        resetGame(/*newGame=*/true);
    }

    ~FroggerEffect() override = default;

    const std::string &name() const override {
        static std::string NAME = "Frogger";
        return NAME;
    }

    // Main loop
    int32_t update() override {
        if (show.on()) {
            if (show.step(model, consumeElapsedMs(50.0))) {
                return 50;
            }
            resetFrameTimer();   // over: the game ends below, as after GAME OVER
        }
        constexpr double baseFrameMs = 50.0;
        double elapsedMs = consumeElapsedMs(baseFrameMs);
        if (elapsedMs <= 0.0) elapsedMs = baseFrameMs;
        double frameScalar = std::clamp(elapsedMs / baseFrameMs, 0.1, 5.0);

            if (isPaused()) {
                drawPauseMenu();
                model->flushOverlayBuffer();
                return 50;
            }

        if (!gameOn) {
            // A good game (or a new best) gets the end show instead of the
            // GAME OVER screen; when it's over this carries on below as if
            // that screen had been held.
            if (showDue) {
                showDue = false;
                gameOverShown = true;
                show.start(model, FroggerShow::config(score, level, newBest), std::make_unique<FroggerShow::Story>());
                resetFrameTimer();
                return 50;
            }
            // Draw the game-over screen once and HOLD it. This used to draw and
            // then fall straight through to the teardown below on the very next
            // update, so the screen was gone before it could be read - Tetris
            // returns a hold here and Frogger did not.
            if (!gameOverShown) {
                gameOverShown = true;
                model->clearOverlayBuffer();
                drawGameOver();
                model->flushOverlayBuffer();
                return gameOverHoldMs;
            }
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            if (!waitingUntilOutput) {
                waitingUntilOutput = true;
                return -1;
            }
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return 0;
        }

        if (paused) {
            drawFrame(0.25f, /*drawPaused=*/true);
            return 50;
        }
        const int cheat = takeCheat();
        if (cheat == CHEAT_NEXT_LEVEL) {
            level++;
            speedBoost = 1.0f;
            initLevel();
            sfx("frogger", "LevelClear");
        }
        if (cheat == CHEAT_LAST_LEVEL) {
            // Frogger keeps speeding up, so "last" is level
            // 10 (traffic and logs about twice as fast as level 1), with 9
            // spare frogs to try it.
            level = 10;
            lives = std::max(lives, 9);
            speedBoost = 1.0f;
            initLevel();
            sfx("frogger", "LevelClear");
        }

        // An extra frog earned by the last level: once its tune has played.
        lifeBlinkT = std::max(0.0f, lifeBlinkT - (float)(elapsedMs / 1000.0));
        if (lifeDueMs > 0 && (lifeDueMs -= elapsedMs) <= 0) {
            if (lives < 8) {   // spare frogs: nine in all
                ++lives;
                lifeBlinkT = LIFE_BLINK_SECS;
                sfx("frogger", "ExtraLife");
            }
        }

        // Which log is the frog standing on, and where along it, BEFORE anything
        // moves. Deciding this after the movers advance is what made a hop onto a
        // log that the player could see under the frog sometimes drown instantly.
        int rideIdx = -1;
        int rideOffset = 0;
        if (isRiverRow(frog.gy)) {
            rideIdx = logIndexUnder(frog);
            if (rideIdx >= 0) {
                rideOffset = frog.gx - moverDrawX(movers[rideIdx]);
            }
        }

        advanceMovers((float)frameScalar);

        // River: drown unless on a log/turtle (then ride it)
        if (isRiverRow(frog.gy)) {
            if (rideIdx >= 0) {
                // Hold the same offset along the log rather than integrating a
                // carry speed of our own. The old carry could not work: it used
                // laneSpeed*frameScalar while the movers use speed*dt (a
                // different unit entirely), and frog.gx is a whole cell, so
                // round()ing a sub-cell carry threw the remainder away every
                // frame. The log slid out from under the frog a fraction at a
                // time, which is why it always ended up off the left end of a
                // log drifting right.
                frog.gx = moverDrawX(movers[rideIdx]) + rideOffset;
                // Riding up to the edge is fine; only a log that carries the
                // frog past it (off the screen) costs a life.
                if (frog.gx < 0 || frog.gx + frog.gw > gridCols) {
                    loseLife("Splash");   // carried off the end of the row
                }
            } else {
                loseLife("Splash");
            }
        }

        // Road: car collision
        if (isRoadRow(frog.gy) && hitByCar(frog)) {
            loseLife("Squash");
        }

        // Home row: must land on a free slot
        if (frog.gy == homeRow) {
            int slot = nearestHomeSlot(frog.gx);
            if (slot >= 0 && !homeFilled[slot]) {
                homeFilled[slot] = true;
                score += 50;
                resetFrog();
                if (std::all_of(homeFilled.begin(), homeFilled.end(), [](bool f){ return f; })) {
                    if (earnsExtraLife(opt.extraLife, level)) {
                        lifeDueMs = 1500;
                    }
                    level++;
                    score += 100;
                    initLevel();
                    sfx("frogger", "LevelClear");
                } else {
                    sfx("frogger", "Home");
                }
            } else {
                loseLife("Splash"); // missed the slot -> water
            }
        }

        drawFrame(1.0f);
        return 50;
    }

    // Button handling
    void button(const std::string &button) {
        if (show.on()) {
            if (button.find("Pressed") != std::string::npos) {
                show.skip();
            }
            return;
        }
        const bool isPress = (button.find("Pressed") != std::string::npos) || (button=="Fire");
        if (!isPress) return;

        const bool left   = (button=="Left - Pressed");
        const bool right  = (button=="Right - Pressed");
        const bool up     = (button=="Up - Pressed");
        const bool down   = (button=="Down - Pressed");
        // A and B change the traffic speed; Fire still hops forward.
        const bool aPress =
            (button=="Fire - Pressed")     || (button=="Fire") ||
            (button=="Fire Button - Pressed");
        const bool faster = (button=="A Button - Pressed");
        const bool slower = (button=="B Button - Pressed");
        const bool startPress = (button=="Start - Pressed") || (button=="Start");

        // allow base pause menu handling while game is active
        if (gameOn) {
            FPPArcadeGameEffect::button(button);
            if (isPaused()) return;
            // Once the game is running the base owns Start: it toggles pause and
            // drives the pause menu. Without returning here the local fallback
            // below runs too and flips `paused` straight back on, so the base
            // could pause but never resume - every later button was swallowed by
            // the `if (paused) return` guard.
            if (startPress) return;
        }

        if (startPress) {
            if (!gameOn) {
                resetGame(/*newGame=*/true);
            } else {
                // if we're here, base didn't handle Start (e.g., base not used), toggle local pause
                paused = !paused;
            }
            return;
        }

        if (paused) return;

        // A speeds everything on the road and river up a step, B slows it
        // back down - never below the level's own speed.
        // At either end of the range the press does nothing, and sounds it.
        if (faster || slower) {
            const float was = speedBoost;
            speedBoost = faster ? std::min(MAX_BOOST, speedBoost + BOOST_STEP)
                                : std::max(1.0f, speedBoost - BOOST_STEP);
            sfx("frogger", speedBoost == was ? "Bump" : (faster ? "SpeedUp" : "SpeedDown"));
            return;
        }

        if (aPress || up)       moveFrog(0, -1);
        else if (down)          moveFrog(0, +1);
        else if (left)          moveFrog(-1, 0);
        else if (right)         moveFrog(+1, 0);
    }

private:
    // ------------ helpers ------------
    int fittedLanesForGrid(int rowsLogical) const {
        // rows needed = homes(1) + river(L) + median(1) + road(L) + start(1)
        // => 2*L + 3  <= rowsLogical  =>  L <= (rowsLogical - 3)/2
        int maxL = std::max(1, (rowsLogical - 3) / 2);
        return std::max(1, std::min(opt.lanes, maxL));
    }

    // ------------ Grid/rows ------------
    void configureGrid() {
        // Apply pixel scaling to “logical” grid
        cellW = cellH = std::max(1, opt.scale);

        // The old min bounds of 30x20 could exceed the panel, and pxRaw() then
        // silently clipped whatever fell off the right or bottom - the game was
        // still "playing" in cells nobody could see. Take the size from
        // setPlayfield() instead: 0/0 fills the panel exactly, and a configured
        // Playfield Width/Height is centred on it.
        // Left to fill the panel, the field is only as tall as its lanes
        // need - the score row, the homes, the river, the median, the road,
        // the start and the lives row - and centred: five lanes on a 25-row
        // wall at Pixel Scaling 1 used to leave 12 empty rows under the start
        // that the frog could wander into.
        int wantH = opt.playfieldH;
        if (wantH <= 0) {
            const int avail = std::max(1, model->getHeight() / cellW);
            wantH = std::min(avail, 2 * fittedLanesForGrid(avail) + 5);
        }
        setPlayfield(opt.playfieldW, wantH, cellW);
        cellW = cellH = scale;
        gridCols = pfW;
        gridRows = pfH;

        // Fit lane count to available rows so start/home always exist
        int L = fittedLanesForGrid(gridRows);

        // rows: [homes][river x L][median][road x L][start]
        homeRow       = 1;
        riverTop      = 2;
        riverBottom   = riverTop + L - 1;
        safeMedianRow = riverBottom + 1;
        roadTop       = safeMedianRow + 1;
        roadBottom    = roadTop + L - 1;

        // keep everything in range and place start just above bottom
        roadBottom = std::min(roadBottom, gridRows - 3);
        startRow   = std::min(roadBottom + 1, gridRows - 2);

        // home slots
        const int slots = 5;
        homeSlots = slots;
        homeX.clear();
        homeFilled.assign(slots, false);
        for (int i=0; i<slots; ++i) {
            int cx = (i+1) * gridCols / (slots+1);
            homeX.push_back(cx);
        }

        laneMult.assign(gridRows, 1.0f);
    }

    // ------------ Game state ------------
    void resetGame(bool /*newGame*/) {
        speedBoost = 1.0f;
        level  = 1;
        score  = 0;
        lives  = 3;
        lifeDueMs = 0;
        lifeBlinkT = 0;
        paused = false;
        gameOn = true;
        waitingUntilOutput = false;
        gameOverShown = false;
        showDue = false;
        initLevel();
        sfxStop("frogger");   // cuts off the title's GameSelected, or the last game's sounds
        sfx("frogger", "Start");
    }

    void initLevel() {
        std::fill(homeFilled.begin(), homeFilled.end(), false);
        movers.clear();
        buildRiver();
        buildRoad();
        reseedLaneMultipliers();
        resetFrog();
        resetFrameTimer();
    }

    void resetFrog() {
        frog = {};
        frog.gw = frog.gh = 1;
        frog.gx = std::clamp(gridCols / 2, 0, gridCols - 1);
        frog.gy = std::clamp(startRow,      0, gridRows - 1);
    }

    void restart() override {
        // Use the existing reset helper to restart the game cleanly
        resetGame(/*newGame=*/true);
        resetFrameTimer();
    }

    // sound: "Splash" in the water, "Squash" under a car.
    void loseLife(const char *sound) {
        // Already over: a second hazard in the same frame mustn't end the game
        // (and record the best score) twice.
        if (!gameOn) return;
        if (invulnTimerMs > 0.0) return; // grace
        lives--;
        LogInfo(VB_PLUGIN, "[Frogger] Life lost. Lives left=%d", lives);
        if (lives < 0) {
            gameOn = false;
            sfxStop("frogger");
            // Decided here so the show's music starts the moment the last
            // frog is lost, just as GAME OVER's does; update() starts the show.
            int best = 0;
            newBest = arcadeRecordBest(model->getName(), "Frogger", score, &best);
            showDue = arcadeWantShow(opt.endShow, level >= opt.goodGame, newBest);
            showDue ? sfxEndShow("frogger") : sfx("frogger", "GameOver");
            return;
        }
        sfx("frogger", sound);
        score = std::max(0, score - 25);
        resetFrog();
        invulnTimerMs = 800.0; // ms
    }

    // ------------ Lanes helpers ------------
    bool isRiverRow(int gy) const { return gy >= riverTop && gy <= riverBottom; }
    bool isRoadRow (int gy) const { return gy >= roadTop  && gy <= roadBottom; }

    int laneDir(int gy) const { return ((gy % 2) == 0) ? +1 : -1; }

    float laneSpeed(int gy) const {
        const float bandBase = isRiverRow(gy) ? 0.45f : 0.60f;
        const float levelAdj = 0.06f * (level - 1);
        const float bandMult = isRiverRow(gy) ? opt.riverMult : opt.roadMult;
        const float laneJit  = (gy >= 0 && gy < (int)laneMult.size()) ? laneMult[gy] : 1.0f;
        return (bandBase + levelAdj) * bandMult * laneJit;
    }

    void reseedLaneMultipliers() {
        const float v = std::clamp(opt.variability, 0.0f, 1.0f);
        std::uniform_real_distribution<float> d(1.0f - v, 1.0f + v);
        for (int gy = riverTop; gy <= riverBottom; ++gy) {
            laneMult[gy] = std::clamp(d(rng), 0.2f, 3.0f);
        }
        for (int gy = roadTop; gy <= roadBottom; ++gy) {
            laneMult[gy] = std::clamp(d(rng), 0.2f, 3.0f);
        }
    }

    // ------------ Randomized lane population ------------
    void buildRiver() {
        std::uniform_int_distribution<int> wdist(4, 8);            // log length
        std::uniform_int_distribution<int> spandist(8, 14);        // spacing
        std::bernoulli_distribution turtleLane(0.45);              // lane may prefer turtles
        std::bernoulli_distribution turtleMix(0.40);               // per-entity chance

        for (int gy = riverTop; gy <= riverBottom; ++gy) {
            const int   dir    = laneDir(gy);
            const float laneS  = laneSpeed(gy); // lane-uniform speed
            const int   span   = spandist(rng);
            const int   count  = std::max(2, gridCols / span);
            const bool  preferTurtles = turtleLane(rng);

            int advance = 0;
            for (int i=0; i<count; ++i) {
                FGLaneMover m;
                m.gy    = gy;
                m.dir   = dir;
                m.speed = std::max(0.15f, laneS); // same for entire lane
                m.isLog = true;

                // turtles fixed to width=2; logs keep random length
                m.isTurtle = preferTurtles ? true : turtleMix(rng);
                if (m.isTurtle) {
                    m.gw = 2;
                    m.r = 60;  m.g = 200; m.b = 200;   // bluish-green
                } else {
                    m.gw = wdist(rng);
                    m.r = 100; m.g = 200; m.b = 100;   // greenish
                }

                // staggered start
                int startCol = (advance % gridCols);
                m.gx = (dir > 0) ? (float)startCol : (float)(gridCols - startCol);
                advance += span;

                movers.push_back(m);
            }
        }
    }

    void buildRoad() {
        std::uniform_int_distribution<int> wdist(2, 5);           // car length
        std::uniform_int_distribution<int> spandist(10, 16);      // spacing
        std::uniform_int_distribution<int> colorPick(0, 4);

        auto pickCarColor = [&](int &r, int &g, int &b) {
            switch (colorPick(rng)) {
                case 0: r=220; g=60;  b=60;  break; // red
                case 1: r=60;  g=160; b=240; break; // blue
                case 2: r=220; g=220; b=60;  break; // yellow
                case 3: r=180; g=80;  b=220; break; // purple
                default:r=120; g=220; b=90;  break; // lime
            }
        };

        for (int gy = roadTop; gy <= roadBottom; ++gy) {
            const int   dir    = laneDir(gy);
            const float laneS  = laneSpeed(gy); // lane-uniform speed
            const int   span   = spandist(rng);
            const int   count  = std::max(2, gridCols / span);

            int advance = 0;
            for (int i=0; i<count; ++i) {
                FGLaneMover m;
                m.gy    = gy;
                m.gw    = wdist(rng);
                m.dir   = dir;
                m.speed = std::max(0.20f, laneS); // same for entire lane
                m.isLog = false;
                pickCarColor(m.r, m.g, m.b);

                int startCol = (advance % gridCols);
                m.gx = (dir > 0) ? (float)startCol : (float)(gridCols - startCol);
                advance += span;

                movers.push_back(m);
            }
        }
    }

    void advanceMovers(float frameScalar) {
        double elapsedMs = frameScalar * 50.0;
        if (invulnTimerMs > 0.0) {
            invulnTimerMs -= elapsedMs;
            if (invulnTimerMs < 0.0) invulnTimerMs = 0.0;
        }

        const float dt = (float)(elapsedMs / 1000.0); // seconds
        // A mover lives on [-gw, gridCols): from just off the left edge to
        // just off the right. Leaving either side puts it just off the other,
        // so it slides back in a cell at a time. The right-hand test used to
        // be gx > gridCols + gw, which wrapped it to about 0 - fully on screen
        // - so everything moving right popped in whole at the left edge.
        for (auto &m : movers) {
            m.gx += m.dir * m.speed * speedBoost * dt;
            const float wrap = (float)(gridCols + m.gw);
            if (m.gx < -m.gw)      m.gx += wrap;
            if (m.gx >= gridCols)  m.gx -= wrap;
        }
    }

    // collisions
    static bool rectOverlap(int ax,int ay,int aw,int ah,
                            int bx,int by,int bw,int bh) {
        return (ax < bx + bw) && (ax + aw > bx) &&
               (ay < by + bh) && (ay + ah > by);
    }

    // The cell a mover is DRAWN at. Anything that reasons about standing on a
    // log has to agree with the renderer, or the frog drowns on a log the
    // player can plainly see under it.
    static int moverDrawX(const FGLaneMover &m) { return (int)std::floor(m.gx + 0.5f); }

    bool onAnyLog(const FGEntity &f) const {
        return logIndexUnder(f) >= 0;
    }

    int logIndexUnder(const FGEntity &f) const {
        for (size_t i = 0; i < movers.size(); ++i) {
            const FGLaneMover &m = movers[i];
            if (!m.isLog || m.gy != f.gy) continue;
            if (rectOverlap(f.gx, f.gy, f.gw, f.gh, moverDrawX(m), m.gy, m.gw, 1)) {
                return (int)i;
            }
        }
        return -1;
    }

    bool hitByCar(const FGEntity &f) const {
        for (const auto &m : movers) {
            if (m.isLog || m.gy != f.gy) continue;
            int mx = (int)std::floor(m.gx + 0.5f);
            if (rectOverlap(f.gx, f.gy, f.gw, f.gh, mx, m.gy, m.gw, 1))
                return true;
        }
        return false;
    }

    void moveFrog(int dx, int dy) {
        const int oldX = frog.gx, oldY = frog.gy;
        frog.gx = std::clamp(frog.gx + dx, 0, gridCols - 1);
        frog.gy = std::clamp(frog.gy + dy, homeRow, startRow);   // never below the start
        if (dy < 0) score += 1; // reward forward hops
        sfx("frogger", (frog.gx == oldX && frog.gy == oldY) ? "Bump" : "Hop");
    }

    // homes
    int nearestHomeSlot(int gx) const {
        int best=-1, bestDist=9999;
        for (int i=0; i<(int)homeX.size(); ++i) {
            int d = std::abs(gx - homeX[i]);
            if (d < bestDist && d <= 1) { best = i; bestDist = d; }
        }
        return best;
    }

    // -------- Rendering with pixel scaling --------
    inline void pxRaw(int x,int y,int r,int g,int b) {
        if (x<0||y<0||x>=model->getWidth()||y>=model->getHeight()) return;
        model->setOverlayPixelValue(x,y,r,g,b);
    }

    inline void fillCell(int gx,int gy,int r,int g,int b) {
        // Only cells on the playfield: a mover part way on shows just the part
        // that has arrived, rather than spilling into the margin beside it.
        if (gx < 0 || gx >= gridCols || gy < 0 || gy >= gridRows) return;
        // draw an opt.scale x opt.scale block, offset into the centred playfield
        const int x0 = gx * cellW + offsetX;
        const int y0 = gy * cellH + offsetY;
        for (int yy=0; yy<cellH; ++yy) {
            for (int xx=0; xx<cellW; ++xx) {
                pxRaw(x0 + xx, y0 + yy, r, g, b);
            }
        }
    }

    void drawRectGrid(int gx,int gy,int gw,int gh,int r,int g,int b,float bright=1.0f) {
        const int rr = std::clamp((int)(r*bright),0,255);
        const int gg = std::clamp((int)(g*bright),0,255);
        const int bb = std::clamp((int)(b*bright),0,255);
        for (int yy=0; yy<gh; ++yy)
            for (int xx=0; xx<gw; ++xx)
                fillCell(gx + xx, gy + yy, rr, gg, bb);
    }

    // A mover drawn LED by LED within its cells, facing the way it goes:
    // shaped, shaded vehicles, wooden logs and turtles, where they used to be
    // solid bars. Where a cell is one LED tall (Pixel Scaling 1) there's no
    // room for a shape, so it's shading along the length instead.
    void drawMover(const FGLaneMover &m, float bright) {
        const int mx = (int)std::floor(m.gx + 0.5f);
        const int W = m.gw * cellW, H = cellH;
        const int x0 = mx * cellW + offsetX, y0 = m.gy * cellH + offsetY;
        const int clipL = offsetX, clipR = offsetX + gridCols * cellW;   // only on the playfield
        struct C {
            float r, g, b;
        };
        auto scaled = [](const C &c, float k) { return C{c.r * k, c.g * k, c.b * k}; };
        auto blend = [](const C &a, const C &b, float t) { return C{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; };
        // i counts from the back of the mover to its front; row 0 is its top.
        auto put = [&](int i, int row, const C &c) {
            const int x = m.dir > 0 ? x0 + i : x0 + W - 1 - i;
            if (x < clipL || x >= clipR || i < 0 || i >= W) {
                return;
            }
            pxRaw(x, y0 + row, std::clamp((int)(c.r * bright), 0, 255), std::clamp((int)(c.g * bright), 0, 255),
                  std::clamp((int)(c.b * bright), 0, 255));
        };
        const int topRows = H >= 2 ? H / 2 : 0;   // rows above the body line; none at one LED tall

        if (m.isLog && m.isTurtle) {
            // Turtles, one to a cell: a green shell, lighter along its back,
            // and a head at the front.
            const C SHELL = {40, 150, 55}, BACK = {100, 205, 90}, EDGE = {20, 85, 30}, HEAD = {170, 210, 90};
            for (int k = 0; k < m.gw; ++k) {
                for (int j = 0; j < cellW; ++j) {
                    const int i = k * cellW + j;
                    const bool front = j == cellW - 1 && cellW > 1;
                    for (int row = 0; row < H; ++row) {
                        if (H == 1) {
                            put(i, row, front ? HEAD : (k & 1 ? EDGE : SHELL));
                        } else if (row < topRows) {
                            put(i, row, front ? EDGE : (j == (cellW - 1) / 2 ? BACK : SHELL));
                        } else {
                            put(i, row, front ? HEAD : EDGE);
                        }
                    }
                }
            }
            return;
        }
        if (m.isLog) {
            // A log: brown wood, paler cut ends, lit along the top and in
            // shadow underneath, with a fleck of grain here and there.
            const C WOOD = {140, 72, 22}, TOP = {175, 100, 42}, UNDER = {95, 45, 12}, END = {215, 155, 95}, GRAIN = {110, 55, 16};
            for (int i = 0; i < W; ++i) {
                const bool end = i == 0 || i == W - 1;
                for (int row = 0; row < H; ++row) {
                    if (end) {
                        put(i, row, END);
                    } else if (H == 1) {
                        put(i, row, (i * 5) % 7 == 0 ? GRAIN : WOOD);
                    } else if (row < topRows) {
                        put(i, row, (i * 5) % 7 == 0 ? GRAIN : TOP);
                    } else {
                        put(i, row, row == H - 1 && H > 2 ? UNDER : WOOD);
                    }
                }
            }
            return;
        }

        // Vehicles, by length: a car, a van, a bus, a truck. Body along the
        // bottom with a headlight at the front and a tail light behind; the
        // cab, windows or trailer above.
        const C body = {(float)m.r, (float)m.g, (float)m.b};
        const C dark = scaled(body, 0.55f), light = blend(body, {255, 255, 255}, 0.3f);
        const C GLASS = {110, 150, 200}, HEADLIGHT = {255, 240, 190}, TAIL = {170, 20, 10};
        const int kind = m.gw <= 2 ? 0 : (m.gw == 3 ? 1 : (m.gw == 4 ? 2 : 3));
        const int cabLen = std::max(2, W / 4);   // a truck's cab, at the front
        for (int i = 0; i < W; ++i) {
            const bool front = i == W - 1, back = i == 0;
            for (int row = 0; row < H; ++row) {
                const bool top = row < topRows;
                if (H == 1) {
                    // One LED tall: the outline as shading along the length.
                    if (front) {
                        put(i, row, HEADLIGHT);
                    } else if (back) {
                        put(i, row, TAIL);
                    } else if (kind == 3) {
                        put(i, row, i >= W - cabLen ? body : (i == W - cabLen - 1 ? dark : light));
                    } else if (kind == 2) {
                        put(i, row, i % 2 ? light : body);
                    } else {
                        put(i, row, body);
                    }
                    continue;
                }
                if (!top) {
                    // The body line: tail light, body, headlight; a truck's
                    // trailer runs dark along its chassis.
                    if (front) {
                        put(i, row, HEADLIGHT);
                    } else if (back) {
                        put(i, row, TAIL);
                    } else if (kind == 3 && i < W - cabLen) {
                        put(i, row, i == W - cabLen - 1 ? dark : blend(light, dark, 0.4f));
                    } else {
                        put(i, row, body);
                    }
                    continue;
                }
                switch (kind) {
                case 0:   // car: a cab over the middle half, windscreen at its front
                case 1: { // van: the same, longer
                    const int c0 = kind == 0 ? W / 4 : 1, c1 = W - 1 - (kind == 0 ? W / 4 : 1);
                    if (i >= c0 && i <= c1) {
                        put(i, row, i == c1 ? GLASS : light);
                    }
                    break;
                }
                case 2:   // bus: full height, a row of windows
                    put(i, row, front ? GLASS : (back ? dark : (i % 2 ? GLASS : body)));
                    break;
                default:  // truck: a cab at the front, a gap, then the trailer
                    if (i >= W - cabLen) {
                        put(i, row, front ? GLASS : body);
                    } else if (i < W - cabLen - 1) {
                        put(i, row, light);
                    }
                    break;
                }
            }
        }
    }

    void drawFrame(float brightness, bool drawPaused=false) {
        model->clearOverlayBuffer();

        // Background stays black - the old filled bands (10-30 per channel)
        // washed out the frog and the movers on a low-res matrix, where every
        // sprite is only a cell or two across. clearOverlayBuffer() has already
        // zeroed the buffer, so the road, median and start rows need no drawing.
        //
        // The river is the one exception. An empty water cell drowns the frog
        // while an empty road cell is safe, so the player has to be able to see
        // which is which even with no mover in the lane. Mark it with a sparse
        // dim-blue dash instead of a filled band: two cells in three stay black,
        // so sprites still read clearly, and alternating the phase per row gives
        // it the broken-up look of water rather than a dotted line.
        for (int gy=0; gy<gridRows; ++gy) {
            if (!isRiverRow(gy)) continue;
            for (int gx=(gy & 1); gx<gridCols; gx+=3)
                drawRectGrid(gx, gy, 1, 1, 0, 0, 70, brightness);
        }

        // homes
        for (int i=0; i<(int)homeX.size(); ++i) {
            int hx = homeX[i];
            int r = homeFilled[i] ? 0   : 80;
            int g = homeFilled[i] ? 200 : 80;
            int b = homeFilled[i] ? 0   : 80;
            drawRectGrid(hx, homeRow, 1, 1, r,g,b, brightness);
        }

        // movers
        for (auto &m : movers) {
            drawMover(m, brightness);
        }

        // frog (blink during invulnerability)
        float frogBright = brightness;
        if (invulnTimerMs>0.0 && ((int)(invulnTimerMs/100) % 2)==0) frogBright *= 0.35f;
        drawRectGrid(frog.gx, frog.gy, 1, 1, 255,255,255, frogBright);

        // --- HUD as 1px raw pixels (unscaled) ---
        // HUD rides the edges of the playfield, not the panel, so it stays with
        // the game when the field is smaller than the matrix.
        const int panelW = pfW * cellW;
        const int panelH = pfH * cellH;

        // score dots across the top row of the playfield
        int dots = std::min((score/10) % panelW, panelW - 1);
        for (int i=0; i<dots; ++i) pxRaw(offsetX + i, offsetY, 200,200,200);

        // lives as dots on the bottom row of the playfield
        int yLives = offsetY + panelH - 1;
        int xLives = 0;
        for (int i=0; i<std::max(0,lives); ++i) {
            if (!(i == lives - 1 && lifeBlinkOff(lifeBlinkT)))   // one just earned blinks
                pxRaw(offsetX + xLives, yLives, 255,255,255);
            xLives += 2; // spacing
            if (xLives >= panelW) break;
        }

        // Traffic speed above the level's own: a yellow dot per A press,
        // from the bottom right.
        const int boostDots = (int)std::lround((speedBoost - 1.0f) / BOOST_STEP);
        for (int i=0; i<boostDots && 2*i < panelW; ++i) {
            pxRaw(offsetX + panelW - 1 - 2*i, yLives, 255,200,40);
        }

        if (drawPaused) {
            outputString("PAUSED", centerTextX("PAUSED", 1), std::max(0, gridRows/2-3), 255,255,255, 1);
        }

        model->flushOverlayBuffer();
    }

    void drawGameOver() {
        drawFrame(0.2f, false);
        // GAME / OVER / final score, centred on the panel over a black box.
        // This used to lay the lines out against gridRows, which counts game
        // cells, not LEDs: with Pixel Scaling 2 it thought a 25-row matrix
        // was 12 rows tall and put GAME on the top row, over the score bar.
        textBox({"GAME", "OVER", std::to_string(score)}, 255, 255, 255);
    }

private:
    // grid
    int gridCols=60, gridRows=25;
    int cellW=1, cellH=1;

    // rows
    int homeRow=1;
    int riverTop=3, riverBottom=10;
    int safeMedianRow=12;
    int roadTop=14, roadBottom=22;
    int startRow=23;

    // homes
    int homeSlots=5;
    std::vector<int>  homeX;
    std::vector<bool> homeFilled;

    // entities
    FGEntity frog;
    std::vector<FGLaneMover> movers;

    // state
    int    level=1;
    // Multiplies every mover's speed: 1 is the level's own, A raises it a
    // step at a time up to MAX_BOOST, B lowers it back. Kept across levels.
    static constexpr float BOOST_STEP = 0.25f;
    static constexpr float MAX_BOOST = 3.0f;
    float  speedBoost = 1.0f;
    int    lives=3;   // spare frogs: the game ends when one is lost with none left
    double lifeDueMs=0;   // an extra frog earned, given when this runs out
    float  lifeBlinkT=0;
    int    score=0;
    bool   paused=false;
    bool   gameOn=true;
    bool   waitingUntilOutput=false;
    bool   gameOverShown=false;
    static constexpr int32_t gameOverHoldMs = 3000;
    bool   showDue=false;    // the game just ended well: update() starts the end show
    bool   newBest=false;
    ArcadeShowPlayer show;
    double invulnTimerMs=0.0;

    // per-lane variability multiplier (baseline)
    std::vector<float> laneMult;

    Options opt;
    std::mt19937 rng;
};

// --------- FPPFrogger methods (wrapper) ---------

const std::string &FPPFrogger::getName() {
    static const std::string name = "Frogger";
    return name;
}

void FPPFrogger::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (!m) return;

    auto *effect = dynamic_cast<FroggerEffect*>(m->getRunningEffect());
    if (!effect) {
        // read settings from the JSON row
        FroggerEffect::Options opts;

        // Pixel Scaling
        try { opts.scale = std::max(1, std::min(20, std::stoi(findOption("Pixel Scaling", "1")))); } catch (...) {}

        // Lanes (river+road count)
        try { opts.lanes = std::max(1, std::min(20, std::stoi(findOption("Lanes", "5")))); } catch (...) {}

        // Playfield size in cells; 0 = fill the panel
        opts.playfieldW = playfieldWidthOption();
        opts.playfieldH = playfieldHeightOption();

        // River & Road speeds (no legacy global)
        try { opts.riverMult = mapSpeedInt(std::stoi(findOption("River Speed", "1"))); } catch (...) {}
        try { opts.roadMult  = mapSpeedInt(std::stoi(findOption("Road Speed",  "1"))); } catch (...) {}

        // Speed Variability (% -> 0..1)
        try {
            int vpc = std::max(0, std::min(100, std::stoi(findOption("Speed Variability", "20"))));
            opts.variability = vpc / 100.0f;
        } catch (...) {}

        opts.endShow = endShowOption();
        opts.goodGame = goodGameOption(4);
        opts.extraLife = extraLifeOption();

        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new FroggerEffect(m, opts);
        m->setRunningEffect(effect, 50);
    } else {
        effect->button(button);
    }
}
