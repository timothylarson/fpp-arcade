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
#include <vector>
#include <algorithm>
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
#include "FPPFrogger.h"

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
            return true;
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
        int x = centerTextX(title, scl);
        int y = (rows - 5) / 2;
        if (y < 0) {
            y = 0;
        }
        outputString(title, x, y, 255, 255, 255, scl);
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
        button(btn);
    }
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
    {'H', {1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1}},

    {'Y', {1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0}},
    {'U', {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1}},
    {'W', {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1}},
    {'I', {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0}},
    {'N', {1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1}},

    
    {'0', {1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1}},
    {'1', {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0}},
    {'2', {1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1}},
    {'3', {1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1}},
    {'4', {1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1}},
    {'5', {1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1}},
    {'6', {1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1}},
    {'7', {1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1}},
    {'8', {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1}},
    {'9', {1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1}},

    {':', {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0}}
};

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
void FPPArcadeGameEffect::outputLetter(int x, int y, char l, int r, int g, int b, int scl) {
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

int FPPArcadeGameEffect::centerTextX(const std::string &s, int scl) {
    if (scl == -1) {
        scl = scale == 0 ? 1 : scale;
    }
    int gridWidth = std::max(1, model->getWidth() / scl);
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

        int idx = 0;
        if (FileExists(FPP_DIR_CONFIG("/plugin.fpp-arcade.json"))) {
            Json::Value root;
            if (LoadJsonFromFile(FPP_DIR_CONFIG("/plugin.fpp-arcade.json"), root)
                && root.isMember("games")) {
                for (int x = 0; x < root["games"].size(); x++) {
                    if (root["games"][x]["enabled"].asBool()) {
                        std::string model = root["games"][x]["model"].asString();
                        games[model].push_back(createGame(root["games"][x]));
                        if (games[model].back() != nullptr) {
                            games[model].back()->setIdx(++idx);
                        }
                    }
                }
            }
        }
        
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

        int idx = 0;
        if (FileExists(FPP_DIR_CONFIG("/plugin.fpp-arcade.json"))) {
            Json::Value root;
            if (LoadJsonFromFile(FPP_DIR_CONFIG("/plugin.fpp-arcade.json"), root)
                && root.isMember("games")) {
                for (int x = 0; x < root["games"].size(); x++) {
                    if (root["games"][x]["enabled"].asBool()) {
                        std::string model = root["games"][x]["model"].asString();
                        games[model].push_back(createGame(root["games"][x]));
                        if (games[model].back() != nullptr) {
                            games[model].back()->setIdx(++idx);
                        }
                    }
                }
            }
        }
    }

    virtual std::function<bool()> shutdown() override {
        FileMonitor::INSTANCE.RemoveFile(name, FPP_DIR_CONFIG("/plugin.fpp-arcade.json"));
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
    }
    

    void handleButton(std::list<FPPArcadeGame *> &games, const std::string &button, const std::vector<std::string> &args) {
        if (games.empty()) {
            return;
        }
        if (button == "Start - Pressed" || button == "Select - Pressed") {
            if (games.front()->isRunning()) {
                games.front()->stop();
            }
        }
        if (button == "Start - Released" || button == "Select - Released") {
            return;
        }
        if (button == "Select - Pressed") {
            FPPArcadeGame *g = games.front();
            games.pop_front();
            games.push_back(g);
            if (!games.empty() && !games.front()->isRunning()) {
                displayGameTitle(games.front());
            }
        } else {
            games.front()->button(button);
        }
    }
    std::unique_ptr<Command::Result>  selectGame(const std::vector<std::string> &args) {
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
            } else {
                callback(makeStringResponse("Not found", 404));
            }
        };
        auto handleArcade2 = handleArcade;

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
    }

    void unregisterApis() override {
        // Neither returns until no request is inside the handler and the
        // handler itself has been destroyed, which is what makes a later
        // dlclose() safe.
        FPPPlugins::unregisterPluginApi("/arcade/controllers");
        FPPPlugins::unregisterPluginApi("/arcade/events");
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
