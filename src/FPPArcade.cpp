#include <fpp-pch.h>


#ifdef PLATFORM_OSX
#define USE_SDL_CONTROLLERS
#else
//#define USE_SDL_CONTROLLERS
#endif

#include <unistd.h>
#include <ifaddrs.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifndef USE_SDL_CONTROLLERS
#include <linux/joystick.h>
#else
extern "C" {
#include <SDL2/SDL.h>
}
#include "Timers.h"
#endif
#include <arpa/inet.h>
#include <cstring>
#include <list>
#include <array>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <cctype>
#include <cmath>
#include <fcntl.h>
#include <mutex>

#include "FPPArcade.h"

#include "commands/Commands.h"
#include "fpphttp.h"
#include "FileMonitor.h"
#include "common.h"
#include "settings.h"
#include "Plugin.h"
#include "log.h"

#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlay.h"

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
#include "ArcadeSound.h"

static std::vector<std::string> BUTTONS({
    "Up - Pressed", "Up - Released",
    "Down - Pressed", "Down - Released",
    "Left - Pressed", "Left - Released",
    "Right - Pressed", "Right - Released",
    "Up/Left - Pressed", "Up/Left - Released",
    "Up/Right - Pressed", "Up/Right - Released",
    "Down/Left - Pressed", "Down/Left - Released",
    "Down/Right - Pressed", "Down/Right - Released",
    "A Button - Pressed", "A Button - Released",
    "B Button - Pressed", "B Button - Released",
    "Select - Pressed", "Select - Released",
    "Start - Pressed", "Start - Released",
});

static std::vector<std::string> AXIS({
    "Up -> Down", "Left -> Right", "Down -> Up", "Right -> Left"
});

namespace {
struct ArcadeControllerInfo {
    std::string name;
    int numButtons;
    int numAxis;
};

std::mutex gArcadeControllersLock;
std::vector<ArcadeControllerInfo> gArcadeControllers;

std::mutex gArcadeEventsLock;
std::list<std::string> gArcadeLastEvents;

std::vector<ArcadeControllerInfo> getArcadeControllersSnapshot() {
    std::lock_guard<std::mutex> lock(gArcadeControllersLock);
    return gArcadeControllers;
}

std::list<std::string> getArcadeEventsSnapshot() {
    std::lock_guard<std::mutex> lock(gArcadeEventsLock);
    return gArcadeLastEvents;
}

void setArcadeControllers(std::vector<ArcadeControllerInfo>&& controllers) {
    std::lock_guard<std::mutex> lock(gArcadeControllersLock);
    gArcadeControllers = std::move(controllers);
}

void appendArcadeEvent(const std::string &s) {
    std::lock_guard<std::mutex> lock(gArcadeEventsLock);
    gArcadeLastEvents.push_back(s);
    while (gArcadeLastEvents.size() > 20) {
        gArcadeLastEvents.pop_front();
    }
}

void resetArcadeState() {
    {
        std::lock_guard<std::mutex> lock(gArcadeControllersLock);
        gArcadeControllers.clear();
    }
    {
        std::lock_guard<std::mutex> lock(gArcadeEventsLock);
        gArcadeLastEvents.clear();
    }
}

std::string getArcadePath(const HttpRequestPtr& req) {
    std::vector<std::string> pieces = getPathPieces(req->path());
    if (pieces.size() == 2 && pieces[0] == "arcade") {
        return pieces[1];
    }
    if (pieces.size() == 4 && pieces[0] == "api" && pieces[1] == "plugin-apis" && pieces[2] == "arcade") {
        return pieces[3];
    }
    return std::string();
}
}

class FPPArcadePlugin;
class FPPArcadeCommand : public Command {
public:
    FPPArcadeCommand(FPPArcadePlugin *p) : Command("FPP Arcade Button"), plugin(p) {
        args.push_back(CommandArg("Button", "string", "Button").setContentList(BUTTONS));
        args.push_back(CommandArg("Target", "string", "Target").setContentListUrl("api/models?simple=true", true));
    }
    
    virtual std::unique_ptr<Command::Result> run(const std::vector<std::string> &args) override;
    FPPArcadePlugin *plugin;
};

class FPPArcadeAxisCommand : public Command {
public:
    FPPArcadeAxisCommand(FPPArcadePlugin *p) : Command("FPP Arcade Axis"), plugin(p) {
        args.push_back(CommandArg("Axis", "string", "Axis").setContentList(AXIS));
        args.push_back(CommandArg("Target", "string", "Target").setContentListUrl("api/models?simple=true", true));
        args.push_back(CommandArg("Value", "int", "Value", true).setDefaultValue("0").setAdjustable().setRange(-32767, 32767));
    }
    
    virtual std::unique_ptr<Command::Result> run(const std::vector<std::string> &args) override;
    FPPArcadePlugin *plugin;
};

class FPPArcadeSelectGameCommand : public Command {
public:
    FPPArcadeSelectGameCommand(FPPArcadePlugin *p) : Command("FPP Arcade Select Game"), plugin(p) {
        args.push_back(CommandArg("Game", "int", "Game", true).setDefaultValue("1").setAdjustable().setRange(1, 100));
        args.push_back(CommandArg("Target", "string", "Target").setContentListUrl("api/models?simple=true", true));
    }
    
    virtual std::unique_ptr<Command::Result> run(const std::vector<std::string> &args) override;
    FPPArcadePlugin *plugin;
};

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

void FPPArcadeGame::stop() {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m != nullptr) {
        m->setRunningEffect(new ClearRunningEffect(m), 10);
    }
}

class GameTitleEffect : public FPPArcadeGameEffect {
public:
    GameTitleEffect(PixelOverlayModel *m, const std::string &titleText)
        : FPPArcadeGameEffect(m), title(titleText) {
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
    }

    const std::string &name() const override {
        static std::string NAME = "ArcadeTitle";
        return NAME;
    }

    int32_t update() override {
        int scl = scale <= 0 ? 1 : scale;
        int rows = std::max(1, model->getHeight() / scl);
        model->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        model->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        model->clearOverlayBuffer();
        // A title wider than the model goes one word per line, so "ASTEROID
        // ORBIT" still reads on a 30-wide half of the matrix.
        std::vector<std::string> lines;
        if ((int)title.size() * 4 - 1 > model->getWidth() / scl && title.find(' ') != std::string::npos) {
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
        int y = (rows - ((int)lines.size() * 6 - 1)) / 2;
        if (y < 0) {
            y = 0;
        }
        const int gridW = std::max(1, model->getWidth() / scl);
        for (const auto &l : lines) {
            if ((int)l.size() > 1 && (int)l.size() * 4 - 1 > gridW) {
                // Still a column or two too wide ("ASTEROID" is 31 on a
                // 30-wide half): close up some of the gaps rather than lose
                // the last letter off the edge.
                for (size_t k = 0; k < l.size(); ++k) {
                    outputLetter((int)(k * (gridW - 3) / (l.size() - 1)), y, l[k], 255, 255, 255, scl);
                }
            } else {
                outputString(l, centerTextX(l, scl), y, 255, 255, 255, scl);
            }
            y += 6;
        }
        model->flushOverlayBuffer();
        return 100;
    }

private:
    std::string title;
};
//default behavior will map the axis directions to button presses
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
    const int scl = fitTextScale({"RESUME", "RESTART"}, std::max(1, (int)(model->getHeight() / 16)));

    // Draw semi-transparent overlay (darken current buffer)
    for (int y = 0; y < model->getHeight(); y++) {
        for (int x = 0; x < model->getWidth(); x++) {
            int r, g, b;
            model->getOverlayPixelValue(x, y, r, g, b);
            model->setOverlayPixelValue(x, y, r/3, g/3, b/3);
        }
    }

    // Draw menu options only (no PAUSED header to save space)
    const std::string resumeText = "RESUME";
    const std::string restartText = "RESTART";

    // Logical letter height is 5 rows. Choose spacing (in logical rows) between
    // the two option lines; scale it relative to scl for larger displays.
    const int letterHeight = 5;
    // Increase vertical spacing between the two option lines so they don't feel cramped
    const int spacing = std::max(1, 2 * scl); // logical rows between the two words

    int gridRows = std::max(1, model->getHeight() / scl);
    int totalMenuHeight = letterHeight * 2 + spacing;
    const int optionTopY = std::max(0, (gridRows - totalMenuHeight) / 2);
    const int resumeY = optionTopY;
    const int restartY = optionTopY + letterHeight + spacing;

    // The menu darkens and covers the whole matrix, so it centres on the panel.
    // Without this it inherited the game's transform: centerTextX() measured the
    // playfield, clamped to 0 because the words are wider than an 11-cell Tetris
    // well, and outputPixel() then shifted everything right by offsetX - the menu
    // started at the left wall and ran off to the right.
    PanelTransform pt(this);
    outputString(resumeText, centerTextX(resumeText, scl), resumeY, 255, 255, 255, scl);
    outputString(restartText, centerTextX(restartText, scl), restartY, 255, 255, 255, scl);

    // Draw underline under selected option (directly beneath the 5-row letters)
    if (pauseMenu.isResumeSelected()) {
        drawUnderline(centerTextX(resumeText, scl), resumeY, (int)resumeText.length(), 255, 255, 255, scl);
    } else {
        drawUnderline(centerTextX(restartText, scl), restartY, (int)restartText.length(), 255, 255, 255, scl);
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
    pauseMenu.moveUp();  // Reset to Resume option
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

class FPPArcadePlugin : public FPPPlugins::Plugin, public FPPPlugins::APIProviderPlugin {
public:
    
    FPPArcadePlugin() : FPPPlugins::Plugin("fpp-arcade"), FPPPlugins::APIProviderPlugin() {
        LogInfo(VB_PLUGIN, "Initializing Arcade Plugin\n");
        resetArcadeState();

        // This plugin's configuration is its own JSON file rather than the
        // key=value settings file FPPPlugins::Plugin watches, so the
        // monitorSettings constructor argument would not see it. Watch it
        // directly, so editing the game list takes effect without restarting
        // fppd. shutdown() gives the watch back - the callback lives here.
        std::function<void()> reload = [this]() {
            LogInfo(VB_PLUGIN, "Arcade: game configuration changed, reloading\n");
            loadGames();
        };
        FileMonitor::INSTANCE.AddFile(name, FPP_DIR_CONFIG("/plugin.fpp-arcade.json"), reload);

        loadGames();
        
        if (FileExists(FPP_DIR_CONFIG("/joysticks.json"))) {
            Json::Value root;
            if (LoadJsonFromFile(FPP_DIR_CONFIG("/joysticks.json"), root)) {
                for (int x = 0; x < root.size(); x++) {
                    if (root[x]["enabled"].asBool()) {
                        std::string controller = root[x]["controller"].asString();
                        
                        if (root[x].isMember("button")) {
                            int button =  root[x]["button"].asInt();
                            std::string ev = controller + ":" + std::to_string(button);
                            events[ev + ":1"] = root[x]["pressed"];
                            events[ev + ":0"] = root[x]["released"];
                        } else if (root[x].isMember("axis")) {
                            int button =  root[x]["axis"].asInt();
                            std::string ev = controller + ":a" + std::to_string(button);
                            events[ev] = root[x]["command"];
                        }
                    }
                }
            }
        }
    }
    // Give back everything outside this library that points into it. Nothing
    // here is asynchronous, so no readiness predicate is needed.
    // Rebuild the game list from what is on disk now. Runs on the main loop,
    // which is also where the games are stepped and where a button event
    // reaches them, so nothing can be part way through one.
    void loadGames() {
        std::lock_guard<std::recursive_mutex> l(gamesLock);
        for (auto &a : games) {
            for (auto &g : a.second) {
                // Stop anything running on a model before its game goes: the
                // effect is owned by the overlay model, not by us.
                if (g && g->isRunning()) {
                    g->stop();
                }
                delete g;
            }
        }
        games.clear();

        // Games are numbered per model, 1..n in config order, which is what
        // "FPP Arcade Select Game" matches against. They used to be numbered
        // across every model, so with games on Matrix-Left and Matrix-Right
        // the right-hand side's Breakout was game 6, not 1. A game that fails
        // to create is skipped rather than left in the list as a null.
        Json::Value root;
        const bool haveConfig = FileExists(FPP_DIR_CONFIG("/plugin.fpp-arcade.json"))
            && LoadJsonFromFile(FPP_DIR_CONFIG("/plugin.fpp-arcade.json"), root);
        // Sound settings live beside the games, so saving the page applies them too.
        ArcadeSound::configure(haveConfig ? root["sound"] : Json::Value());
        {
            if (haveConfig && root.isMember("games")) {
                for (int x = 0; x < root["games"].size(); x++) {
                    if (root["games"][x]["enabled"].asBool()) {
                        std::string model = root["games"][x]["model"].asString();
                        FPPArcadeGame *g = createGame(root["games"][x]);
                        if (g != nullptr) {
                            games[model].push_back(g);
                            g->setIdx((int)games[model].size());
                        }
                    }
                }
            }
        }
    }

    virtual std::function<bool()> shutdown() override {
        FileMonitor::INSTANCE.RemoveFile(name, FPP_DIR_CONFIG("/plugin.fpp-arcade.json"));
        ArcadeSound::shutdown();   // its mixer thread runs code in this library
        std::lock_guard<std::recursive_mutex> l(gamesLock);
        for (auto &a : games) {
            for (auto &g : a.second) {
                g->release();
            }
        }
#ifdef USE_SDL_CONTROLLERS
        // Stop pumping SDL, then take back the event filter. SDL keeps
        // controller_event_filter - a function pointer into this .so - in a
        // global, and FPP uses SDL for its own audio and pumps events itself,
        // so a filter left installed is a call into an unmapped library the
        // moment SDL next sees an event. Deliberately NO SDL_Quit(): SDL is
        // shared with FPP's audio output and this plugin does not own it.
        Timers::INSTANCE.stopPeriodicTimer("ArcadeSDLEventPump");
        SDL_SetEventFilter(nullptr, nullptr);
#endif
        // The Command subclasses are declared here, so their vtables live in
        // this .so and they hold a back-pointer to this plugin.
        for (Command *c : myCommands) {
            // removeCommand() only unregisters - CommandManager deletes what is
            // still in its registry at shutdown, so taking one back means
            // owning it again.
            CommandManager::INSTANCE.removeCommand(c);
            delete c;
        }
        myCommands.clear();
        // Release the controller handles and joystick descriptors now rather
        // than at destruction, so a reload can reopen them. FPP has already
        // taken the descriptors out of its epoll loop by this point.
        {
            std::lock_guard<std::mutex> lock(joysticksLock);
            joysticks.clear();
        }
        resetArcadeState();
        return nullptr;
    }

    virtual ~FPPArcadePlugin() {
#ifdef USE_SDL_CONTROLLERS
        // Belt and braces if teardown never went through shutdown().
        Timers::INSTANCE.stopPeriodicTimer("ArcadeSDLEventPump");
        SDL_SetEventFilter(nullptr, nullptr);
#endif
        resetArcadeState();
        for (auto & a : games) {
            for (auto &g : a.second) {
                delete g;
            }
        }
    }
    FPPArcadeGame *createGame(Json::Value &config) {
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
    
    void displayGameTitle(FPPArcadeGame *game) {
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
    

    void handleButton(std::list<FPPArcadeGame *> &games, const std::string &button, const std::vector<std::string> &args) {
        if (games.empty()) {
            return;
        }
        if (games.front()->isRunning() && games.front()->cheatInput(button)) {
            return;
        }
        if (button == "Start - Pressed") {
            if (games.front()->isRunning()) {
                // When a game is running, pressing Start should pause it (handled by the game/effect)
                games.front()->button(button);
                return;
            }
        }
        if (button == "Start - Released" || button == "Select - Released") {
            return;
        }
        if (button == "Select - Pressed") {
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
                displayGameTitle(games.front());
            }
        } else {
            // Forward other buttons to the currently-selected game
            games.front()->button(button);
        }
    }
    std::unique_ptr<Command::Result>  selectGame(const std::vector<std::string> &args) {
        std::lock_guard<std::recursive_mutex> l(gamesLock);
        int idx = std::atoi(args[0].c_str());
        const std::string model = args.size() > 1 ? args[1] : "";
        int max = games[model].size();
        if (max == 0) {
            return std::make_unique<Command::ErrorResult>("FPP Arcade No games configured for model " + model);
        }
        if (games[model].front()->isRunning()) {
            games[model].front()->stop();
        }
        for (int x = 0; x < max; x++) {
            FPPArcadeGame *g = games[model].front();
            if (g->getIdx() == idx) {
                if (!g->isRunning()) {
                    displayGameTitle(g);
                }
                return std::make_unique<Command::Result>("FPP Arcade Game " + g->getName() + " Selected");
            } else {
                games[model].pop_front();
                games[model].push_back(g);
            }
        }
        return std::make_unique<Command::ErrorResult>("FPP Arcade Could not find game matching " + args[0] + " for model " + model);
    }
    virtual std::unique_ptr<Command::Result> runAxisCommand(const std::vector<std::string> &args) {
        std::lock_guard<std::recursive_mutex> l(gamesLock);
        const std::string axis = args[0];
        const std::string model = args.size() > 1 ? args[1] : "";
        int value = args.size() > 2 ? std::atoi(args[2].c_str()) : 0;

        if (model != "") {
            if (!games[model].empty()) {
                games[model].front()->axis(axis, value);
            }
        } else {
            for (auto &a : games) {
                if (!a.second.empty()) {
                    a.second.front()->axis(axis, value);
                }
            }
        }
        return std::make_unique<Command::Result>("FPP Arcade Axis Processed");
    }
    virtual std::unique_ptr<Command::Result> runCommand(const std::vector<std::string> &args) {
        std::lock_guard<std::recursive_mutex> l(gamesLock);
        const std::string button = args[0];
        const std::string model = args.size() > 1 ? args[1] : "";
        if (model != "") {
            if (!games[model].empty()) {
                handleButton(games[model], button, args);
            }
        } else {
            for (auto &a : games) {
                if (!a.second.empty()) {
                    handleButton(a.second, button, args);
                }
            }
        }
        return std::make_unique<Command::Result>("FPP Arcade Button Processed");
    }
    
    void registerApis() override {
        LogInfo(VB_PLUGIN, "Registering Arcade Plugin APIs\n");
        auto handleArcade = [](const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback) {
            std::string path = getArcadePath(req);
            LogDebug(VB_PLUGIN, "Arcade API Request: %s\n", path.c_str());
            if (path == "controllers") {
                auto joystickSnapshot = getArcadeControllersSnapshot();

                Json::Value response(Json::arrayValue);
                for (const auto &j : joystickSnapshot) {
                    Json::Value c;
                    c["name"] = j.name;
                    c["buttons"] = j.numButtons;
                    c["axis"] = j.numAxis;
                    response.append(c);
                }

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                std::string s = Json::writeString(writer, response);
                callback(makeStringResponse(s, 200, "application/json"));
            } else if (path == "events") {
                auto eventsSnapshot = getArcadeEventsSnapshot();
                std::string v;
                for (auto &a : eventsSnapshot) {
                    v += a + "\n";
                }
                callback(makeStringResponse(v, 200));
            } else if (path == "speakers") {
                callback(makeStringResponse(ArcadeSound::speakersJson(), 200, "application/json"));
            } else if (path == "sounds") {
                callback(makeStringResponse(ArcadeSound::soundListJson(), 200, "application/json"));
            } else if (path == "sound") {
                // One sound as raw PCM for a controller speaker; see ArcadeSound.h.
                std::string pcm;
                const int asked = atoi(req->getParameter("rate").c_str()), rate = asked > 0 ? asked : 22050;
                const bool ulaw = req->getParameter("fmt") == "ulaw";
                if (ulaw ? ArcadeSound::soundUlaw(req->getParameter("f"), rate, pcm)
                         : ArcadeSound::soundPcm(req->getParameter("f"), rate, pcm)) {
                    callback(makeStringResponse(pcm, 200, "application/octet-stream"));
                } else {
                    callback(makeStringResponse("No such sound", 404));
                }
            } else {
                callback(makeStringResponse("Not found", 404));
            }
        };
        auto handleArcade2 = handleArcade;
        auto handleArcade3 = handleArcade;
        auto handleArcade4 = handleArcade;
        auto handleArcade5 = handleArcade;

        // Only the plain paths are needed: Apache rewrites
        // api/plugin-apis/arcade/* to localhost:32322/arcade/*, stripping the
        // plugin-apis/ prefix, so "/api/plugin-apis/arcade/*" routes would
        // never be reached.
        //
        // Registered through FPP rather than drogon::app() directly: drogon has
        // no route removal, so a handler registered straight with it could never
        // be withdrawn and would pin this plugin in memory for the life of fppd.
        FPPPlugins::registerPluginApi("/arcade/controllers", std::move(handleArcade), {drogon::Get});
        FPPPlugins::registerPluginApi("/arcade/events", std::move(handleArcade2), {drogon::Get});
        FPPPlugins::registerPluginApi("/arcade/speakers", std::move(handleArcade3), {drogon::Get});
        FPPPlugins::registerPluginApi("/arcade/sounds", std::move(handleArcade4), {drogon::Get});
        FPPPlugins::registerPluginApi("/arcade/sound", std::move(handleArcade5), {drogon::Get});
    }

    void unregisterApis() override {
        // Neither returns until no request is inside the handler and the
        // handler itself has been destroyed, which is what makes a later
        // dlclose() safe.
        FPPPlugins::unregisterPluginApi("/arcade/controllers");
        FPPPlugins::unregisterPluginApi("/arcade/events");
        FPPPlugins::unregisterPluginApi("/arcade/speakers");
        FPPPlugins::unregisterPluginApi("/arcade/sounds");
        FPPPlugins::unregisterPluginApi("/arcade/sound");
    }

#ifdef USE_SDL_CONTROLLERS
    static int SDLCALL controller_event_filter(void *userdata, SDL_Event * event) {
        FPPArcadePlugin *p = (FPPArcadePlugin*)userdata;
        return p->handleSDLControllerEvent(event);
    }
    int handleSDLControllerEvent(SDL_Event * event) {
        switch (event->type) {
            case SDL_CONTROLLERAXISMOTION: {
                SDL_ControllerAxisEvent *ae = (SDL_ControllerAxisEvent*)event;
                std::string joystickName;
                {
                    std::lock_guard<std::mutex> lock(joysticksLock);
                    for (const auto &j : joysticks) {
                        if (j.joystickId == ae->which) {
                            joystickName = j.name;
                            break;
                        }
                    }
                }
                if (!joystickName.empty()) {
                    std::string s = joystickName;
                    s += " - ";
                    s += "axis: " + std::to_string(ae->axis);
                    s += ", value: " + std::to_string(ae->value);
                    appendLastEvent(s);
                    std::string evnt = joystickName + ":a" + std::to_string(ae->axis);
                    processEvent(evnt, ae->value);
                }
                return 0;
            }
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                SDL_ControllerButtonEvent *be = (SDL_ControllerButtonEvent*)event;
                std::string joystickName;
                {
                    std::lock_guard<std::mutex> lock(joysticksLock);
                    for (const auto &j : joysticks) {
                        if (j.joystickId == be->which) {
                            joystickName = j.name;
                            break;
                        }
                    }
                }
                if (!joystickName.empty()) {
                    std::string s = joystickName;
                    s += " - ";
                    s += "button: " + std::to_string(be->button);
                    s += ", value: " + std::to_string(be->state);
                    appendLastEvent(s);
                    std::string evnt = joystickName + ":" + std::to_string(be->button) + ":" + std::to_string(be->state);
                    processEvent(evnt, be->state);
                }
                return 0;
            }
        }
        return 1;  // let all events be added to the queue since we always return 1.
    }
#endif

    void appendLastEvent(const std::string &s) {
        appendArcadeEvent(s);
    }

    void updateControllerSnapshot() {
        std::vector<ArcadeControllerInfo> controllers;
        {
            std::lock_guard<std::mutex> lock(joysticksLock);
            controllers.reserve(joysticks.size());
            for (const auto &j : joysticks) {
                controllers.push_back({j.name, j.numButtons, j.numAxis});
            }
        }
        setArcadeControllers(std::move(controllers));
    }

    void checkUniqueNames() {
        std::lock_guard<std::mutex> lock(joysticksLock);
        std::map<std::string, int> names;
        for (auto &j : joysticks) {
            names[j.name]++;
            if (names[j.name] != 1) {
                j.name = j.name + " - " + std::to_string(names[j.name]);
            }
        }
    }
    void addOwnedCommand(Command *c) {
        myCommands.push_back(c);
        CommandManager::INSTANCE.addCommand(c);
    }

    virtual void addControlCallbacks(std::map<int, std::function<bool(int)>> &callbacks) override {
        addOwnedCommand(new FPPArcadeCommand(this));
        addOwnedCommand(new FPPArcadeAxisCommand(this));
        addOwnedCommand(new FPPArcadeSelectGameCommand(this));

#ifdef USE_SDL_CONTROLLERS
        SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
        SDL_SetEventFilter(controller_event_filter, this);
        // fppd no longer pumps the SDL event queue (it dropped the periodic
        // SDL_PollEvent loop it used to run). We must pump it ourselves or it
        // grows unbounded and controller input is never processed. Draining via
        // SDL_PollEvent runs the event filter (handling controller input) and
        // discards everything else. addPeriodicTimer fires on the main-loop
        // thread, which is required for SDL event handling on macOS.
        Timers::INSTANCE.addPeriodicTimer("ArcadeSDLEventPump", 16, []() {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                // Controller events are handled in controller_event_filter;
                // any remaining events are intentionally discarded.
            }
        });
        {
            std::lock_guard<std::mutex> lock(joysticksLock);
            for (int x = 0; x < SDL_NumJoysticks(); x++) {
                if (SDL_IsGameController(x)) {
                    joysticks.emplace_back(Joystick(x));
                }
            }
        }
        checkUniqueNames();
#else
        {
            std::lock_guard<std::mutex> lock(joysticksLock);
            for (int x = 0; x < 10; x++) {
                std::string js = "/dev/input/js" + std::to_string(x);
                if (FileExists(js)) {
                    int i = open(js.c_str(), O_RDONLY | O_NONBLOCK);
                    if (i >= 0) {
                        joysticks.emplace_back(Joystick(i));
                    } else {
                        LogWarn(VB_PLUGIN, "Could not open %s: %s\n", js.c_str(), strerror(errno));
                    }
                }
            }
        }
        checkUniqueNames();

        std::vector<std::pair<int, std::string>> joystickSnapshot;
        {
            std::lock_guard<std::mutex> lock(joysticksLock);
            joystickSnapshot.reserve(joysticks.size());
            for (const auto &a : joysticks) {
                joystickSnapshot.emplace_back(a.file, a.name);
            }
        }
        for (const auto &a : joystickSnapshot) {
            callbacks[a.first] = [joyName = a.second, this] (int f) {
                struct js_event ev;
                while (read(f, &ev, sizeof(ev)) > 0) {
                    if (!(ev.type & JS_EVENT_INIT)) {
                        std::string s = joyName;
                        s += " - ";
                        if (ev.type == 1) {
                            s += "button: " + std::to_string(ev.number);
                        } else if (ev.type == 2) {
                            s += "axis: " + std::to_string(ev.number);
                        }
                        s += ", value: " + std::to_string(ev.value);
                        appendLastEvent(s);
                        
                        std::string evnt = joyName + ":" + (ev.type == 2 ? "a" : "") + std::to_string(ev.number);
                        if (ev.type == 1) {
                            evnt += ":" + std::to_string(ev.value);
                        }
                        processEvent(evnt, ev.value);
                    }
                }
                return false;
            };
        }
#endif
    updateControllerSnapshot();
    }
    
    void processEvent(const std::string &ev, int value) {
        const auto &f = events.find(ev);
        if (f != events.end()) {
            if (f->second["command"] != "") {
                if (f->second["command"] == "FPP Arcade Axis") {
                    Json::Value val = f->second;
                    val["args"][2] = std::to_string(value);
                    CommandManager::INSTANCE.run(val);
                } else {
                    CommandManager::INSTANCE.run(f->second);
                }
            }
        }
    }
    
    std::map<std::string, std::list<FPPArcadeGame*>> games;
    // Input comes from more than one thread at once: FPP runs commands from
    // the web API on its own threads, the controllers come in through the SDL
    // pump, and a config change reloads the games. Two players pressing at
    // the same moment had two threads changing the same game's cheat-code
    // history (and Select reordering this list) together, which crashed fppd.
    // Everything that reads or changes the games, or hands them input, holds
    // this.
    std::recursive_mutex gamesLock;
    
    class Joystick {
    public:
        Joystick(int f) : file(f) {
#ifndef USE_SDL_CONTROLLERS
            char buf[256] = {0};
            ioctl(file, JSIOCGNAME(sizeof(buf)), buf);
            name = buf;
            TrimWhiteSpace(name);

            char tmp;
            ioctl(file, JSIOCGAXES, &tmp);
            numAxis = tmp;
            ioctl(file, JSIOCGBUTTONS, &tmp);
            numButtons = tmp;
#else
            controller = SDL_GameControllerOpen(f);
            if (!controller) {
                joystickId = -1;
                name = "SDL Controller " + std::to_string(f);
                LogWarn(VB_PLUGIN, "Could not open SDL controller %d: %s\n", f, SDL_GetError());
            } else {
                SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
                if (!joystick) {
                    joystickId = -1;
                    name = "SDL Controller " + std::to_string(f);
                    LogWarn(VB_PLUGIN, "Could not get SDL joystick for controller %d: %s\n", f, SDL_GetError());
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                } else {
                    const char *controllerName = SDL_GameControllerName(controller);
                    joystickId = SDL_JoystickInstanceID(joystick);
                    name = controllerName ? controllerName : ("SDL Controller " + std::to_string(f));
                    numAxis = SDL_JoystickNumAxes(joystick);
                    numButtons = std::max(15, SDL_JoystickNumButtons(joystick));
                }
            }
#endif
        }
        Joystick(Joystick &&j) : file(j.file), name(j.name), numButtons(j.numButtons), numAxis(j.numAxis), controller(j.controller), joystickId(j.joystickId) {
            j.file = -1;
            j.controller = nullptr;
        }
        ~Joystick() {
            if (file != -1) {
                close(file);
            }
#ifdef USE_SDL_CONTROLLERS
            if (controller) {
                SDL_GameControllerClose(controller);
            }
#endif
        }
#ifdef USE_SDL_CONTROLLERS
        SDL_GameController *controller = nullptr;
        SDL_JoystickID joystickId = 0;
#else
        void *controller = nullptr;
        int joystickId = 0;
#endif
        std::string name;
        int numButtons = 0;
        int numAxis = 0;
        int file;
    };
    
    std::list<Joystick> joysticks;
    std::mutex joysticksLock;
    std::map<std::string, Json::Value> events;
    std::vector<Command *> myCommands;
};


std::unique_ptr<Command::Result> FPPArcadeCommand::run(const std::vector<std::string> &args) {
    return plugin->runCommand(args);
}
std::unique_ptr<Command::Result> FPPArcadeAxisCommand::run(const std::vector<std::string> &args) {
    return plugin->runAxisCommand(args);
}
std::unique_ptr<Command::Result> FPPArcadeSelectGameCommand::run(const std::vector<std::string> &args) {
    return plugin->selectGame(args);
}

// Safe to dlclose() on unload: no threads of its own, no CurlManager requests
// and no drogon client objects. The routes go through registerPluginApi() and
// come back in unregisterApis(); shutdown() stops the SDL pump timer, takes
// back the SDL event filter (a function pointer into this library that SDL
// holds globally), withdraws the three commands and releases the controllers
// and joystick descriptors. SDL itself is left initialised on purpose - FPP's
// audio output shares it.
FPP_PLUGIN_SUPPORTS_UNLOAD()

extern "C" {
    FPPPlugins::Plugin *createPlugin() {
        return new FPPArcadePlugin();
    }
}
