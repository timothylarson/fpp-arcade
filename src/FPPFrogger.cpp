#include <fpp-pch.h>
#include "FPPFrogger.h"

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
        float riverMult    = 1.0f; // river (logs/turtles)
        float roadMult     = 1.0f; // road (cars)
        float variability  = 0.20f; // 0..1 lane-to-lane speed spread
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
        constexpr double baseFrameMs = 50.0;
        double elapsedMs = consumeElapsedMs(baseFrameMs);
        if (elapsedMs <= 0.0) elapsedMs = baseFrameMs;
        double frameScalar = std::clamp(elapsedMs / baseFrameMs, 0.1, 5.0);

        if (!gameOn) {
            model->clearOverlayBuffer();
            drawGameOver();
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

        advanceMovers((float)frameScalar);

        // River: drown unless on a log/turtle (then get carried)
        if (isRiverRow(frog.gy)) {
            if (onAnyLog(frog)) {
                float carry = laneDir(frog.gy) * laneSpeed(frog.gy) * (float)frameScalar;
                carryFrog(carry);
            } else {
                loseLife();
            }
        }

        // Road: car collision
        if (isRoadRow(frog.gy) && hitByCar(frog)) {
            loseLife();
        }

        // Home row: must land on a free slot
        if (frog.gy == homeRow) {
            int slot = nearestHomeSlot(frog.gx);
            if (slot >= 0 && !homeFilled[slot]) {
                homeFilled[slot] = true;
                score += 50;
                resetFrog();
                if (std::all_of(homeFilled.begin(), homeFilled.end(), [](bool f){ return f; })) {
                    level++;
                    score += 100;
                    initLevel();
                }
            } else {
                loseLife(); // missed the slot -> water
            }
        }

        drawFrame(1.0f);
        return 50;
    }

    // Button handling
    void button(const std::string &button) {
        const bool isPress = (button.find("Pressed") != std::string::npos) || (button=="Fire");
        if (!isPress) return;

        const bool left   = (button=="Left - Pressed");
        const bool right  = (button=="Right - Pressed");
        const bool up     = (button=="Up - Pressed");
        const bool down   = (button=="Down - Pressed");
        const bool aPress =
            (button=="A Button - Pressed") || (button=="B Button - Pressed") ||
            (button=="Fire - Pressed")     || (button=="Fire") ||
            (button=="Fire Button - Pressed");
        const bool startPress = (button=="Start - Pressed") || (button=="Start");

        if (startPress) {
            if (!gameOn) {
                resetGame(/*newGame=*/true);
            } else {
                paused = !paused;
            }
            return;
        }

        if (paused) return;

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

        // logical cols/rows (min bounds keep game playable on tiny panels)
        gridCols = std::max(30, model->getWidth()  / cellW);
        gridRows = std::max(20, model->getHeight() / cellH);

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
        level  = 1;
        score  = 0;
        lives  = 3;
        paused = false;
        gameOn = true;
        waitingUntilOutput = false;
        initLevel();
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

    void loseLife() {
        if (invulnTimerMs > 0.0) return; // grace
        lives--;
        LogInfo(VB_PLUGIN, "[Frogger] Life lost. Lives left=%d", lives);
        if (lives < 0) {
            gameOn = false;
            return;
        }
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
        for (auto &m : movers) {
            m.gx += m.dir * m.speed * dt;
            const float wrap = (float)(gridCols + m.gw);
            if (m.gx < -m.gw)      m.gx += wrap;
            if (m.gx > wrap)       m.gx -= wrap;
        }
    }

    // collisions
    static bool rectOverlap(int ax,int ay,int aw,int ah,
                            int bx,int by,int bw,int bh) {
        return (ax < bx + bw) && (ax + aw > bx) &&
               (ay < by + bh) && (ay + ah > by);
    }

    bool onAnyLog(const FGEntity &f) const {
        for (const auto &m : movers) {
            if (!m.isLog || m.gy != f.gy) continue;
            int mx = (int)std::floor(m.gx + 0.5f);
            if (rectOverlap(f.gx, f.gy, f.gw, f.gh, mx, m.gy, m.gw, 1))
                return true;
        }
        return false;
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

    void carryFrog(float dx) {
        float nx = frog.gx + dx;
        int ix   = (int)std::round(nx);
        frog.gx  = std::clamp(ix, 0, gridCols - 1);
        if (frog.gx <= 0 || frog.gx >= gridCols - 1) {
            loseLife(); // fell off screen
        }
    }

    void moveFrog(int dx, int dy) {
        frog.gx = std::clamp(frog.gx + dx, 0, gridCols - 1);
        frog.gy = std::clamp(frog.gy + dy, 0, gridRows - 1);
        if (dy < 0) score += 1; // reward forward hops
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
        // draw an opt.scale x opt.scale block
        const int x0 = gx * cellW;
        const int y0 = gy * cellH;
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

    void drawFrame(float brightness, bool drawPaused=false) {
        model->clearOverlayBuffer();

        // background bands
        for (int gy=0; gy<gridRows; ++gy) {
            int r=0,g=0,b=0;
            if (gy==homeRow)            { r=30; g=30; b=30; }
            else if (isRiverRow(gy))    { r=10; g=10; b=40; }
            else if (gy==safeMedianRow) { r=20; g=20; b=20; }
            else if (isRoadRow(gy))     { r=30; g=30; b=30; }
            for (int gx=0; gx<gridCols; ++gx)
                drawRectGrid(gx, gy, 1, 1,
                             (int)(r*brightness),(int)(g*brightness),(int)(b*brightness), 1.0f);
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
            int mx = (int)std::floor(m.gx + 0.5f);
            drawRectGrid(mx, m.gy, m.gw, 1, m.r, m.g, m.b, brightness);
        }

        // frog (blink during invulnerability)
        float frogBright = brightness;
        if (invulnTimerMs>0.0 && ((int)(invulnTimerMs/100) % 2)==0) frogBright *= 0.35f;
        drawRectGrid(frog.gx, frog.gy, 1, 1, 255,255,255, frogBright);

        // --- HUD as 1px raw pixels (unscaled) ---
        const int panelW = model->getWidth();
        const int panelH = model->getHeight();

        // score dots across the top physical row (y=0)
        int dots = std::min((score/10) % panelW, panelW - 1);
        for (int i=0; i<dots; ++i) pxRaw(i, 0, 200,200,200);

        // lives as dots on bottom physical row (y=panelH-1)
        int yLives = panelH - 1;
        int xLives = 0;
        for (int i=0; i<std::max(0,lives); ++i) {
            pxRaw(xLives, yLives, 255,255,255);
            xLives += 2; // spacing
            if (xLives >= panelW) break;
        }

        if (drawPaused) {
            outputString("PAUSED", centerTextX("PAUSED"), std::max(0, gridRows/2-3), 255,255,255, 1);
        }

        model->flushOverlayBuffer();
    }

    int centerTextX(const std::string &text) const {
        const int glyphWidth = 4;
        const int textWidth  = glyphWidth * (int)text.size();
        int pos = (gridCols - textWidth) / 2;
        return std::max(0, pos);
    }

    void drawGameOver() {
        drawFrame(0.2f, false);
        outputString("GAME",  centerTextX("GAME"),  std::max(0, gridRows/2 - 6), 255,255,255, 1);
        outputString("OVER",  centerTextX("OVER"),  std::max(0, gridRows/2),     255,255,255, 1);
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
    int    lives=3;
    int    score=0;
    bool   paused=false;
    bool   gameOn=true;
    bool   waitingUntilOutput=false;
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

        // River & Road speeds (no legacy global)
        try { opts.riverMult = mapSpeedInt(std::stoi(findOption("River Speed", "1"))); } catch (...) {}
        try { opts.roadMult  = mapSpeedInt(std::stoi(findOption("Road Speed",  "1"))); } catch (...) {}

        // Speed Variability (% -> 0..1)
        try {
            int vpc = std::max(0, std::min(100, std::stoi(findOption("Speed Variability", "20"))));
            opts.variability = vpc / 100.0f;
        } catch (...) {}

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
