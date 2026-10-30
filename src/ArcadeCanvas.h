#pragma once
// A float RGB buffer the size of the model, for games that build a frame by
// adding light (glows, fades, things drawn over each other) and then write it
// out in one pass. Values are 0..1 per channel; anything outside is clamped
// when written, so a layer can also darken by adding a negative amount.

#include <algorithm>
#include <cmath>
#include <vector>

#include "overlays/PixelOverlayModel.h"

namespace ArcadeCanvas {

struct RGB {
    float r, g, b;
};
inline RGB mix(const RGB &a, const RGB &b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
const RGB WHITE = {1.0f, 1.0f, 1.0f};

class Canvas {
public:
    void resize(int w, int h) {
        W = w;
        H = h;
        R.assign(W * H, 0.0f);
        G.assign(W * H, 0.0f);
        B.assign(W * H, 0.0f);
    }
    void clear() {
        std::fill(R.begin(), R.end(), 0.0f);
        std::fill(G.begin(), G.end(), 0.0f);
        std::fill(B.begin(), B.end(), 0.0f);
    }
    // Off-canvas pixels are ignored.
    void add(int x, int y, const RGB &c, float k = 1.0f) {
        if (x < 0 || y < 0 || x >= W || y >= H) {
            return;
        }
        const int i = y * W + x;
        R[i] += c.r * k;
        G[i] += c.g * k;
        B[i] += c.b * k;
    }
    void set(int x, int y, const RGB &c, float k = 1.0f) {
        if (x < 0 || y < 0 || x >= W || y >= H) {
            return;
        }
        const int i = y * W + x;
        R[i] = c.r * k;
        G[i] = c.g * k;
        B[i] = c.b * k;
    }
    RGB get(int x, int y) const {
        if (x < 0 || y < 0 || x >= W || y >= H) {
            return {0.0f, 0.0f, 0.0f};
        }
        const int i = y * W + x;
        return {R[i], G[i], B[i]};
    }
    void scale(float k) {
        for (int i = 0; i < W * H; ++i) {
            R[i] *= k;
            G[i] *= k;
            B[i] *= k;
        }
    }
    void writeTo(PixelOverlayModel *m, float brightness = 1.0f) const {
        const float k = 255.0f * brightness;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int i = y * W + x;
                m->setOverlayPixelValue(x, y, std::clamp((int)(R[i] * k + 0.5f), 0, 255),
                                        std::clamp((int)(G[i] * k + 0.5f), 0, 255),
                                        std::clamp((int)(B[i] * k + 0.5f), 0, 255));
            }
        }
    }
    int W = 0, H = 0;

private:
    std::vector<float> R, G, B;
};

}  // namespace ArcadeCanvas
