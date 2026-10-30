#pragma once
// Stands in for FPP's overlays/PixelOverlayModel.h in tools/host: a model's
// overlay buffer, its running effect and when it next runs, and what goes
// out. Like FPP, a frame goes out when it's flushed, or at the next output
// if it's marked dirty; and an effect replaced mid-update() is only deleted
// once that update() has returned.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "overlays/PixelOverlayEffects.h"

class PixelOverlayState {
public:
    enum PixelState { Disabled, Enabled, Transparent, TransparentRGB };
    PixelOverlayState(PixelState s = Disabled) : state(s) {}
    PixelState getState() const { return state; }

private:
    PixelState state;
};

class PixelOverlayModel {
public:
    PixelOverlayModel(const std::string &n, int w, int h)
        : name(n), width(w), height(h), overlay((size_t)w * h * 3), shown((size_t)w * h * 3) {}
    ~PixelOverlayModel() {
        delete effect;
        for (RunningEffect *e : retired) {
            delete e;
        }
    }

    const std::string &getName() const { return name; }
    std::string getType() const { return "Channel"; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    void getSize(int &w, int &h) const {
        w = width;
        h = height;
    }
    int getStartChannel() const { return 0; }
    int getChannelCount() const { return width * height * 3; }

    void setState(const PixelOverlayState &s) { state = s; }
    PixelOverlayState getState() const { return state; }

    void setOverlayPixelValue(int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        uint8_t *p = &overlay[((size_t)y * width + x) * 3];
        p[0] = (uint8_t)std::min(255, std::max(0, r));
        p[1] = (uint8_t)std::min(255, std::max(0, g));
        p[2] = (uint8_t)std::min(255, std::max(0, b));
    }
    void getOverlayPixelValue(int x, int y, int &r, int &g, int &b) const {
        r = g = b = 0;
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const uint8_t *p = &overlay[((size_t)y * width + x) * 3];
        r = p[0];
        g = p[1];
        b = p[2];
    }
    void clearOverlayBuffer() { std::fill(overlay.begin(), overlay.end(), 0); }
    void flushOverlayBuffer() {
        shown = overlay;
        dirty = false;
    }
    void setOverlayBufferDirty(bool d) { dirty = d; }

    RunningEffect *getRunningEffect() const { return effect; }
    void setRunningEffect(RunningEffect *e, int firstUpdateMs) {
        std::lock_guard<std::recursive_mutex> l(effectMutex);
        if (effect && effect != e) {
            retired.push_back(effect);
        }
        effect = e;
        nextUpdateMs = nowMs + std::max(0, firstUpdateMs);
        afterOutput = false;
    }
    std::recursive_mutex &getRunningEffectMutex() { return effectMutex; }

    // ---- the host's side ----

    // Runs the effect if it's due, as FPP's overlay manager does.
    void hostRun(int64_t now) {
        nowMs = now;
        std::lock_guard<std::recursive_mutex> l(effectMutex);
        if (effect && !afterOutput && now >= nextUpdateMs) {
            RunningEffect *running = effect;
            const int32_t next = running->update();
            if (effect == running) {   // not replaced during its own update()
                if (next == EFFECT_DONE) {
                    retired.push_back(effect);
                    effect = nullptr;
                } else if (next == EFFECT_AFTER_NEXT_OUTPUT) {
                    afterOutput = true;
                } else {
                    nextUpdateMs = now + std::max<int32_t>(1, next);
                }
            }
        }
        for (RunningEffect *e : retired) {
            delete e;
        }
        retired.clear();
    }
    // The output step: a frame marked ready goes out, and an effect that
    // asked to wait for it runs next time round.
    void hostOutput() {
        if (dirty) {
            flushOverlayBuffer();
        }
        afterOutput = false;
    }
    // What the wall shows: nothing while the model is disabled.
    const std::vector<uint8_t> &hostShown() const {
        static const std::vector<uint8_t> none;
        return state.getState() == PixelOverlayState::Disabled ? none : shown;
    }

private:
    std::string name;
    int width, height;
    std::vector<uint8_t> overlay, shown;
    bool dirty = false;
    PixelOverlayState state;
    RunningEffect *effect = nullptr;
    std::vector<RunningEffect *> retired;
    std::recursive_mutex effectMutex;
    int64_t nowMs = 0, nextUpdateMs = 0;
    bool afterOutput = false;
};
