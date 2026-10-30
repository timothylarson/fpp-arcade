#include <fpp-pch.h>

#include "ArcadeDemo.h"
#include "FPPArcade.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <sys/stat.h>

#include "common.h"
#include "settings.h"
#include "log.h"
#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

namespace ArcadeDemo {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int SAMPLE_MS = 40;         // 25 pictures a second
constexpr double MAX_SECS = 180.0;    // a long run keeps its last three minutes, GAME OVER and all
constexpr double MIN_SECS = 5.0;      // anything shorter isn't worth showing
const char MAGIC[] = "FPPARCADEDEMO1\n";

// One picture: how long it was up (ms), and its pixels run-length coded -
// runs of up to 255 of the same colour as [count, r, g, b] - which makes a
// mostly black wall a few hundred bytes.
struct Frame {
    uint32_t ms = 0;
    std::string rle;
};
struct Recording {
    std::string game;
    int w = 0, h = 0;
    std::deque<Frame> frames;
    double secs = 0;
};
struct ModelState {
    Recording cur, last;
    Clock::time_point lastSample, lastInput = Clock::now(), demoEnded;
    bool sampled = false, demoOn = false, everDemo = false;
    std::string lastDemo;   // the game whose demo played last, for taking turns
    Clock::time_point lastCheck;
};

std::recursive_mutex lock;
std::map<std::string, ModelState> models;
int idleMinutes = 0, betweenMinutes = 0;

bool kinectGame(const std::string &g) { return g == "Orb Pop" || g == "Shape Fit"; }

std::string safeName(std::string s) {
    for (char &c : s) {
        if (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 32) {
            c = '_';
        }
    }
    if (s.empty() || s[0] == '.') {
        s = "_" + s;
    }
    return s;
}
std::string dirFor(const std::string &model) { return FPP_DIR_MEDIA("/plugindata/fpp-arcade/demos/") + safeName(model); }
std::string fileFor(const std::string &model, const std::string &game) { return dirFor(model) + "/" + safeName(game) + ".demo"; }

std::string encode(PixelOverlayModel *m, int w, int h) {
    std::string o;
    int n = 0, pr = -1, pg = -1, pb = -1;
    auto flush = [&]() {
        if (n) {
            o += (char)n;
            o += (char)pr;
            o += (char)pg;
            o += (char)pb;
        }
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int r, g, b;
            m->getOverlayPixelValue(x, y, r, g, b);
            if (r == pr && g == pg && b == pb && n < 255) {
                ++n;
            } else {
                flush();
                pr = r;
                pg = g;
                pb = b;
                n = 1;
            }
        }
    }
    flush();
    return o;
}

void decode(PixelOverlayModel *m, int w, int h, const std::string &rle) {
    int i = 0;
    for (size_t k = 0; k + 4 <= rle.size(); k += 4) {
        const int n = (unsigned char)rle[k];
        const int r = (unsigned char)rle[k + 1], g = (unsigned char)rle[k + 2], b = (unsigned char)rle[k + 3];
        for (int j = 0; j < n && i < w * h; ++j, ++i) {
            m->setOverlayPixelValue(i % w, i / w, r, g, b);
        }
    }
}

// --------------------------------------------------------------- the file
void put32(std::string &o, uint32_t v) {
    for (int k = 0; k < 4; ++k) o += (char)((v >> (8 * k)) & 255);
}
bool get32(std::istream &in, uint32_t &v) {
    unsigned char b[4];
    if (!in.read((char *)b, 4)) return false;
    v = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

// The header: MAGIC, then width, height, frame count, total ms and the game's
// name (length first); then each frame's ms, length and bytes.
bool readHeader(std::istream &in, Recording &r, uint32_t &count, double &secs) {
    char magic[sizeof(MAGIC) - 1];
    uint32_t w, h, ms, len;
    if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, MAGIC, sizeof(magic)) != 0 ||
        !get32(in, w) || !get32(in, h) || !get32(in, count) || !get32(in, ms) || !get32(in, len) || len > 200) {
        return false;
    }
    r.game.resize(len);
    if (len && !in.read(&r.game[0], len)) return false;
    r.w = (int)w;
    r.h = (int)h;
    secs = ms / 1000.0;
    return true;
}
bool load(const std::string &file, Recording &r) {
    std::ifstream in(file, std::ios::binary);
    uint32_t count;
    double secs;
    if (!readHeader(in, r, count, secs) || count > 100000) {
        return false;
    }
    r.secs = secs;
    for (uint32_t i = 0; i < count; ++i) {
        Frame f;
        uint32_t len;
        if (!get32(in, f.ms) || !get32(in, len) || len > 1u << 20) return false;
        f.rle.resize(len);
        if (len && !in.read(&f.rle[0], len)) return false;
        r.frames.push_back(std::move(f));
    }
    return true;
}
bool save(const std::string &model, const Recording &r) {
    mkdir(FPP_DIR_MEDIA("/plugindata").c_str(), 0775);
    mkdir(FPP_DIR_MEDIA("/plugindata/fpp-arcade").c_str(), 0775);
    mkdir(FPP_DIR_MEDIA("/plugindata/fpp-arcade/demos").c_str(), 0775);
    mkdir(dirFor(model).c_str(), 0775);
    std::string o(MAGIC, sizeof(MAGIC) - 1);
    put32(o, r.w);
    put32(o, r.h);
    put32(o, (uint32_t)r.frames.size());
    put32(o, (uint32_t)(r.secs * 1000));
    put32(o, (uint32_t)r.game.size());
    o += r.game;
    for (const auto &f : r.frames) {
        put32(o, f.ms);
        put32(o, (uint32_t)f.rle.size());
        o += f.rle;
    }
    const std::string file = fileFor(model, r.game), tmp = file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.write(o.data(), o.size())) {
            return false;
        }
    }
    return std::rename(tmp.c_str(), file.c_str()) == 0;
}

// --------------------------------------------------------------- playing one
class DemoEffect : public RunningEffect {
public:
    DemoEffect(PixelOverlayModel *m, Recording &&r) : RunningEffect(m), rec(std::move(r)) {}

    const std::string &name() const override {
        static std::string NAME = "ArcadeDemo";
        return NAME;
    }
    const std::string &game() const { return rec.game; }
    std::atomic<bool> stopNow{false};   // ends at its next frame

    int32_t update() override {
        if (ending) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return EFFECT_DONE;
        }
        if (next >= rec.frames.size() || stopNow) {
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            ending = true;
            return EFFECT_AFTER_NEXT_OUTPUT;
        }
        const Frame &f = rec.frames[next++];
        decode(model, rec.w, rec.h, f.rle);
        // PRESS START for three seconds of every fifteen, from the fourth.
        const int cycle = (int)(playedMs % 15000);
        if (cycle >= 4000 && cycle < 7000) {
            pressStart();
        }
        model->flushOverlayBuffer();
        const uint32_t ms = std::clamp<uint32_t>(f.ms, 10, 2000);
        playedMs += ms;
        return (int32_t)ms;
    }

private:
    // A black box with PRESS START (or PRESS over START, if narrow) in yellow.
    void pressStart() {
        std::vector<std::string> lines = {"PRESS START"};
        if ((int)lines[0].size() * 4 + 1 > rec.w) {
            lines = {"PRESS", "START"};
        }
        int tw = 0;
        for (const auto &l : lines) tw = std::max(tw, (int)l.size() * 4 - 1);
        const int th = (int)lines.size() * 6 - 1;
        const int x0 = (rec.w - tw) / 2, y0 = (rec.h - th) / 2;
        for (int y = y0 - 1; y <= y0 + th; ++y) {
            for (int x = x0 - 1; x <= x0 + tw; ++x) {
                model->setOverlayPixelValue(x, y, 0, 0, 0);
            }
        }
        for (size_t li = 0; li < lines.size(); ++li) {
            const std::string &l = lines[li];
            const int lx = (rec.w - ((int)l.size() * 4 - 1)) / 2, ly = y0 + (int)li * 6;
            for (size_t i = 0; i < l.size(); ++i) {
                for (int py = 0; py < 5; ++py) {
                    for (int px = 0; px < 3; ++px) {
                        if (arcadeGlyph(l[i], px, py)) {
                            model->setOverlayPixelValue(lx + (int)i * 4 + px, ly + py, 255, 220, 0);
                        }
                    }
                }
            }
        }
    }

    Recording rec;
    size_t next = 0;
    uint64_t playedMs = 0;
    bool ending = false;
};

bool startDemo(PixelOverlayModel *m, const std::string &model, const std::string &game, const std::string &overlay) {
    Recording r;
    if (!load(fileFor(model, game), r) || r.frames.empty()) {
        return false;
    }
    if (r.w != m->getWidth() || r.h != m->getHeight()) {
        LogWarn(VB_PLUGIN, "FPP Arcade: the %s demo on %s is %dx%d, the model %dx%d; not played\n", game.c_str(),
                model.c_str(), r.w, r.h, m->getWidth(), m->getHeight());
        return false;
    }
    LogInfo(VB_PLUGIN, "FPP Arcade: playing the %s demo on %s\n", game.c_str(), model.c_str());
    m->setState(PixelOverlayState(overlay == "Transparent" ? PixelOverlayState::PixelState::TransparentRGB
                                                           : PixelOverlayState::PixelState::Enabled));
    m->setRunningEffect(new DemoEffect(m, std::move(r)), 1);
    return true;
}

// --------------------------------------------------------------- recording
void finishRun(ModelState &st) {
    if (!st.cur.game.empty() && st.cur.secs >= MIN_SECS) {
        st.last = std::move(st.cur);
    }
    st.cur = Recording();
    st.sampled = false;
}

void record(ModelState &st, PixelOverlayModel *m, const std::string &game, bool paused, Clock::time_point now) {
    if (game != st.cur.game) {
        finishRun(st);
        st.cur.game = game;
    }
    if (game.empty() || paused) {
        st.sampled = false;   // the time on the pause menu isn't kept
        return;
    }
    int dt = SAMPLE_MS;
    if (st.sampled) {
        dt = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - st.lastSample).count();
        if (dt < SAMPLE_MS - 5) {
            return;
        }
        dt = std::min(dt, 1000);
    }
    st.lastSample = now;
    st.sampled = true;
    const int w = m->getWidth(), h = m->getHeight();
    if (st.cur.frames.empty()) {
        st.cur.w = w;
        st.cur.h = h;
    }
    std::string rle = encode(m, w, h);
    Recording &r = st.cur;
    if (!r.frames.empty()) {
        r.frames.back().ms += dt;   // the last picture was up until now
        r.secs += dt / 1000.0;
        if (r.frames.back().rle == rle) {
            return;
        }
    }
    r.frames.push_back({0, std::move(rle)});
    while (r.secs > MAX_SECS && r.frames.size() > 1) {
        r.secs -= r.frames.front().ms / 1000.0;
        r.frames.pop_front();
    }
}

// --------------------------------------------------------------- taking turns
void schedule(ModelState &st, const std::string &model, PixelOverlayModel *m, std::list<FPPArcadeGame *> &list,
              bool free, Clock::time_point now) {
    if (st.demoOn) {
        return;
    }
    if (idleMinutes <= 0 || !free || now - st.lastInput < std::chrono::minutes(idleMinutes)) {
        return;
    }
    if (st.everDemo && now - st.demoEnded < std::chrono::minutes(betweenMinutes)) {
        return;
    }
    // The model's games in its list order (Select turns the list round).
    std::vector<FPPArcadeGame *> order(list.begin(), list.end());
    std::sort(order.begin(), order.end(), [](FPPArcadeGame *a, FPPArcadeGame *b) { return a->getIdx() < b->getIdx(); });
    std::vector<FPPArcadeGame *> have;
    for (FPPArcadeGame *g : order) {
        struct stat sb;
        if (!kinectGame(g->getName()) && stat(fileFor(model, g->getName()).c_str(), &sb) == 0) {
            have.push_back(g);
        }
    }
    if (have.empty()) {
        return;
    }
    size_t pick = 0;
    for (size_t i = 0; i < have.size(); ++i) {
        if (have[i]->getName() == st.lastDemo) {
            pick = (i + 1) % have.size();
        }
    }
    for (size_t k = 0; k < have.size(); ++k) {
        FPPArcadeGame *g = have[(pick + k) % have.size()];
        if (startDemo(m, model, g->getName(), g->overlayMode())) {
            st.lastDemo = g->getName();
            st.demoOn = st.everDemo = true;
            return;
        }
    }
}

}  // namespace

void configure(int idleMin, int betweenMin) {
    std::lock_guard<std::recursive_mutex> l(lock);
    idleMinutes = std::clamp(idleMin, 0, 24 * 60);
    betweenMinutes = std::clamp(betweenMin, 0, 24 * 60);
}

void noteInput(const std::string &model) {
    std::lock_guard<std::recursive_mutex> l(lock);
    models[model].lastInput = Clock::now();
}

void tick(std::map<std::string, std::list<FPPArcadeGame *>> &games) {
    std::lock_guard<std::recursive_mutex> l(lock);
    const auto now = Clock::now();
    for (auto &a : games) {
        if (a.second.empty()) {
            continue;
        }
        PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(a.first);
        if (!m) {
            continue;
        }
        ModelState &st = models[a.first];
        FPPArcadeGame *front = a.second.front();
        std::string game;
        bool paused = false, demo = false, free = false;
        {
            std::lock_guard<std::recursive_mutex> el(m->getRunningEffectMutex());
            RunningEffect *re = m->getRunningEffect();
            if (front->isRunning() && !kinectGame(front->getName())) {
                game = front->getName();
                FPPArcadeGameEffect *e = dynamic_cast<FPPArcadeGameEffect *>(re);
                paused = e && e->isPaused();
            }
            demo = re && re->name() == "ArcadeDemo";
            free = !re || re->name() == "KinectBlit";
            // Under the model's effect lock, which FPP holds while the game
            // draws, so a picture is never caught half drawn (the score not
            // on it yet).
            record(st, m, game, paused, now);
        }
        if (st.demoOn && !demo) {
            st.demoOn = false;   // it ended
            st.demoEnded = now;
        }
        if (now - st.lastCheck >= std::chrono::seconds(1)) {
            st.lastCheck = now;
            schedule(st, a.first, m, a.second, free, now);
        }
    }
}

std::string stopDemo(const std::string &model) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(model);
    if (!m) {
        return "";
    }
    std::string game;
    {
        std::lock_guard<std::recursive_mutex> el(m->getRunningEffectMutex());
        DemoEffect *d = dynamic_cast<DemoEffect *>(m->getRunningEffect());
        if (!d) {
            return "";
        }
        game = d->game();
        d->stopNow = true;   // and anything put on the model now replaces it
    }
    std::lock_guard<std::recursive_mutex> l(lock);   // after the model's: tick() takes them the other way round
    ModelState &st = models[model];
    st.demoOn = false;
    st.demoEnded = Clock::now();
    return game;
}

// The run to save: the one being played, so far, if there is one, else the
// last one finished.
static const Recording *runToSave(const std::string &model) {
    auto it = models.find(model);
    if (it == models.end()) {
        return nullptr;
    }
    if (!it->second.cur.game.empty() && it->second.cur.secs >= MIN_SECS) {
        return &it->second.cur;
    }
    return it->second.last.game.empty() ? nullptr : &it->second.last;
}

Info lastRun(const std::string &model) {
    std::lock_guard<std::recursive_mutex> l(lock);
    const Recording *r = runToSave(model);
    return r ? Info{r->game, r->secs} : Info{};
}

bool saveLastRun(const std::string &model, std::string &error) {
    std::lock_guard<std::recursive_mutex> l(lock);
    const Recording *r = runToSave(model);
    if (!r) {
        error = "Nothing has been played on " + model + " since FPP started.";
        return false;
    }
    if (!save(model, *r)) {
        error = "Couldn't write " + fileFor(model, r->game);
        return false;
    }
    LogInfo(VB_PLUGIN, "FPP Arcade: saved a %.0f s %s demo for %s\n", r->secs, r->game.c_str(), model.c_str());
    return true;
}

std::vector<Info> demos(const std::string &model) {
    std::vector<Info> out;
    DIR *d = opendir(dirFor(model).c_str());
    if (!d) {
        return out;
    }
    while (struct dirent *e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() < 6 || n.compare(n.size() - 5, 5, ".demo") != 0) {
            continue;
        }
        std::ifstream in(dirFor(model) + "/" + n, std::ios::binary);
        Recording r;
        uint32_t count;
        double secs;
        if (readHeader(in, r, count, secs)) {
            out.push_back({r.game, secs});
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const Info &a, const Info &b) { return a.game < b.game; });
    return out;
}

bool remove(const std::string &model, const std::string &game) {
    stopDemo(model);
    return std::remove(fileFor(model, game).c_str()) == 0;
}

bool play(const std::string &model, const std::string &game, const std::string &overlay) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(model);
    if (!m) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> l(lock);
    if (!startDemo(m, model, game, overlay)) {
        return false;
    }
    ModelState &st = models[model];
    st.demoOn = st.everDemo = true;
    st.lastDemo = game;
    return true;
}

}  // namespace ArcadeDemo
