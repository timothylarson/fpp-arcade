#pragma once
// Stands in for FPP's PixelOverlayModel when previewing shows off the player.
#include <vector>
struct PixelOverlayModel {
    int w, h;
    std::vector<unsigned char> px;
    PixelOverlayModel(int W, int H) : w(W), h(H), px(W * H * 3) {}
    int getWidth() const { return w; }
    int getHeight() const { return h; }
    void setOverlayPixelValue(int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const int i = (y * w + x) * 3;
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
    void flushOverlayBuffer() {}
};
