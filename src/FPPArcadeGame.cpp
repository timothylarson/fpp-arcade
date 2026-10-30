#include <fpp-pch.h>

// What every game shares, apart from FPP's plugin machinery (FPPArcade.cpp):
// the game and effect base classes, the letters, the pause menu, best scores,
// sounds and the end-show theme. Kept free of commands, the web API and the
// controllers, so tools/host can build the games on a Mac as well.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <vector>

#include "FPPArcade.h"

#include "common.h"
#include "settings.h"
#include "log.h"

#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlay.h"

#include "ArcadeSound.h"
#include "ArcadeDemo.h"
#include "FPPTetris.h"
#include "FPPPong.h"
#include "FPPSnake.h"
#include "FPPBreakout.h"
#include "FPPBreakoutVersus.h"
#include "FPPStickBridge.h"
#include "FPPSkyJump.h"
#include "FPPReindeerRun.h"
#include "FPPRoadRace.h"
#include "FPPSnowballFight.h"
#include "FPPFrogger.h"
#include "FPPOrbPop.h"
#include "FPPAsteroidOrbit.h"
#include "FPPMazeChase.h"
#include "FPPBarrelClimb.h"
#include "FPPStarDefenders.h"
#include "FPPShapeFit.h"
#include "FPPSimpleGames.h"

// The FPP Arcade Axis command's axes, in this order (see FPPArcadeGame::axis).
std::vector<std::string> AXIS({
    "Up -> Down", "Left -> Right", "Down -> Up", "Right -> Left"
});

FPPArcadeGame::FPPArcadeGame(Json::Value &c) : modelName(c["model"].asString()), config(c), idx(0) {
    lastValues[0] = 0; lastValues[1] = 0;
}

bool FPPArcadeGame::isRunning() {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m != nullptr) {
        FPPArcadeGameEffect *effect = dynamic_cast<FPPArcadeGameEffect*>(m->getRunningEffect());
        if (effect) {
                RunningEffect *re = m->getRunningEffect();
                if (re != nullptr) {
                    // If the running effect is a FPPArcadeGameEffect (actual game), consider it running.
                    // However, the temporary title overlay (GameTitleEffect) also inherits from
                    // FPPArcadeGameEffect. Treat the title overlay specially so it does NOT
                    // count as a running game — this allows the Select button to keep
                    // cycling through titles while the title overlay is displayed.
                    FPPArcadeGameEffect *effect = dynamic_cast<FPPArcadeGameEffect*>(re);
                    if (effect && re->name() != std::string("ArcadeTitle")) {
                        return true;
                    }
                }
        }
    }
    return false;
}

class ClearRunningEffect : public RunningEffect {
public:
    ClearRunningEffect(PixelOverlayModel *m) : RunningEffect(m) {}
    
    const std::string &name() const override {
        static std::string NAME = "Clear";
        return NAME;
    }
    
    virtual int32_t update() override {
        if (calledOnce) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return 0;
        }
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();
        calledOnce = true;
        return -1;
    }
    bool calledOnce = false;
};

bool FPPArcadeGame::takeExitRequest() {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    FPPArcadeGameEffect *effect = m ? dynamic_cast<FPPArcadeGameEffect *>(m->getRunningEffect()) : nullptr;
    return effect && effect->takeExit();
}

void FPPArcadeGame::stop() {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m != nullptr) {
        m->setRunningEffect(new ClearRunningEffect(m), 10);
    }
}

void FPPArcadeGame::axis(const std::string &axis, int value) {
    std::string btn = "";
    if (axis == AXIS[2]) { // DOWN->UP
        if (value == 0 && lastValues[0] < 0) {
            btn = "Down - Released";
        } else if (value == 0 && lastValues[0] > 0) {
            btn = "Up - Released";
        } else if (value != 0) {
            btn = value > 0 ? "Up - Pressed" : "Down - Pressed";
        }
        lastValues[0] = value;
    } else if (axis == AXIS[1]) { // left -> right
        if (value == 0 && lastValues[1] < 0) {
            btn = "Left - Released";
        } else if (value == 0 && lastValues[1] > 0) {
            btn = "Right - Released";
        } else if (value != 0) {
            btn = value > 0 ? "Right - Pressed" : "Left - Pressed";
        }
        lastValues[1] = value;
    } else if (axis == AXIS[0]) { // up -> down
        if (value == 0 && lastValues[0] < 0) {
            btn = "Up - Released";
        } else if (value == 0 && lastValues[0] > 0) {
            btn = "Down - Released";
        } else if (value != 0) {
            btn = value < 0 ? "Up - Pressed" : "Down - Pressed";
        }
        lastValues[0] = value;
    } else if (axis == AXIS[3]) { // right -> left
        if (value == 0 && lastValues[1] < 0) {
            btn = "Right - Released";
        } else if (value == 0 && lastValues[1] > 0) {
            btn = "Left - Released";
        } else if (value != 0) {
            btn = value < 0 ? "Right - Pressed" : "Left - Pressed";
        }
        lastValues[1] = value;
    }
    if (btn != "") {
        cheatInput(btn);   // directions only, so it never finishes the code here
        button(btn);
    }
}

// Two codes, entered while a game is playing (not paused), each press within
// 2 seconds of the last:
//   Up Up Down Down Left Right Left Right B A Start (the Konami code): the
//     last level, and an endless game's hardest with lives and power-ups
//     topped up
//   Up Up Down Down Left Right Left Right A B Start: the next level
// The directions play as usual; the B, A and Start that finish a code are kept
// from the game, so they don't fire, drop a Stacker row or pause it.
bool FPPArcadeGame::cheatInput(const std::string &button) {
    struct Code {
        const char *first, *second;
        int cheat;
        const char *what;
    };
    static const Code CODES[] = {
        { "B Button", "A Button", FPPArcadeGameEffect::CHEAT_LAST_LEVEL, "last level" },
        { "A Button", "B Button", FPPArcadeGameEffect::CHEAT_NEXT_LEVEL, "next level" },
    };
    static const char *DIRS[] = { "Up", "Up", "Down", "Down", "Left", "Right", "Left", "Right" };
    const size_t dash = button.find(" - ");
    const std::string name = dash == std::string::npos ? button : button.substr(0, dash);
    if (dash != std::string::npos && button.compare(dash, std::string::npos, " - Released") == 0) {
        cheatHeld.erase(name);
        return false;
    }
    if (!cheatHeld.insert(name).second && dash != std::string::npos) {
        return false;   // still held: an axis sending its position again
    }
    if (dash == std::string::npos) {
        cheatHeld.erase(name);   // a bare "Start" has no release to come
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - cheatAt > std::chrono::milliseconds(2000)) {
        cheatHist.clear();   // too slow between presses: start over
    }
    cheatAt = now;
    cheatHist.push_back(name);
    if (cheatHist.size() > 11) {
        cheatHist.erase(cheatHist.begin());
    }
    for (const Code &c : CODES) {
        std::vector<std::string> full(std::begin(DIRS), std::end(DIRS));
        full.push_back(c.first);
        full.push_back(c.second);
        full.push_back("Start");
        // Do the latest presses finish this code, or have they got as far
        // as its first button (past the directions)?
        for (size_t k = std::min(full.size(), cheatHist.size()); k >= 9; --k) {
            if (!std::equal(full.begin(), full.begin() + k, cheatHist.end() - k)) {
                continue;
            }
            if (k < full.size()) {
                return true;   // partway: keep this A or B from the game
            }
            cheatHist.clear();
            PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
            FPPArcadeGameEffect *effect = m ? dynamic_cast<FPPArcadeGameEffect *>(m->getRunningEffect()) : nullptr;
            if (!effect || effect->isPaused()) {
                return false;
            }
            LogInfo(VB_PLUGIN, "FPP Arcade: cheat code (%s) on %s\n", c.what, modelName.c_str());
            effect->requestCheat(c.cheat);
            return true;
        }
    }
    return false;
}


std::string FPPArcadeGame::findOption(const std::string &s, const std::string &def) {
    if (config.isMember("options")) {
        for (int x = 0; x < config["options"].size(); x++) {
            if (config["options"][x]["name"].asString() == s) {
                return config["options"][x]["value"].asString();
            }
        }
    }
    if (config.isMember(s)) {
        return config[s].asString();
    }
    return def;
}


static const std::map<uint8_t, std::vector<uint8_t>> LETTERS = {
    {'G', {1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1}},
    {'A', {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1}},
    {'M', {1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1}},
    {'E', {1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1}},
    {'F', {1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0}},
    {'O', {1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1}},
    {'V', {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 0}},
    {'R', {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1}},
    {'T', {1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0}},
    {'S', {1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1}},
    {'P', {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0}},
    {'B', {1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1}},
    {'K', {1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1}},
    {'L', {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1}},
    {'C', {0, 1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 1}},
    {'D', {1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0}},
    {'J', {0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1}},
    {'Q', {0, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1}},
    {'X', {1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1}},
    {'Z', {1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1}},
    {'H', {1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1}},

    {'Y', {1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0}},
    {'U', {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1}},
    {'W', {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1}},
    {'I', {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0}},
    {'N', {1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1}},   // an arch: solid, it read as a thick H

    
    {'0', {1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1}},
    {'1', {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0}},
    {'2', {1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1}},
    {'3', {1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1}},
    {'4', {1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1}},
    {'5', {1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1}},
    {'6', {1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1}},
    {'7', {1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1}},
    {'8', {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1}},
    {'9', {1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1}},

    {':', {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0}},
    {'!', {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0}},
    {'.', {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0}},
    {',', {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0}},
    {'-', {0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0}},
    {'\'', {0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}
};

// Letters drawn twice the size or more have room for more detail than the 3x5
// grid: these are 6x10 versions, scaled from there instead. Only the letters
// that need it; the rest are the 3x5 ones scaled up. One may be 7 wide (W),
// running one LED into the gap before the next letter.
static const std::map<char, std::array<const char *, 10>> LETTERS_2X = {
    // N with its diagonal stepping down through the middle
    {'N', {"XX..XX", "XX..XX", "XXX.XX", "XXXXXX", "XXXXXX", "XXXXXX", "XXXXXX", "XX.XXX", "XX..XX", "XX..XX"}},
    // D with rounded corners
    {'D', {"XXXX..", "XXXXX.", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XXXXX.", "XXXX.."}},
    // W with a centre stroke, seven wide so the stroke can be one LED
    {'W', {"XX...XX", "XX...XX", "XX...XX", "XX...XX", "XX.X.XX", "XXXXXXX", "XXXXXXX", "XXX.XXX", "XX...XX", "XX...XX"}},
    // G with a bar in from the right: without one it read as a C
    {'G', {"XXXXXX", "XXXXXX", "XX....", "XX....", "XX.XXX", "XX.XXX", "XX..XX", "XX..XX", "XXXXXX", "XXXXXX"}},
    // V tapering over its lower half rather than two bars and a point
    {'V', {"XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", "XX..XX", ".XXXX.", ".XXXX.", "..XX..", "..XX.."}},
};

bool arcadeGlyphScaled(char c, int px, int py, int scl) {
    if (scl >= 2) {
        auto it = LETTERS_2X.find(c);
        if (it != LETTERS_2X.end()) {
            const int hx = px * 2 / scl, hy = py * 2 / scl;
            return hx >= 0 && hy >= 0 && hy < 10 && hx < (int)strlen(it->second[hy]) && it->second[hy][hx] == 'X';
        }
    }
    return scl >= 1 && px < 3 * scl && arcadeGlyph(c, px / scl, py / scl);
}

int arcadeGlyphWidth(char c, int scl) {
    if (scl >= 2) {
        auto it = LETTERS_2X.find(c);
        if (it != LETTERS_2X.end()) {
            return (int)strlen(it->second[0]) * scl / 2;
        }
    }
    return 3 * scl;
}

bool arcadeGlyph(char c, int x, int y) {
    if (x < 0 || x > 2 || y < 0 || y > 4) {
        return false;
    }
    auto it = LETTERS.find((uint8_t)c);
    return it != LETTERS.end() && it->second.size() == 15 && it->second[y * 3 + x];
}

class Letter {
public:
    Letter(int l) {
        data.assign(15, 0);
        set(l);
    }
    
    uint8_t get(int x, int y) {
        return data[y * 3 + x];
    }
    void set(int x, int y, uint8_t d) {
        data[y * 3 + x] = d;
    }
private:
    void set(int l) {
        auto it = LETTERS.find(l);
        if (it != LETTERS.end()) {
            data = it->second;
        }
        if (data.size() != 15) {
            data.assign(15, 0);
        }
    }
    
    std::vector<uint8_t> data;
};
FPPArcadeGameEffect::FPPArcadeGameEffect(PixelOverlayModel *m)
    : RunningEffect(m),
      scale(1),
      offsetX(0),
      offsetY(0),
      lastFrameTime(std::chrono::steady_clock::now()),
      firstFrame(true) {
}
FPPArcadeGameEffect::~FPPArcadeGameEffect() {
}
int FPPArcadeGame::findIntOption(const std::string &s, int def) {
    const std::string v = findOption(s, "");
    if (v.empty()) {
        return def;
    }
    try {
        return std::stoi(v);
    } catch (...) {
        return def;   // blank or junk in the config must not throw out of a button press
    }
}

void FPPArcadeGameEffect::beginFrame() {
    model->setOverlayBufferDirty(false);
}
void FPPArcadeGameEffect::endFrame() {
    model->setOverlayBufferDirty(true);
}

static std::mutex bestLock;

bool arcadeRecordBest(const std::string &model, const std::string &game, int score, int *previousBest) {
    std::lock_guard<std::mutex> l(bestLock);
    const std::string dir = FPP_DIR_MEDIA("/plugindata/fpp-arcade");
    const std::string file = dir + "/best.json";
    Json::Value root;
    if (!FileExists(file) || !LoadJsonFromFile(file, root) || !root.isObject()) {
        root = Json::Value(Json::objectValue);
    }
    const std::string key = model + "/" + game;
    const int best = root.isMember(key) ? root[key].asInt() : 0;
    if (previousBest) {
        *previousBest = best;
    }
    if (score <= best) {
        return false;
    }
    root[key] = score;
    mkdir(dir.c_str(), 0775);
    SaveJsonToFile(root, file);
    return true;
}

FPPArcadeGameEffect::PanelTransform::PanelTransform(FPPArcadeGameEffect *eff)
    : e(eff), s(eff->scale), ox(eff->offsetX), oy(eff->offsetY), w(eff->pfW), h(eff->pfH) {
    e->scale = 1;
    e->offsetX = 0;
    e->offsetY = 0;
    e->pfW = e->model->getWidth();
    e->pfH = e->model->getHeight();
}
FPPArcadeGameEffect::PanelTransform::~PanelTransform() {
    e->scale = s;
    e->offsetX = ox;
    e->offsetY = oy;
    e->pfW = w;
    e->pfH = h;
}

void FPPArcadeGameEffect::setPlayfield(int logicalW, int logicalH, int scl) {
    if (scl < 1) {
        scl = 1;
    }
    const int maxW = std::max(1, model->getWidth() / scl);
    const int maxH = std::max(1, model->getHeight() / scl);
    // 0 = fill the panel. Oversized values are clamped rather than honoured so a
    // playfield can never be drawn partly off the model.
    pfW = (logicalW <= 0 || logicalW > maxW) ? maxW : logicalW;
    pfH = (logicalH <= 0 || logicalH > maxH) ? maxH : logicalH;
    scale = scl;
    offsetX = std::max(0, (model->getWidth()  - pfW * scl) / 2);
    offsetY = std::max(0, (model->getHeight() - pfH * scl) / 2);
}

void FPPArcadeGameEffect::outputPixel(int x, int y, int r, int g, int b, int scl) {
    if (scl == -1) {
        scl = scale;
    }
    x *= scl;
    x += offsetX;
    y *= scl;
    y += offsetY;
    for (int nx = 0; nx < scl; nx++) {
        for (int ny = 0; ny < scl; ny++) {
            model->setOverlayPixelValue(x + nx, y + ny, r, g, b);
        }
    }
}

void FPPArcadeGameEffect::drawUnderline(int x, int y, int length, int r, int g, int b, int scl) {
    if (scl == -1) {
        scl = scale;
    }
    // Letters are 5 rows tall (logical). Draw the underline on the row immediately
    // below the letters so it doesn't overlap the characters.
    const int letterHeight = 5;
    for (int i = 0; i < length * 4; i++) {
        outputPixel(x + i, y + letterHeight, r, g, b, scl);
    }
}

// Both ways of choosing a pause-menu entry (Start, or A/B) land here, so the
// two cannot drift apart.
void FPPArcadeGameEffect::activatePauseSelection() {
    if (pauseMenu.getSelectedOption() == PauseMenuOption::Exit) {
        // Ends the game as GAME OVER would, without the end show. Only a flag
        // here: the plugin stops the game once this press has been handled
        // (FPPArcadeGame::takeExitRequest), never from inside the game's own
        // button(). The menu stays up until then.
        exitWanted.store(true);
        return;
    }
    sfx("arcade", "Unpause");   // shared by every game, in sounds/arcade
    if (pauseMenu.isResumeSelected()) {
        resume();
        return;
    }
    restart();
    // Leaving the menu is part of what Restart means. The base restart() clears
    // `paused`, but every game overrides it and none chained to the base, so a
    // restart built the fresh state and then left it sitting behind the menu
    // until the player also picked Resume. Enforced here so a new game cannot
    // reintroduce it by forgetting a line.
    paused = false;
    resetFrameTimer();
}

void FPPArcadeGameEffect::drawPauseMenu() {
    static const char *const ITEMS[] = {"RESUME", "RESTART", "EXIT"};   // as in PauseMenuOption
    constexpr int N = 3;
    const int scl = fitTextScale({ITEMS[0], ITEMS[1], ITEMS[2]}, std::max(1, (int)(model->getHeight() / 16)));

    // Draw semi-transparent overlay (darken current buffer)
    for (int y = 0; y < model->getHeight(); y++) {
        for (int x = 0; x < model->getWidth(); x++) {
            int r, g, b;
            model->getOverlayPixelValue(x, y, r, g, b);
            model->setOverlayPixelValue(x, y, r/3, g/3, b/3);
        }
    }

    // The entries only (no PAUSED header, to save space), 5 rows of letters
    // each with a gap between them: two rows on a 25-row wall, less if the
    // three lines wouldn't fit.
    const int letterHeight = 5;
    const int gridRows = std::max(1, model->getHeight() / scl);
    const int spacing = std::clamp((gridRows - N * letterHeight) / (N - 1), 1, 2);
    const int totalMenuHeight = N * letterHeight + (N - 1) * spacing;
    const int optionTopY = std::max(0, (gridRows - totalMenuHeight) / 2);

    // The menu darkens and covers the whole matrix, so it centres on the panel.
    // Without this it inherited the game's transform: centerTextX() measured the
    // playfield, clamped to 0 because the words are wider than an 11-cell Tetris
    // well, and outputPixel() then shifted everything right by offsetX - the menu
    // started at the left wall and ran off to the right.
    PanelTransform pt(this);
    const int selected = (int)pauseMenu.getSelectedOption();
    for (int i = 0; i < N; ++i) {
        const std::string text = ITEMS[i];
        const int y = optionTopY + i * (letterHeight + spacing);
        outputString(text, centerTextX(text, scl), y, 255, 255, 255, scl);
        if (i == selected) {
            // The underline, on the row just beneath the letters.
            drawUnderline(centerTextX(text, scl), y, (int)text.length(), 255, 255, 255, scl);
        }
    }
}

void FPPArcadeGameEffect::button(const std::string &button) {
    // Activation keys when paused: A, B, Fire
    bool isActivate = (button == "A Button - Pressed") || (button == "B Button - Pressed") ||
                      (button == "Fire - Pressed") || (button == "Fire") || (button == "Fire Button - Pressed");

    // If paused and an activation key is pressed, select current option
    if (paused && isActivate) {
        activatePauseSelection();
        return;
    }

    // Start always toggles pause / activates selection when paused
    if (button == "Start - Pressed" || button == "Start") {
        if (!paused) {
            // The game's own sounds stop (not the other player's), and the
            // menu's plays.
            sfxStop(ArcadeSound::folderFor(name()));   // "MazeChase" -> mazechase
            sfx("arcade", "Pause");
            pause();
        } else {
            activatePauseSelection();
        }
        return;
    }

    // Navigation while paused
    if (paused) {
        handlePauseInput(button);
    }
}

void FPPArcadeGameEffect::handlePauseInput(const std::string &button) {
    if (button == "Up - Pressed" || button == "Left - Pressed") {
        pauseMenu.moveUp();
    } else if (button == "Down - Pressed" || button == "Right - Pressed") {
        pauseMenu.moveDown();
    }
}
void FPPArcadeGameEffect::outputLetter(int x, int y, char l, int r, int g, int b, int scl) {
    if (scl == -1) {
        scl = scale;
    }
    if (scl >= 2) {
        // Panel pixel by panel pixel, so the larger letters get their detail.
        const int x0 = x * scl + offsetX, y0 = y * scl + offsetY;
        const int w = arcadeGlyphWidth(l, scl);
        for (int py = 0; py < 5 * scl; py++) {
            for (int px = 0; px < w; px++) {
                if (arcadeGlyphScaled(l, px, py, scl)) {
                    model->setOverlayPixelValue(x0 + px, y0 + py, r, g, b);
                }
            }
        }
        return;
    }
    Letter letter(l);
    for (int nx = 0; nx < 3; nx++) {
        for (int ny = 0; ny < 5; ny++) {
            if (letter.get(nx, ny)) {
                outputPixel(x + nx, y + ny, r, g, b, scl);
            }
        }
    }
}
void FPPArcadeGameEffect::outputString(const std::string &s, int x, int y, int r, int g, int b, int scl) {
    for (auto ch : s) {
        outputLetter(x, y, ch, r, g, b, scl);
        x += 4;
    }
}

int FPPArcadeGameEffect::fitTextScale(const std::vector<std::string> &lines, int want, int widthPx) const {
    if (widthPx <= 0) {
        widthPx = model->getWidth();
    }
    size_t longest = 1;
    for (const auto &l : lines) {
        longest = std::max(longest, l.size());
    }
    // Letters are 3 pixels wide with a 1 pixel gap, and no gap after the last.
    const int textW = (int)longest * 4 - 1;
    return std::max(1, std::min(want, widthPx / textW));
}

void FPPArcadeGameEffect::sfx(const std::string &game, const std::string &name, float gain) {
    ArcadeSound::play(game, name, gain, model->getName());
}

void FPPArcadeGameEffect::sfxEndShow(const std::string &game, float gain) {
    const char *themed = showThemeSound(arcadeShowTheme());
    sfx(game, ArcadeSound::lengthMs(game, themed) > 0 ? themed : "EndShow", gain);
}

// The end-show theme (ShowTheme.h): 0 Christmas, 1 Halloween, 2 Everyday, 3 by date.
static std::atomic<int> showThemeSetting{3};

void arcadeSetShowTheme(const std::string &s) {
    showThemeSetting = s == "christmas" ? 0 : s == "halloween" ? 1 : s == "everyday" ? 2 : 3;
}

ShowTheme arcadeShowTheme() {
    const int s = showThemeSetting.load();
    if (s < 3) {
        return (ShowTheme)s;
    }
    const time_t now = time(nullptr);
    struct tm lt {};
    localtime_r(&now, &lt);
    const int month = lt.tm_mon + 1, day = lt.tm_mday;
    if (month == 10) {
        return ShowTheme::Halloween;
    }
    if ((month == 11 && day >= 15) || month == 12 || (month == 1 && day <= 6)) {
        return ShowTheme::Christmas;
    }
    return ShowTheme::Everyday;
}

void FPPArcadeGameEffect::sfxStop(const std::string &game) {
    ArcadeSound::stopAll(game, model->getName());
}

void FPPArcadeGameEffect::textBox(const std::vector<std::string> &lines, int r, int g, int b) {
    const int W = model->getWidth(), H = model->getHeight();
    const int n = std::min<int>((int)lines.size(), std::max(1, (H + 1) / 6));
    size_t longest = 1;
    for (int k = 0; k < n; ++k) {
        longest = std::max(longest, lines[k].size());
    }
    const int w = (int)longest * 4 + 1, h = n * 6 + 1;
    const int x0 = std::max(0, (W - w) / 2), y0 = std::max(0, (H - h) / 2);
    for (int y = y0; y < std::min(H, y0 + h); ++y) {
        for (int x = x0; x < std::min(W, x0 + w); ++x) {
            model->setOverlayPixelValue(x, y, 0, 0, 0);
        }
    }
    PanelTransform pt(this);
    for (int k = 0; k < n; ++k) {
        outputString(lines[k], centerTextX(lines[k], 1), y0 + 1 + k * 6, r, g, b, 1);
    }
}

int FPPArcadeGameEffect::centerTextX(const std::string &s, int scl) {
    if (scl == -1) {
        scl = scale == 0 ? 1 : scale;
    }
    // Centre within the playfield, not the panel. The returned x is a playfield
    // coordinate that outputPixel() then shifts by offsetX, so measuring against
    // the model here would add the centring offset twice and push text off to
    // one side whenever the playfield is smaller than the matrix.
    // pfW counts cells at `scale`, but the caller centres in units of `scl`
    // (the pause menu, for instance, draws at its own scale). Convert through
    // pixels so the two never disagree.
    const int pixelsWide = pfW > 0 ? (pfW * std::max(1, scale)) : model->getWidth();
    int gridWidth = std::max(1, pixelsWide / scl);
    int textWidth = static_cast<int>(s.size()) * 4;
    int x = (gridWidth - textWidth) / 2;
    if (x < 0) {
        x = 0;
    }
    return x;
}

double FPPArcadeGameEffect::consumeElapsedMs(double fallbackMs, double maxClampMs) {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
    lastFrameTime = now;
    if (firstFrame) {
        firstFrame = false;
        return fallbackMs;
    }
    if (elapsed <= 0.0) {
        elapsed = fallbackMs;
    }
    if (maxClampMs > 0.0) {
        elapsed = std::min(elapsed, maxClampMs);
    }
    return elapsed;
}

void FPPArcadeGameEffect::resetFrameTimer() {
    firstFrame = true;
    lastFrameTime = std::chrono::steady_clock::now();
}

void FPPArcadeGameEffect::pause() {
    paused = true;
    exitWanted.store(false);
    pauseMenu.reset();  // Resume selected
    drawPauseMenu();
    model->flushOverlayBuffer();
}

void FPPArcadeGameEffect::resume() {
    paused = false;
    resetFrameTimer();
}

void FPPArcadeGameEffect::restart() {
    // Base implementation - games should override this
    paused = false;
    resetFrameTimer();
}

class GameTitleEffect : public FPPArcadeGameEffect {
public:
    GameTitleEffect(PixelOverlayModel *m, const std::string &titleText)
        : FPPArcadeGameEffect(m), title(titleText), gameName(titleText) {
        std::string upper;
        upper.reserve(title.size());
        for (char ch : title) {
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        title.swap(upper);
        int width = 0;
        int height = 0;
        m->getSize(width, height);
        offsetX = 0;
        offsetY = 0;
        scale = 1;
        shownAt = std::chrono::steady_clock::now();
    }

    const std::string &name() const override {
        static std::string NAME = "ArcadeTitle";
        return NAME;
    }

    // A title nobody starts or changes clears after TIMEOUT_MS, handing the
    // model back to what was there before - the sequence, or the Kinect
    // picture (and Orb Pop's ambient mode after it, if that's set to come back).
    static constexpr int TIMEOUT_MS = 10000;

    int32_t update() override {
        if (clearing) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return EFFECT_DONE;
        }
        if (std::chrono::steady_clock::now() - shownAt >= std::chrono::milliseconds(TIMEOUT_MS)) {
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            clearing = true;
            return EFFECT_AFTER_NEXT_OUTPUT;
        }
        model->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        model->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        model->clearOverlayBuffer();
        // The title, then how to start, then the game's controls, then the
        // title again until it times out.
        const auto shown = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shownAt).count();
        bool drawn = false;
        if (shown >= 3000 && shown < 5500) {
            drawn = drawPage({"PRESS START", "SELECT: NEXT"}, 255, 220, 0) || drawPage({"PRESS START"}, 255, 220, 0);
        } else if (shown >= 5500 && shown < 9000) {
            auto it = HOW_TO.find(gameName);
            drawn = it != HOW_TO.end() && drawPage(it->second, 120, 200, 255);
        }
        if (!drawn) {
            drawTitle();
        }
        model->flushOverlayBuffer();
        return 100;
    }

private:
    void drawTitle() {
        const int rows = model->getHeight();
        // A title wider than the model goes one word per line, so "ASTEROID
        // ORBIT" still reads on a 30-wide half of the matrix.
        std::vector<std::string> lines;
        if ((int)title.size() * 4 - 1 > model->getWidth() && title.find(' ') != std::string::npos) {
            size_t start = 0;
            while (start <= title.size()) {
                size_t sp = title.find(' ', start);
                if (sp == std::string::npos) {
                    sp = title.size();
                }
                if (sp > start) {
                    lines.push_back(title.substr(start, sp - start));
                }
                start = sp + 1;
            }
        } else {
            lines.push_back(title);
        }
        int y = std::max(0, (rows - ((int)lines.size() * 6 - 1)) / 2);
        const int gridW = std::max(1, model->getWidth());
        for (const auto &l : lines) {
            if ((int)l.size() > 1 && (int)l.size() * 4 - 1 > gridW) {
                // Still a column or two too wide ("ASTEROID" is 31 on a
                // 30-wide half): close up some of the gaps rather than lose
                // the last letter off the edge.
                for (size_t k = 0; k < l.size(); ++k) {
                    outputLetter((int)(k * (gridW - 3) / (l.size() - 1)), y, l[k], 255, 255, 255, 1);
                }
            } else {
                outputString(l, centerTextX(l, 1), y, 255, 255, 255, 1);
            }
            y += 6;
        }
    }

    // Lines of small letters, centred; a line too wide for the model goes a
    // word a line. False, drawing nothing, if it all won't fit.
    bool drawPage(const std::vector<std::string> &page, int r, int g, int b) {
        const int W = model->getWidth(), H = model->getHeight();
        std::vector<std::string> lines;
        for (const auto &p : page) {
            if ((int)p.size() * 4 - 1 <= W) {
                lines.push_back(p);
                continue;
            }
            std::string word;
            for (size_t k = 0; k <= p.size(); ++k) {
                if (k == p.size() || p[k] == ' ') {
                    if (!word.empty()) {
                        lines.push_back(word);
                    }
                    word.clear();
                } else {
                    word += p[k];
                }
            }
        }
        const int blockH = (int)lines.size() * 6 - 1;
        if (lines.empty() || blockH > H) {
            return false;
        }
        for (const auto &l : lines) {
            if ((int)l.size() * 4 - 1 > W) {
                return false;
            }
        }
        int y = (H - blockH) / 2;
        for (const auto &l : lines) {
            outputString(l, centerTextX(l, 1), y, r, g, b, 1);
            y += 6;
        }
        return true;
    }

    // How to play each game, shown under its title: a few words a line, in
    // the 3x5 letters (15 to a line on a 60-wide wall).
    static inline const std::map<std::string, std::vector<std::string>> HOW_TO = {
        {"Tetris", {"ARROWS: MOVE", "UP OR A: TURN", "DOWN: FASTER"}},
        {"Pong", {"P1: UP DOWN", "P2: LEFT RIGHT"}},
        {"Snake", {"ARROWS: STEER", "A B: 2ND SNAKE"}},
        {"Breakout", {"ARROWS: MOVE", "A OR B: LAUNCH"}},
        {"Stick Bridge", {"HOLD TO GROW", "LET GO TO DROP"}},
        {"Sky Jump", {"UP: LAUNCH", "ARROWS: STEER", "A B: 2 PLAYERS"}},
        {"Reindeer Run", {"UP: JUMP", "DOWN: DUCK", "A B: 2ND DEER"}},
        {"Road Race", {"UP DOWN: LANE", "RIGHT: FASTER", "A B: 2ND CAR"}},
        {"Snowball Fight", {"LEFT RIGHT: AIM", "HOLD UP: THROW", "A B: GREEN ELF"}},
        {"Breakout Versus", {"ARROWS: MOVE", "UP: LAUNCH", "A B: TOP PADDLE"}},
        {"Frogger", {"ARROWS: HOP", "A: FASTER", "B: SLOWER"}},
        {"Orb Pop", {"STAND IN VIEW", "TOUCH THE ORBS"}},
        {"Shape Fit", {"FILL THE SHAPE", "WITH YOUR BODY"}},
        {"Asteroid Orbit", {"ARROWS: THRUST", "A: FIRE", "B: SHOCKWAVE"}},
        {"Maze Chase", {"ARROWS: TURN", "EAT THE DOTS"}},
        {"Barrel Climb", {"ARROWS: WALK", "UP DOWN: CLIMB", "A: JUMP"}},
        {"Star Defenders", {"ARROWS: MOVE", "A: FIRE", "B: LASER"}},
        {"Stacker", {"ANY BUTTON:", "DROP THE ROW"}},
        {"Flap", {"UP: FLAP", "A B: BLUE BIRD"}},
        {"Light Memory", {"COPY THE LIGHTS", "ARROWS: PICK"}},
    };

    std::string title;
    std::string gameName;
    std::chrono::steady_clock::time_point shownAt;
    bool clearing = false;
};

// The last game played on each model, for Orb Pop's "When it was the last
// game played" (it comes back to its ambient mode only after a game of it).
static std::mutex lastPlayedLock;
static std::map<std::string, std::string> lastPlayed;

void arcadeNotePlayed(const std::string &model, const std::string &game) {
    std::lock_guard<std::mutex> l(lastPlayedLock);
    lastPlayed[model] = game;
}

std::string arcadeLastPlayed(const std::string &model) {
    std::lock_guard<std::mutex> l(lastPlayedLock);
    auto it = lastPlayed.find(model);
    return it == lastPlayed.end() ? "" : it->second;
}

// EXIT on the pause menu: the game ends as at GAME OVER (see
// activatePauseSelection), stopped here once its button() has returned.
static void stopIfExited(FPPArcadeGame *g, const std::string &button) {
    if (g && g->takeExitRequest()) {
        LogInfo(VB_PLUGIN, "FPP Arcade: %s exited from the pause menu\n", g->getName().c_str());
        g->stop();
        const size_t dash = button.find(" - Pressed");
        g->releaseToIgnore = dash == std::string::npos ? "" : button.substr(0, dash);
    }
}

void arcadeShowTitle(FPPArcadeGame *game) {
    if (!game || game->isRunning()) {
        return;
    }
    PixelOverlayModel *model = PixelOverlayManager::INSTANCE.getModel(game->getModelName());
    if (!model) {
        return;
    }
    model->setRunningEffect(new GameTitleEffect(model, game->getName()), 20);
    ArcadeSound::stopAll(ArcadeSound::folderFor(game->getName()), game->getModelName());
    ArcadeSound::play(ArcadeSound::folderFor(game->getName()), "GameSelected", 1.0f, game->getModelName());
}


// Input on a model: it's in use (no demo for a while), and if a demo is
// playing, that press stops it and shows its game's title instead of doing
// anything else. True if the press was taken that way. pressed: the button
// whose release to ignore, as the title would take it for a press.
static bool demoTakeover(std::list<FPPArcadeGame *> &games, const std::string &pressed) {
    const std::string model = games.front()->getModelName();
    ArcadeDemo::noteInput(model);
    const std::string game = ArcadeDemo::stopDemo(model);
    if (game.empty()) {
        return false;
    }
    for (size_t k = 0; k < games.size() && games.front()->getName() != game; ++k) {
        games.push_back(games.front());
        games.pop_front();
    }
    games.front()->releaseToIgnore = pressed;
    arcadeShowTitle(games.front());
    return true;
}

void arcadeAxis(std::list<FPPArcadeGame *> &games, const std::string &axis, int value) {
    if (games.empty()) {
        return;
    }
    if (value != 0 && demoTakeover(games, "")) {
        return;   // the axis stays where the game last saw it, so its return to centre does nothing
    }
    ArcadeDemo::noteInput(games.front()->getModelName());
    games.front()->axis(axis, value);
}

void arcadeButton(std::list<FPPArcadeGame *> &games, const std::string &button) {
    if (games.empty()) {
        return;
    }
    if (button.find(" - Released") != std::string::npos) {
        ArcadeDemo::noteInput(games.front()->getModelName());
    } else {
        const size_t dash = button.find(" - Pressed");
        if (demoTakeover(games, dash == std::string::npos ? "" : button.substr(0, dash))) {
            return;
        }
    }
    std::string &ignore = games.front()->releaseToIgnore;
    if (!ignore.empty()) {
        if (button == ignore + " - Released") {
            ignore.clear();
            return;
        }
        if (button.find(" - Pressed") != std::string::npos) {
            ignore.clear();
        }
    }
    if (games.front()->isRunning() && games.front()->cheatInput(button)) {
        return;
    }
    if (button == "Start - Pressed") {
        if (games.front()->isRunning()) {
            // When a game is running, pressing Start should pause it (handled by the game/effect)
            games.front()->button(button);
            stopIfExited(games.front(), button);
            return;
        }
    }
    if (button == "Start - Released" || button == "Select - Released") {
        return;
    }
    if (button == "Select - Pressed") {
        // After its title has timed out, the first Select brings back the
        // game that's selected rather than skipping past it.
        {
            FPPArcadeGame *front = games.front();
            PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(front->getModelName());
            RunningEffect *re = m ? m->getRunningEffect() : nullptr;
            if (!front->isRunning() && !(re && re->name() == "ArcadeTitle")) {
                arcadeShowTitle(front);
                return;
            }
        }
        // If the currently-front game is running, stop it so the selection takes effect.
        FPPArcadeGame *current = games.front();
        if (current && current->isRunning()) {
            current->stop();
        }
        // Rotate the list so the next game becomes front
        FPPArcadeGame *g = games.front();
        games.pop_front();
        games.push_back(g);
        // Show title for the newly-selected game if it's not running
        if (!games.empty() && !games.front()->isRunning()) {
            arcadeShowTitle(games.front());
        }
    } else {
        // Forward other buttons to the currently-selected game
        games.front()->button(button);
        stopIfExited(games.front(), button);
    }
    if (games.front()->isRunning()) {
        arcadeNotePlayed(games.front()->getModelName(), games.front()->getName());
    }
}

// A game from its entry in the plugin's config ("game" is its name); null for
// one this plugin doesn't know.
FPPArcadeGame *arcadeCreateGame(Json::Value &config) {
    std::string game = config["game"].asString();
    if (game == "Tetris") {
        return new FPPTetris(config);
    }
    if (game == "Pong") {
        return new FPPPong(config);
    }
    if (game == "Snake") {
        return new FPPSnake(config);
    }
    if (game == "Breakout") {
        return new FPPBreakout(config);
    }
    if (game == "Frogger") {
        return new FPPFrogger(config);
    }
    if (game == "Orb Pop") {
        return new FPPOrbPop(config);
    }
    if (game == "Asteroid Orbit") {
        return new FPPAsteroidOrbit(config);
    }
    if (game == "Maze Chase") {
        return new FPPMazeChase(config);
    }
    if (game == "Barrel Climb") {
        return new FPPBarrelClimb(config);
    }
    if (game == "Star Defenders") {
        return new FPPStarDefenders(config);
    }
    if (game == "Shape Fit") {
        return new FPPShapeFit(config);
    }
    if (game == "Stacker") {
        return new FPPStacker(config);
    }
    if (game == "Flap") {
        return new FPPFlap(config);
    }
    if (game == "Light Memory") {
        return new FPPLightMemory(config);
    }
    if (game == "Breakout Versus") {
        return new FPPBreakoutVersus(config);
    }
    if (game == "Stick Bridge") {
        return new FPPStickBridge(config);
    }
    if (game == "Sky Jump") {
        return new FPPSkyJump(config);
    }
    if (game == "Reindeer Run") {
        return new FPPReindeerRun(config);
    }
    if (game == "Road Race") {
        return new FPPRoadRace(config);
    }
    if (game == "Snowball Fight") {
        return new FPPSnowballFight(config);
    }
    return nullptr;
}
