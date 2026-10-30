// FPP Arcade host: the plugin's games on a Mac, without FPP or the wall.
//
// It builds the plugin's own game sources unchanged (src/ - everything but
// FPPArcade.cpp, which is FPP's plugin machinery) against small stand-ins for
// the FPP headers they use (tools/host/stub/), runs them on an overlay model
// as FPP does, and shows the model in a window. Keys or a game controller
// play; the pixels also go out as sACN, in the show matrix's channel order,
// for the Matrix Viewer to draw on any wall layout, and the sounds stream to
// the viewer as the plugin's "Stream to" setting sends them from FPP.
//
// See tools/host/README.md.

#include <fpp-pch.h>   // first, as in every plugin source

#include <arpa/inet.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <list>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "FPPArcade.h"
#include "ArcadeSound.h"
#include "ArcadeDemo.h"
#include "HostJson.h"
#include "Timers.h"
#include "common.h"
#include "overlays/PixelOverlay.h"

PixelOverlayManager PixelOverlayManager::INSTANCE;
Timers Timers::INSTANCE;

namespace {

// The plugin's folder: the host runs from tools/host/build*/ inside it.
std::string repoDir;

void findRepo() {
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    char real[PATH_MAX];
    if (_NSGetExecutablePath(path, &size) == 0 && realpath(path, real)) {
        std::string p = real;
        for (int up = 0; up < 4 && !p.empty(); ++up) {   // arcade-host, build, host, tools
            p = p.substr(0, p.find_last_of('/'));
        }
        repoDir = p;
    } else {
        repoDir = ".";
    }
}

// Every game, in the order FPP Arcade lists them.
const char *const GAMES[] = {"Pong", "Snake", "Breakout", "Tetris", "Frogger", "Orb Pop", "Asteroid Orbit",
                             "Maze Chase", "Barrel Climb", "Star Defenders", "Shape Fit", "Stacker", "Flap",
                             "Light Memory", "Breakout Versus", "Stick Bridge", "Sky Jump", "Reindeer Run",
                             "Road Race", "Snowball Fight"};

std::string dataDir() {
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/Library/Application Support/FPP Arcade Host";
}

struct Options {
    std::string model = "Matrix_-_Interactive";
    int width = 60, height = 25;
    std::string game = "Tetris";
    std::string config;                  // plugin.fpp-arcade.json to take the games' settings from
    std::vector<std::pair<std::string, std::string>> opts;   // --opt Name=Value
    std::string theme = "date";
    bool sacn = true;
    std::string sacnHost = "127.0.0.1";
    int universe = 1, strandsPerString = 10;
    bool sound = true;
    std::string soundHost = "127.0.0.1";
    int soundPort = 5570;
    int cell = 16;
    int demoIdle = 0, demoBetween = 0;   // minutes, as the Demos settings
    // Headless runs, for checks: no window; presses at given times; the last
    // frame printed as text.
    bool headless = false;
    int runMs = 0;
    std::vector<std::pair<int, std::string>> presses;   // ms, button
};

void usage() {
    std::fprintf(stderr,
                 "usage: arcade-host [options]\n"
                 "  --game NAME          the game shown first (default Tetris)\n"
                 "  --size WxH           the model's size (default 60x25)\n"
                 "  --model NAME         the model's name (default Matrix_-_Interactive)\n"
                 "  --config FILE        take the games and their settings from a plugin.fpp-arcade.json\n"
                 "  --opt NAME=VALUE     set a game option, e.g. --opt \"End Show=2\" (repeatable)\n"
                 "  --theme T            end shows: date, christmas, halloween or everyday (default date)\n"
                 "  --sacn HOST          send sACN there (default 127.0.0.1); --no-sacn for none\n"
                 "  --universe N         first sACN universe (default 1)\n"
                 "  --strands N          strands per string of the show matrix (default 10)\n"
                 "  --sound-host HOST    stream the sounds there (default 127.0.0.1); --no-sound for none\n"
                 "  --sound-port N       (default 5570, the Matrix Viewer's)\n"
                 "  --cell N             LED size in the window, in points (default 16)\n"
                 "  --demos IDLE[,GAP]   play saved demos after IDLE minutes idle, GAP minutes apart; D saves the last run\n"
                 "  --headless --run-ms N --press MS:BUTTON ...   run without a window and print the last frame\n");
}

bool parseArgs(int argc, char **argv, Options &o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--game") {
            o.game = next();
        } else if (a == "--size") {
            if (std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height) != 2 || o.width < 8 || o.height < 8) {
                std::fprintf(stderr, "--size wants WxH, such as 60x25\n");
                return false;
            }
        } else if (a == "--model") {
            o.model = next();
        } else if (a == "--config") {
            o.config = next();
        } else if (a == "--opt") {
            const std::string v = next();
            const size_t eq = v.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "--opt wants NAME=VALUE\n");
                return false;
            }
            o.opts.push_back({v.substr(0, eq), v.substr(eq + 1)});
        } else if (a == "--theme") {
            o.theme = next();
        } else if (a == "--sacn") {
            o.sacnHost = next();
        } else if (a == "--no-sacn") {
            o.sacn = false;
        } else if (a == "--universe") {
            o.universe = std::max(1, std::atoi(next().c_str()));
        } else if (a == "--strands") {
            o.strandsPerString = std::max(1, std::atoi(next().c_str()));
        } else if (a == "--sound-host") {
            o.soundHost = next();
        } else if (a == "--sound-port") {
            o.soundPort = std::atoi(next().c_str());
        } else if (a == "--no-sound") {
            o.sound = false;
        } else if (a == "--cell") {
            o.cell = std::max(2, std::atoi(next().c_str()));
        } else if (a == "--demos") {
            const std::string v = next();
            o.demoIdle = std::max(0, std::atoi(v.c_str()));
            const size_t c = v.find(',');
            o.demoBetween = c == std::string::npos ? 0 : std::max(0, std::atoi(v.c_str() + c + 1));
        } else if (a == "--headless") {
            o.headless = true;
        } else if (a == "--run-ms") {
            o.runMs = std::atoi(next().c_str());
        } else if (a == "--press") {
            const std::string v = next();
            const size_t c = v.find(':');
            if (c == std::string::npos) {
                std::fprintf(stderr, "--press wants MS:BUTTON, such as \"500:Start - Pressed\"\n");
                return false;
            }
            o.presses.push_back({std::atoi(v.substr(0, c).c_str()), v.substr(c + 1)});
        } else {
            usage();
            return false;
        }
    }
    return true;
}

// The games for the model: every one, or the ones in a plugin config for this
// model, with --opt settings on top.
std::vector<Json::Value> gameConfigs(const Options &o) {
    std::vector<Json::Value> out;
    if (!o.config.empty()) {
        Json::Value root;
        if (!LoadJsonFromFile(o.config, root)) {
            std::fprintf(stderr, "Can't read %s\n", o.config.c_str());
            std::exit(1);
        }
        for (unsigned k = 0; k < root["games"].size(); ++k) {
            Json::Value g = root["games"][k];
            if (g["model"].asString() == o.model && g.get("enabled", true).asBool()) {
                out.push_back(g);
            }
        }
        if (out.empty()) {
            std::fprintf(stderr, "%s has no enabled games for %s\n", o.config.c_str(), o.model.c_str());
            std::exit(1);
        }
    } else {
        for (const char *name : GAMES) {
            Json::Value g(Json::objectValue);
            g["game"] = name;
            g["model"] = o.model;
            g["overlay"] = "Overwrite";
            g["options"] = Json::Value(Json::arrayValue);
            out.push_back(g);
        }
    }
    for (auto &g : out) {
        for (auto &[name, value] : o.opts) {
            bool set = false;
            for (unsigned k = 0; k < g["options"].size(); ++k) {
                if (g["options"][k]["name"].asString() == name) {
                    g["options"][k]["value"] = value;
                    set = true;
                }
            }
            if (!set) {
                Json::Value opt(Json::objectValue);
                opt["name"] = name;
                opt["value"] = value;
                g["options"].append(opt);
            }
        }
    }
    return out;
}

// The model's pixels in the show matrix's channel order, as xLights numbers a
// vertical matrix starting bottom right with zig-zag strands: node n is on
// strand n / H counted from the right, going up on the first strand of each
// string, then down, and so on.
void toChannels(const std::vector<uint8_t> &px, int W, int H, int strandsPerString, std::vector<uint8_t> &out) {
    out.assign((size_t)W * H * 3, 0);
    if (px.empty()) {
        return;
    }
    for (int n = 0; n < W * H; ++n) {
        const int strand = n / H, p = n % H, segment = strand % strandsPerString;
        const int col = W - 1 - strand;
        const int up = segment % 2 == 0 ? p : H - 1 - p;   // from the bottom
        const int row = H - 1 - up;
        std::memcpy(&out[(size_t)n * 3], &px[((size_t)row * W + col) * 3], 3);
    }
}

// sACN (E1.31): 510 channels a universe, from the first.
class SacnSender {
public:
    bool open(const std::string &host, int firstUniverse) {
        universe = firstUniverse;
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        dest.sin_family = AF_INET;
        dest.sin_port = htons(5568);
        if (sock < 0 || inet_pton(AF_INET, host.c_str(), &dest.sin_addr) != 1) {
            std::fprintf(stderr, "sACN: can't send to %s (an IP address is needed)\n", host.c_str());
            return false;
        }
        std::mt19937 r((unsigned)std::time(nullptr));
        for (auto &b : cid) {
            b = (uint8_t)r();
        }
        return true;
    }
    void send(const std::vector<uint8_t> &channels) {
        if (sock < 0) {
            return;
        }
        for (size_t off = 0, u = universe; off < channels.size(); off += 510, ++u) {
            const size_t n = std::min<size_t>(510, channels.size() - off);
            packet(u, &channels[off], n);
        }
        ++seq;
    }

private:
    void packet(size_t u, const uint8_t *data, size_t n) {
        std::vector<uint8_t> p;
        auto u8 = [&](uint8_t v) { p.push_back(v); };
        auto u16 = [&](uint16_t v) { u8(v >> 8); u8(v & 0xFF); };
        auto u32 = [&](uint32_t v) { u16(v >> 16); u16(v & 0xFFFF); };
        const size_t slots = n + 1, dmp = 10 + slots, framing = 77 + dmp, root = 22 + framing;
        u16(0x0010);
        u16(0x0000);
        const char id[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
        p.insert(p.end(), id, id + 12);
        u16(0x7000 | root);
        u32(4);
        p.insert(p.end(), cid, cid + 16);
        u16(0x7000 | framing);
        u32(2);
        char name[64] = "FPP Arcade host";
        p.insert(p.end(), name, name + 64);
        u8(100);        // priority
        u16(0);         // sync address
        u8(seq);
        u8(0);          // options
        u16((uint16_t)u);
        u16(0x7000 | dmp);
        u8(2);
        u8(0xA1);
        u16(0);
        u16(1);
        u16((uint16_t)slots);
        u8(0);          // start code
        p.insert(p.end(), data, data + n);
        sendto(sock, p.data(), p.size(), 0, (const sockaddr *)&dest, sizeof(dest));
    }

    int sock = -1;
    sockaddr_in dest{};
    uint8_t cid[16];
    size_t universe = 1;
    uint8_t seq = 0;
};

// Keys, as the wall's controllers press them.
const char *keyButton(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return "Up";
    case SDLK_DOWN: return "Down";
    case SDLK_LEFT: return "Left";
    case SDLK_RIGHT: return "Right";
    case SDLK_Z: return "A Button";
    case SDLK_X: return "B Button";
    case SDLK_RETURN:
    case SDLK_SPACE: return "Start";
    case SDLK_TAB:
    case SDLK_BACKSPACE: return "Select";
    default: return nullptr;
    }
}

const char *padButton(Uint8 b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "Right";
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A Button";
    case SDL_GAMEPAD_BUTTON_EAST: return "B Button";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_BACK: return "Select";
    default: return nullptr;
    }
}

int64_t nowMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

// The last frame as text: '#' lit, '.' dark.
void printFrame(const std::vector<uint8_t> &px, int W, int H) {
    for (int y = 0; y < H; ++y) {
        std::string row;
        for (int x = 0; x < W; ++x) {
            const uint8_t *p = px.empty() ? nullptr : &px[((size_t)y * W + x) * 3];
            row += p && (p[0] | p[1] | p[2]) ? '#' : '.';
        }
        std::printf("%s\n", row.c_str());
    }
}

}  // namespace

// FPP's media folder: this checkout stands in for the plugin's own folder,
// and dataDir() for its data (best scores, your own sound files).
std::string hostMediaPath(const std::string &sub) {
    const std::string plugin = "/plugins/fpp-arcade";
    if (sub.compare(0, plugin.size(), plugin) == 0) {
        return repoDir + sub.substr(plugin.size());
    }
    const std::string data = "/plugindata/fpp-arcade";
    if (sub.compare(0, data.size(), data) == 0) {
        return dataDir() + sub.substr(data.size());
    }
    return dataDir() + sub;
}

int main(int argc, char **argv) {
    findRepo();
    Options o;
    if (!parseArgs(argc, argv, o)) {
        return 2;
    }
    std::string mk = "mkdir -p \"" + dataDir() + "\"";
    if (std::system(mk.c_str()) != 0) {
        std::fprintf(stderr, "Can't make %s\n", dataDir().c_str());
    }

    arcadeSetShowTheme(o.theme);
    Json::Value sound(Json::objectValue);
    sound["enabled"] = o.sound;
    sound["device"] = "none";
    sound["volume"] = 80;
    sound["streamHost"] = o.soundHost;
    sound["streamPort"] = o.soundPort;
    ArcadeSound::configure(sound);

    PixelOverlayModel *model = PixelOverlayManager::INSTANCE.addModel(o.model, o.width, o.height);
    std::vector<Json::Value> configs = gameConfigs(o);
    std::map<std::string, std::list<FPPArcadeGame *>> allGames;   // as the plugin keeps them, by model
    std::list<FPPArcadeGame *> &games = allGames[o.model];
    ArcadeDemo::configure(o.demoIdle, o.demoBetween, 60);
    int idx = 1;
    for (auto &c : configs) {
        FPPArcadeGame *g = arcadeCreateGame(c);
        if (!g) {
            std::fprintf(stderr, "No game called \"%s\"\n", c["game"].asString().c_str());
            continue;
        }
        g->setIdx(idx++);
        games.push_back(g);
    }
    // The --game one in front, as if Select had got to it.
    for (size_t k = 0; k < games.size() && games.front()->getName() != o.game; ++k) {
        games.push_back(games.front());
        games.pop_front();
    }
    if (games.empty() || games.front()->getName() != o.game) {
        std::fprintf(stderr, "No game called \"%s\". The games:", o.game.c_str());
        for (const char *n : GAMES) {
            std::fprintf(stderr, " \"%s\"", n);
        }
        std::fprintf(stderr, "\n");
        return 2;
    }

    SacnSender sacn;
    if (o.sacn && !sacn.open(o.sacnHost, o.universe)) {
        o.sacn = false;
    }

    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    if (!o.headless) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
            std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
            return 1;
        }
        win = SDL_CreateWindow("FPP Arcade host", o.width * o.cell, o.height * o.cell,
                               SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
        ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
        if (!ren) {
            std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
            return 1;
        }
        std::fprintf(stderr,
                     "FPP Arcade host: %s on %s (%dx%d)%s%s\n"
                     "  Arrows move, Z = A, X = B, Return/Space = Start, Tab = Select (next game), D saves the last run as its demo, Esc quits.\n",
                     o.game.c_str(), o.model.c_str(), o.width, o.height,
                     o.sacn ? ", sACN to " : "", o.sacn ? o.sacnHost.c_str() : "");
    }

    const auto t0 = std::chrono::steady_clock::now();
    arcadeShowTitle(games.front());
    std::vector<uint8_t> channels;
    std::vector<SDL_Gamepad *> pads;
    size_t nextPress = 0;
    int64_t nextOutput = 0;
    bool quit = false;
    auto press = [&](const std::string &button) {
        if (button == "Save Demo") {   // D, or --press MS:Save Demo
            std::string err;
            const ArcadeDemo::Info last = ArcadeDemo::lastRun(o.model);
            if (ArcadeDemo::saveLastRun(o.model, err)) {
                std::fprintf(stderr, "Saved the last run (%s, %.0f s) as its demo\n", last.game.c_str(), last.seconds);
            } else {
                std::fprintf(stderr, "%s\n", err.c_str());
            }
            return;
        }
        arcadeButton(games, button);
    };
    int64_t nextDemoTick = 0;

    while (!quit) {
        const int64_t now = nowMs(t0);
        if (o.headless) {
            while (nextPress < o.presses.size() && o.presses[nextPress].first <= now) {
                press(o.presses[nextPress++].second);
            }
            if (now >= o.runMs) {
                quit = true;
            }
        } else {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                case SDL_EVENT_QUIT: quit = true; break;
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                    if (e.key.key == SDLK_ESCAPE || (e.key.key == SDLK_Q && (e.key.mod & SDL_KMOD_GUI))) {
                        quit = true;
                    } else if (!e.key.repeat && e.key.key == SDLK_D) {
                        if (e.type == SDL_EVENT_KEY_DOWN) {
                            press("Save Demo");
                        }
                    } else if (!e.key.repeat) {
                        if (const char *b = keyButton(e.key.key)) {
                            press(std::string(b) + (e.type == SDL_EVENT_KEY_DOWN ? " - Pressed" : " - Released"));
                        }
                    }
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (SDL_Gamepad *c = SDL_OpenGamepad(e.gdevice.which)) {
                        pads.push_back(c);
                        std::fprintf(stderr, "Controller: %s\n", SDL_GetGamepadName(c));
                    }
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    if (const char *b = padButton(e.gbutton.button)) {
                        press(std::string(b) + (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? " - Pressed" : " - Released"));
                    }
                    break;
                default: break;
                }
            }
        }

        Timers::INSTANCE.run(now);
        if (now >= nextDemoTick) {
            nextDemoTick = now + 40;   // as the plugin's timer
            ArcadeDemo::tick(allGames);
        }
        model->hostRun(now);
        if (now >= nextOutput) {
            // FPP's output step, 40 times a second.
            nextOutput = now + 25;
            model->hostOutput();
            const std::vector<uint8_t> &px = model->hostShown();
            if (o.sacn) {
                toChannels(px, o.width, o.height, o.strandsPerString, channels);
                sacn.send(channels);
            }
            if (ren) {
                int ow, oh;
                SDL_GetCurrentRenderOutputSize(ren, &ow, &oh);
                const float cell = std::min((float)ow / o.width, (float)oh / o.height);
                const float x0 = (ow - cell * o.width) / 2, y0 = (oh - cell * o.height) / 2;
                const float led = std::max(1.0f, cell * 0.72f);
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
                SDL_RenderClear(ren);
                for (int y = 0; y < o.height; ++y) {
                    for (int x = 0; x < o.width; ++x) {
                        const uint8_t *p = px.empty() ? nullptr : &px[((size_t)y * o.width + x) * 3];
                        if (p && (p[0] | p[1] | p[2])) {
                            SDL_SetRenderDrawColor(ren, p[0], p[1], p[2], 255);
                        } else {
                            SDL_SetRenderDrawColor(ren, 28, 28, 32, 255);   // an unlit LED
                        }
                        SDL_FRect r = {x0 + x * cell + (cell - led) / 2, y0 + y * cell + (cell - led) / 2, led, led};
                        SDL_RenderFillRect(ren, &r);
                    }
                }
                SDL_RenderPresent(ren);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (o.headless) {
        printFrame(model->hostShown(), o.width, o.height);
    }
    for (FPPArcadeGame *g : games) {
        if (g->isRunning()) {
            g->stop();
        }
    }
    model->hostRun(nowMs(t0) + 100);   // let the clear run and the effects go
    ArcadeSound::shutdown();
    for (FPPArcadeGame *g : games) {
        g->release();
        delete g;
    }
    for (SDL_Gamepad *c : pads) {
        SDL_CloseGamepad(c);
    }
    if (ren) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
    }
    return 0;
}
