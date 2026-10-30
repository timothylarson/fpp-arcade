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
#include <sys/ioctl.h>
#include "EPollManager.h"
#else
extern "C" {
#include <SDL2/SDL.h>
}
#endif
#include "Timers.h"
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
#include "ArcadeDemo.h"

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

// Sets controller speakers' volumes from a playlist or schedule: one speaker
// by name, every speaker playing for a model, or All. Show Volume -1 leaves
// the show music's volume as it is. Saved on the speaker, so it keeps it.
class FPPArcadeSpeakerVolumeCommand : public Command {
public:
    FPPArcadeSpeakerVolumeCommand() : Command("FPP Arcade Speaker Volume") {
        args.push_back(CommandArg("Speaker", "string", "Speaker or model").setContentListUrl("api/plugin-apis/arcade/speakernames"));
        args.push_back(CommandArg("Volume", "int", "Game volume (%)", true).setDefaultValue("50").setAdjustable().setRange(0, 100));
        args.push_back(CommandArg("ShowVolume", "int", "Show music volume (%, -1 leaves it)", true)
                           .setDefaultValue("-1").setAdjustable().setRange(-1, 100));
    }

    std::unique_ptr<Command::Result> run(const std::vector<std::string> &args) override {
        const std::string who = args.size() > 0 ? args[0] : "All";
        const int v = std::clamp(args.size() > 1 ? atoi(args[1].c_str()) : 50, 0, 100);
        const int show = std::clamp(args.size() > 2 ? atoi(args[2].c_str()) : -1, -1, 100);
        std::string q = "/volume?v=" + std::to_string(v) + (show >= 0 ? "&show=" + std::to_string(show) : "") + "&save=1";
        int n = 0, status;
        std::string body;
        for (const std::string &ip : ArcadeSound::speakerIps(who)) {
            n += ArcadeSound::speakerRequest(ip, q, "", status, body) && status == 200 ? 1 : 0;
        }
        return std::make_unique<Command::Result>("Set " + std::to_string(n) + " speaker" + (n == 1 ? "" : "s"));
    }
};

//default behavior will map the axis directions to button presses

class FPPArcadePlugin : public FPPPlugins::Plugin, public FPPPlugins::APIProviderPlugin,
                        public FPPPlugins::ChannelDataPlugin {
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
        // Demos (ArcadeDemo.h): record what's played, and play them on idle models.
        Timers::INSTANCE.addPeriodicTimer("ArcadeDemo", 40, [this]() {
            std::lock_guard<std::recursive_mutex> l(gamesLock);
            ArcadeDemo::tick(games);
        });
        
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
        // And the end shows' theme.
        arcadeSetShowTheme(haveConfig && root.isMember("shows") ? root["shows"]["theme"].asString() : "date");
        // And the demos'.
        ArcadeDemo::configure(haveConfig && root.isMember("demos") ? root["demos"]["idle"].asInt() : 5,
                              haveConfig && root.isMember("demos") ? root["demos"]["between"].asInt() : 0);
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
        Timers::INSTANCE.stopPeriodicTimer("ArcadeDemo");
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
        // taken the startup descriptors out of its epoll loop by this point;
        // the ones added since are this plugin's to take back.
#ifndef USE_SDL_CONTROLLERS
        Timers::INSTANCE.stopPeriodicTimer("ArcadeJoystickWatch");
        for (int f : ownFds) {
            EPollManager::INSTANCE.removeFileDescriptor(f);
        }
        ownFds.clear();
#endif
        {
            std::lock_guard<std::mutex> lock(joysticksLock);
            joysticks.clear();
        }
        resetArcadeState();
        return nullptr;
    }

    virtual ~FPPArcadePlugin() {
        Timers::INSTANCE.stopPeriodicTimer("ArcadeDemo");
#ifdef USE_SDL_CONTROLLERS
        // Belt and braces if teardown never went through shutdown().
        Timers::INSTANCE.stopPeriodicTimer("ArcadeSDLEventPump");
        SDL_SetEventFilter(nullptr, nullptr);
#else
        Timers::INSTANCE.stopPeriodicTimer("ArcadeJoystickWatch");
#endif
        resetArcadeState();
        for (auto & a : games) {
            for (auto &g : a.second) {
                delete g;
            }
        }
    }
    FPPArcadeGame *createGame(Json::Value &config) { return arcadeCreateGame(config); }
    
    void displayGameTitle(FPPArcadeGame *game) { arcadeShowTitle(game); }
    void handleButton(std::list<FPPArcadeGame *> &games, const std::string &button, const std::vector<std::string> &) {
        arcadeButton(games, button);
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
            arcadeAxis(games[model], axis, value);
        } else {
            for (auto &a : games) {
                arcadeAxis(a.second, axis, value);
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
        auto handleArcade = [this](const HttpRequestPtr& req,
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
            } else if (path == "speakernames") {
                // For FPP Arcade Speaker Volume's list: All, each speaker, each model.
                Json::Value names(Json::arrayValue);
                names.append("All");
                Json::Value list;
                Json::Reader().parse(ArcadeSound::speakersJson(), list);
                std::set<std::string> seen;
                for (const char *field : {"name", "model"}) {
                    for (const auto &s : list) {
                        const std::string n = s[field].asString();
                        if (!n.empty() && seen.insert(n).second) {
                            names.append(n);
                        }
                    }
                }
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                callback(makeStringResponse(Json::writeString(writer, names), 200, "application/json"));
            } else if (path == "speaker") {
                callback(speakerApi(req));
            } else if (path == "demos") {
                callback(demosApi(req));
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
        auto handleArcade6 = handleArcade;
        auto handleArcade7 = handleArcade;
        auto handleArcade8 = handleArcade;

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
        FPPPlugins::registerPluginApi("/arcade/speaker", std::move(handleArcade6), {drogon::Get, drogon::Post});
        FPPPlugins::registerPluginApi("/arcade/speakernames", std::move(handleArcade7), {drogon::Get});
        FPPPlugins::registerPluginApi("/arcade/demos", std::move(handleArcade8), {drogon::Get, drogon::Post});
    }

    // api/plugin-apis/arcade/demos?model=<model>: that model's last run and
    // saved demos, as {"last": {game, seconds}, "demos": [{game, seconds}]}.
    // POST with do=save keeps the last run as its game's demo, do=delete&game=
    // removes one, and do=play&game= plays it now.
    HttpResponsePtr demosApi(const HttpRequestPtr &req) {
        const std::string model = req->getParameter("model"), what = req->getParameter("do"), game = req->getParameter("game");
        if (model.empty()) {
            return makeStringResponse("No model", 400);
        }
        if (what == "save") {
            std::string err;
            if (!ArcadeDemo::saveLastRun(model, err)) {
                return makeStringResponse(err, 409);
            }
        } else if (what == "delete") {
            if (!ArcadeDemo::remove(model, game)) {
                return makeStringResponse("No " + game + " demo on " + model, 404);
            }
        } else if (what == "play") {
            std::string overlay = "Overwrite";
            {
                std::lock_guard<std::recursive_mutex> l(gamesLock);
                for (FPPArcadeGame *g : games[model]) {
                    if (g->getName() == game) {
                        overlay = g->overlayMode();
                    }
                }
            }
            if (!ArcadeDemo::play(model, game, overlay)) {
                return makeStringResponse("No " + game + " demo on " + model + " that fits it", 404);
            }
        } else if (!what.empty()) {
            return makeStringResponse("Unknown request", 400);
        }
        Json::Value v;
        const ArcadeDemo::Info last = ArcadeDemo::lastRun(model);
        v["last"]["game"] = last.game;
        v["last"]["seconds"] = last.seconds;
        v["demos"] = Json::Value(Json::arrayValue);
        for (const auto &d : ArcadeDemo::demos(model)) {
            Json::Value e;
            e["game"] = d.game;
            e["seconds"] = d.seconds;
            v["demos"].append(e);
        }
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        return makeStringResponse(Json::writeString(writer, v), 200, "application/json");
    }

    // api/plugin-apis/arcade/speaker?ip=<speaker>&do=<what>: the Arcade Games
    // page's controls for a controller speaker, passed on by FPP (see
    // ArcadeSound::speakerRequest). do is status (its /status JSON), volume
    // (v, show: 0-100, either; save=1 keeps it), test, sync, or save (POST:
    // name, model, show, ledAlways, showOffset - the speaker restarts).
    static HttpResponsePtr speakerApi(const HttpRequestPtr &req) {
        const std::string ip = req->getParameter("ip"), what = req->getParameter("do");
        auto pct = [&](const std::string &k) { return std::to_string(std::clamp(atoi(req->getParameter(k).c_str()), 0, 100)); };
        auto enc = [](const std::string &s) {
            static const char *HEX = "0123456789ABCDEF";
            std::string o;
            for (unsigned char c : s) {
                if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    o += (char)c;
                } else {
                    o += '%';
                    o += HEX[c >> 4];
                    o += HEX[c & 15];
                }
            }
            return o;
        };
        std::string path, form;
        if (what == "status" || what == "test" || what == "sync") {
            path = "/" + what;
        } else if (what == "volume") {
            path = "/volume?";
            if (!req->getParameter("v").empty()) {
                path += "v=" + pct("v") + "&";
            }
            if (!req->getParameter("show").empty()) {
                path += "show=" + pct("show") + "&";
            }
            if (req->getParameter("save") == "1") {
                path += "save=1";
            }
        } else if (what == "save") {
            // The speaker's /save takes the whole set-up form, so keep its Wi-Fi
            // and FPP address as they are (a blank password keeps that too).
            int status;
            std::string body;
            Json::Value cur;
            if (!ArcadeSound::speakerRequest(ip, "/status", "", status, body) || status != 200 ||
                !Json::Reader().parse(body, cur) || cur["ssid"].asString().empty()) {
                return makeStringResponse("The speaker didn't answer", 502);
            }
            const std::string name = req->getParameter("name"), model = req->getParameter("model");
            path = "/save";
            form = "ssid=" + enc(cur["ssid"].asString()) + "&pass=&fpp=" + enc(cur["fpp"].asString()) +
                   "&model=" + enc(model.empty() ? cur["model"].asString() : model) +
                   "&name=" + enc(name.empty() ? cur["name"].asString() : name) +
                   (req->getParameter("show") == "on" ? "&show=on" : "") +
                   (req->getParameter("ledAlways") == "on" ? "&ledAlways=on" : "") +
                   "&showOffset=" + std::to_string(std::clamp(atoi(req->getParameter("showOffset").c_str()), -2000, 2000));
        } else {
            return makeStringResponse("Unknown request", 400);
        }
        int status;
        std::string body;
        if (!ArcadeSound::speakerRequest(ip, path, form, status, body)) {
            return makeStringResponse("The speaker didn't answer", 502);
        }
        return makeStringResponse(body, status ? status : 502, what == "status" ? "application/json" : "text/plain");
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
        FPPPlugins::unregisterPluginApi("/arcade/speaker");
        FPPPlugins::unregisterPluginApi("/arcade/speakernames");
        FPPPlugins::unregisterPluginApi("/arcade/demos");
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

#ifndef USE_SDL_CONTROLLERS
    // Reads a controller's events into commands. When its device has gone
    // (unplugged, or its receiver dropped off the bus), it stops FPP watching
    // it at once - a dead descriptor would wake the main loop for ever - and
    // marks it for watchJoysticks() to close.
    std::function<bool(int)> joystickCallback(const std::string &joyName) {
        return [joyName, this](int f) {
            struct js_event ev;
            ssize_t n;
            while ((n = read(f, &ev, sizeof(ev))) > 0) {
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
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                EPollManager::INSTANCE.removeFileDescriptor(f);
                ownFds.erase(f);
                std::lock_guard<std::mutex> lock(joysticksLock);
                for (auto &j : joysticks) {
                    if (j.file == f) {
                        j.dead = true;
                    }
                }
            }
            return false;
        };
    }

    // Every two seconds on the main loop: close the controllers that have
    // gone, and open any new /dev/input/jsN.
    void watchJoysticks() {
        std::vector<int> added;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(joysticksLock);
            for (auto it = joysticks.begin(); it != joysticks.end();) {
                // Gone, or its path now belongs to a different device.
                struct stat now, mine;
                const bool gone = it->dead || it->path.empty() || stat(it->path.c_str(), &now) != 0 ||
                                  fstat(it->file, &mine) != 0 || now.st_rdev != mine.st_rdev;
                if (gone) {
                    LogInfo(VB_PLUGIN, "FPP Arcade: controller %s (%s) disconnected\n", it->name.c_str(), it->path.c_str());
                    if (!it->dead) {
                        EPollManager::INSTANCE.removeFileDescriptor(it->file);
                        ownFds.erase(it->file);
                    }
                    it = joysticks.erase(it);   // closes it
                    changed = true;
                } else {
                    ++it;
                }
            }
            for (int x = 0; x < 10; x++) {
                const std::string js = "/dev/input/js" + std::to_string(x);
                if (!FileExists(js) ||
                    std::any_of(joysticks.begin(), joysticks.end(), [&](const Joystick &j) { return j.path == js; })) {
                    continue;
                }
                const int f = open(js.c_str(), O_RDONLY | O_NONBLOCK);
                if (f < 0) {
                    continue;
                }
                joysticks.emplace_back(Joystick(f));
                joysticks.back().path = js;
                added.push_back(f);
                changed = true;
            }
        }
        if (!changed) {
            return;
        }
        checkUniqueNames();
        updateControllerSnapshot();
        for (int f : added) {
            std::string joyName;
            {
                std::lock_guard<std::mutex> lock(joysticksLock);
                for (const auto &j : joysticks) {
                    if (j.file == f) {
                        joyName = j.name;
                    }
                }
            }
            std::function<bool(int)> cb = joystickCallback(joyName);
            EPollManager::INSTANCE.addFileDescriptor(f, cb);
            ownFds.insert(f);
            LogInfo(VB_PLUGIN, "FPP Arcade: controller %s connected\n", joyName.c_str());
        }
    }
#endif

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
        addOwnedCommand(new FPPArcadeSpeakerVolumeCommand());

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
                        joysticks.back().path = js;
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
            callbacks[a.first] = joystickCallback(a.second);
        }
        // Controllers come and go - a wireless one's receiver can drop off the
        // USB bus and come straight back as a new device, and one can be
        // plugged in after fppd starts. Opened only at startup, a controller
        // that did that was dead to the arcade until fppd restarted (it looked
        // frozen). Every two seconds, drop the ones that have gone and open
        // any new /dev/input/jsN; one that comes back has the same name, so its
        // mapping carries on.
        Timers::INSTANCE.addPeriodicTimer("ArcadeJoystickWatch", 2000, [this]() { watchJoysticks(); });
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
    
    // ChannelDataPlugin: runs on the output thread as each frame's sequence
    // data arrives, before overlays are laid on. A game whose overlay is
    // Transparent shows the sequence through its black LEDs; this dims that
    // sequence to the game's Sequence brightness while it runs, fading over a
    // second each way. The games and models are only ever try_locked, so the
    // output thread never waits on a button press: a frame that can't look
    // keeps the last frame's targets.
    void modifySequenceData(int ms, uint8_t *seqData) override {
        std::unique_lock<std::recursive_mutex> l(gamesLock, std::try_to_lock);
        if (l.owns_lock()) {
            std::map<std::string, double> want;
            for (auto &mg : games) {
                for (FPPArcadeGame *g : mg.second) {
                    const int pct = g->sequenceBrightness();
                    if (pct >= 100) {
                        continue;
                    }
                    const std::string &name = g->getModelName();
                    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(name);
                    if (!m || m->getType() != "Channel") {
                        continue;   // only a plain channel range can be dimmed
                    }
                    bool running;
                    std::unique_lock<std::recursive_mutex> el(m->getRunningEffectMutex(), std::try_to_lock);
                    if (el.owns_lock()) {
                        running = g->isRunning();
                    } else {
                        auto d = seqDims.find(name);
                        running = d != seqDims.end() && d->second.want <= pct;
                    }
                    if (running) {
                        want[name] = want.count(name) ? std::min(want[name], (double)pct) : pct;
                        SeqDim &d = seqDims[name];
                        d.start = m->getStartChannel();
                        d.count = m->getChannelCount();
                    }
                }
            }
            for (auto &d : seqDims) {
                d.second.want = want.count(d.first) ? want[d.first] : 100.0;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const double dt = seqDimAt.time_since_epoch().count() ? std::chrono::duration<double, std::milli>(now - seqDimAt).count() : 0.0;
        seqDimAt = now;
        for (auto it = seqDims.begin(); it != seqDims.end();) {
            SeqDim &d = it->second;
            const double step = 100.0 * dt / 1000.0;
            d.level = d.level > d.want ? std::max(d.want, d.level - step) : std::min(d.want, d.level + step);
            if (d.level >= 100.0 && d.want >= 100.0) {
                it = seqDims.erase(it);
                continue;
            }
            const int pct = (int)std::lround(d.level);
            if (d.start >= 0 && d.count > 0 && pct < 100) {
                uint8_t *p = seqData + d.start;
                for (int i = 0; i < d.count; ++i) {
                    p[i] = (uint8_t)(p[i] * pct / 100);
                }
            }
            ++it;
        }
    }

    // Dimming in progress, by model; the output thread's own.
    struct SeqDim {
        int start = -1, count = 0;
        double want = 100.0, level = 100.0;
    };
    std::map<std::string, SeqDim> seqDims;
    std::chrono::steady_clock::time_point seqDimAt;

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
        Joystick(Joystick &&j) : file(j.file), name(j.name), numButtons(j.numButtons), numAxis(j.numAxis), controller(j.controller), joystickId(j.joystickId),
                                 path(j.path), dead(j.dead) {
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
        std::string path;    // /dev/input/jsN (Linux)
        bool dead = false;   // its device has gone; watchJoysticks() closes it
    };
    
    std::list<Joystick> joysticks;
    // Descriptors this plugin gave FPP's epoll itself (controllers that came
    // later), so shutdown() can take them back; FPP withdraws the startup ones.
    std::set<int> ownFds;
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
