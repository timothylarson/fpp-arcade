#include <fpp-pch.h>

#include "FPPMazeChase.h"
#include "ArcadeCanvas.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"

using ArcadeCanvas::RGB;

// Everything is one LED: corridors and walls are one pixel wide, and the
// player, ghosts and dots are single pixels. Corridors run along odd rows and
// columns, so the maze is a grid of cells with walls between them; it is
// carved as a random spanning tree over the left half, mirrored, and then
// opened up until it has no dead ends and a few extra loops.

namespace {

const RGB WALL = {0.1f, 0.2f, 1.0f};
const RGB DOT = {1.0f, 0.75f, 0.6f};
const RGB DOOR = {1.0f, 0.5f, 0.8f};
const RGB PLAYER = {1.0f, 0.85f, 0.0f};
const RGB SCARED = {0.15f, 0.25f, 1.0f};
const RGB EYES = {0.7f, 0.8f, 1.0f};
const std::array<RGB, 4> GHOST = {{{1.0f, 0.0f, 0.0f}, {1.0f, 0.4f, 0.75f}, {0.0f, 1.0f, 1.0f}, {1.0f, 0.55f, 0.0f}}};

const int DX[4] = {0, -1, 0, 1};   // up, left, down, right: the order ties are broken in
const int DY[4] = {-1, 0, 1, 0};
int reverseOf(int d) { return (d + 2) & 3; }

}  // namespace

class MazeChaseEffect : public FPPArcadeGameEffect {
public:
    // narrowFrom: the first level drawn with one-LED paths; before it they
    // are two LEDs wide. 0 keeps them wide.
    MazeChaseEffect(PixelOverlayModel *m, int lives, int narrowFrom)
        : FPPArcadeGameEffect(m), rng(std::random_device{}()), narrowLevel(std::max(0, narrowFrom)) {
        int w = 0, h = 0;
        model->getSize(w, h);
        canvas.resize(w, h);
        startLives = std::clamp(lives, 1, 9);
        newGame();
    }

    const std::string &name() const override {
        static std::string NAME = "MazeChase";
        return NAME;
    }

    void button(const std::string &b) override {
        const bool wasPaused = paused;
        FPPArcadeGameEffect::button(b);
        if (wasPaused || paused) {
            return;
        }
        // The last direction pressed is where the player turns next, as in the
        // original: it doesn't have to be held.
        if (b == "Up - Pressed") wanted.store(0);
        else if (b == "Left - Pressed") wanted.store(1);
        else if (b == "Down - Pressed") wanted.store(2);
        else if (b == "Right - Pressed") wanted.store(3);
    }

    void restart() override {
        newGame();
        resetFrameTimer();
    }

    int32_t update() override {
        if (finished) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return EFFECT_DONE;
        }
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return FRAME_MS;
        }
        const float dt = (float)(consumeElapsedMs(FRAME_MS, 100.0) / 1000.0);
        clock += dt;
        stateT += dt;
        step(dt);
        if (state == State::Over && stateT > 5.0f) {
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            finished = true;
            return EFFECT_AFTER_NEXT_OUTPUT;
        }
        render();
        model->setOverlayBufferDirty(true);
        return FRAME_MS;
    }

private:
    static constexpr int FRAME_MS = 25;
    enum class State { Ready, Play, Dying, Clear, Over };
    enum class GState { House, Leaving, Active, Eyes, Entering };

    struct Ghost {
        int x, y, dir;
        float acc;
        GState st;
        float releaseAt;
        bool scared;
        bool turnAround;
    };

    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    int rndInt(int n) { return std::min(n - 1, (int)(rnd() * n)); }

    // ---------------------------------------------------------------- the maze
    bool inMaze(int x, int y) const { return x >= 0 && y >= 0 && x < mw && y < mh; }
    bool isOpen(int x, int y) const { return inMaze(x, y) && grid[y * mw + x]; }
    void openSym(int x, int y) {
        grid[y * mw + x] = 1;
        grid[y * mw + (mw - 1 - x)] = 1;
    }
    bool inHouseArea(int x, int y) const { return x >= c - 4 && x <= c + 4 && y >= r - 3 && y <= r + 3; }
    bool isCell(int x, int y) const { return (x & 1) && (y & 1); }

    // The maze is worked out on a logical grid: odd rows and columns are
    // corridor cells, even ones the walls between them. Narrow, every logical
    // unit is one LED. Wide, corridor units are two LEDs and wall units one,
    // so paths are two wide, characters are 2x2, and logical unit i starts at
    // LED ceil(i/2) + 2*floor(i/2). Everything the game does - moving,
    // turning, catching - stays on the logical grid; only drawing changes.
    int screenStart(int i) const { return wide ? (i + 1) / 2 + 2 * (i / 2) : i; }
    int screenSpan(int i) const { return wide ? ((i & 1) ? 2 : 1) : 1; }
    int screenSize(int n) const { return screenStart(n - 1) + screenSpan(n - 1); }
    // Where a character's top-left LED goes for a (fractional) logical
    // position: steps of 1.5 LEDs wide, so it glides evenly over cells and
    // the walls between them.
    float spritePos(float i) const { return wide ? 1.5f * i - 0.5f : i; }
    int spriteSize() const { return wide ? 2 : 1; }

    // Fit the logical maze to the model for this level. It is mirrored about
    // a centre corridor, which needs a logical width of 4k+3; rows are odd.
    void layout() {
        const int w = canvas.W, h = canvas.H;
        wide = narrowLevel <= 0 || level < narrowLevel;
        if (wide) {
            int lw = (2 * (w - 1)) / 3 + 1;
            lw -= ((lw - 3) % 4 + 4) % 4;
            int lh = (2 * (h - 1)) / 3 + 1;
            if (!(lh & 1)) {
                --lh;
            }
            if (lw >= 19 && lh >= 15) {
                mw = lw;
                mh = lh;
            } else {
                wide = false;   // too small for wide paths
            }
        }
        if (!wide) {
            mw = std::max(19, w - ((w - 3) % 4 + 4) % 4);
            mh = std::max(15, (h % 2) ? h : h - 1);
        }
        ox = std::max(0, (w - screenSize(mw)) / 2);
        oy = std::max(0, (h - screenSize(mh)) / 2);
        c = (mw - 1) / 2;
        r = ((mh - 1) / 2) & ~1;
        tunnelY = r + 5;
    }

    void buildMaze() {
        layout();
        grid.assign(mw * mh, 0);
        const int nx = (mw - 1) / 2, ny = (mh - 1) / 2, mid = (nx - 1) / 2;
        for (int cy = 0; cy < ny; ++cy) {
            for (int cx = 0; cx < nx; ++cx) {
                grid[(2 * cy + 1) * mw + 2 * cx + 1] = 1;
            }
        }
        // Spanning tree over the left half and the centre column.
        std::vector<uint8_t> seen((mid + 1) * ny, 0);
        // Carried on in the same direction half the time, for longer straight
        // corridors than a plain random tree.
        struct Step {
            int cx, cy, from;
        };
        std::vector<Step> stack = {{mid, ny / 2, -1}};
        seen[(ny / 2) * (mid + 1) + mid] = 1;
        while (!stack.empty()) {
            const auto [cx, cy, from] = stack.back();
            int options[4], n = 0;
            for (int d = 0; d < 4; ++d) {
                const int nx2 = cx + DX[d], ny2 = cy + DY[d];
                if (nx2 >= 0 && nx2 <= mid && ny2 >= 0 && ny2 < ny && !seen[ny2 * (mid + 1) + nx2]) {
                    options[n++] = d;
                }
            }
            if (!n) {
                stack.pop_back();
                continue;
            }
            int d = options[rndInt(n)];
            if (from >= 0 && rnd() < 0.5f && std::find(options, options + n, from) != options + n) {
                d = from;
            }
            const int nx2 = cx + DX[d], ny2 = cy + DY[d];
            grid[(2 * cy + 1 + DY[d]) * mw + 2 * cx + 1 + DX[d]] = 1;
            seen[ny2 * (mid + 1) + nx2] = 1;
            stack.push_back({nx2, ny2, d});
        }
        for (int y = 0; y < mh; ++y) {
            for (int x = 0; x < c; ++x) {
                grid[y * mw + (mw - 1 - x)] = grid[y * mw + x];
            }
        }

        // The ghost house: a box with a door on top, ringed by a corridor that
        // joins the maze on all four sides.
        for (int y = r - 3; y <= r + 3; ++y) {
            for (int x = c - 4; x <= c + 4; ++x) {
                const bool ring = y == r - 3 || y == r + 3 || x == c - 4 || x == c + 4;
                const bool inside = x >= c - 2 && x <= c + 2 && y >= r - 1 && y <= r + 1;
                grid[y * mw + x] = (ring || inside) ? 1 : 0;
            }
        }
        grid[(r - 2) * mw + c] = 1;   // the door
        openSym(c - 5, r - 3);
        openSym(c - 5, r + 3);
        grid[(r - 4) * mw + c] = 1;
        grid[(r + 4) * mw + c] = 1;
        openSym(c - 1, tunnelY);   // the player starts here and can go either way
        // The side tunnel.
        grid[tunnelY * mw] = 1;
        grid[tunnelY * mw + mw - 1] = 1;

        connectAll();
        // No dead ends: give each one a second way out.
        for (int pass = 0; pass < 2; ++pass) {
            for (int y = 1; y < mh - 1; y += 2) {
                for (int x = 1; x <= c; x += 2) {
                    if (inHouseArea(x, y) || exits(x, y) != 1) {
                        continue;
                    }
                    int walls[4], n = 0;
                    for (int d = 0; d < 4; ++d) {
                        const int wx = x + DX[d], wy = y + DY[d];
                        if (!isOpen(wx, wy) && wx > 0 && wy > 0 && wx < mw - 1 && wy < mh - 1 &&
                            isOpen(x + 2 * DX[d], y + 2 * DY[d]) && !inHouseArea(x + 2 * DX[d], y + 2 * DY[d])) {
                            walls[n++] = d;
                        }
                    }
                    if (n) {
                        const int d = walls[rndInt(n)];
                        openSym(x + DX[d], y + DY[d]);
                    }
                }
            }
        }
        // A few extra loops so it plays like an arcade maze, not a puzzle.
        for (int y = 1; y < mh - 1; ++y) {
            for (int x = 1; x <= c; ++x) {
                if (isOpen(x, y) || ((x & 1) == (y & 1)) || inHouseArea(x, y)) {
                    continue;
                }
                const bool horiz = !(x & 1);   // a wall between two cells side by side
                const int ax = horiz ? x - 1 : x, ay = horiz ? y : y - 1, bx = horiz ? x + 1 : x, by = horiz ? y : y + 1;
                if (isOpen(ax, ay) && isOpen(bx, by) && !inHouseArea(ax, ay) && !inHouseArea(bx, by) && rnd() < 0.22f) {
                    openSym(x, y);
                }
            }
        }

        dots.assign(mw * mh, 0);
        dotsLeft = 0;
        for (int y = 0; y < mh; ++y) {
            for (int x = 1; x < mw - 1; ++x) {
                // Wide paths get a dot per cell, not per logical unit, or
                // they'd sit a LED and a half apart.
                if (isOpen(x, y) && !inHouseArea(x, y) && !(x == c && y == tunnelY) && (!wide || isCell(x, y))) {
                    dots[y * mw + x] = 1;
                    ++dotsLeft;
                }
            }
        }
        for (auto [qx, qy] : {std::pair<int, int>{1, 1}, {mw - 2, 1}, {1, mh - 2}, {mw - 2, mh - 2}}) {
            if (dots[qy * mw + qx] == 1) {
                dots[qy * mw + qx] = 2;
            }
        }
    }

    int exits(int x, int y) const {
        int n = 0;
        for (int d = 0; d < 4; ++d) {
            n += isOpen(x + DX[d], y + DY[d]) ? 1 : 0;
        }
        return n;
    }

    // The house can cut a corner of the tree off; join anything unreachable
    // back on through the wall nearest the part that can be reached.
    void connectAll() {
        for (int guard = 0; guard < 200; ++guard) {
            std::vector<uint8_t> reach(mw * mh, 0);
            std::vector<int> todo = {tunnelY * mw + c};
            reach[todo[0]] = 1;
            while (!todo.empty()) {
                const int i = todo.back();
                todo.pop_back();
                const int x = i % mw, y = i / mw;
                for (int d = 0; d < 4; ++d) {
                    const int nx = x + DX[d], ny = y + DY[d];
                    if (isOpen(nx, ny) && !reach[ny * mw + nx]) {
                        reach[ny * mw + nx] = 1;
                        todo.push_back(ny * mw + nx);
                    }
                }
            }
            bool joined = false, missing = false;
            for (int y = 1; y < mh - 1 && !joined; ++y) {
                for (int x = 1; x < mw - 1 && !joined; ++x) {
                    if (isOpen(x, y) || ((x & 1) == (y & 1))) {
                        continue;
                    }
                    const bool horiz = !(x & 1);
                    const int a = horiz ? y * mw + x - 1 : (y - 1) * mw + x;
                    const int b = horiz ? y * mw + x + 1 : (y + 1) * mw + x;
                    if (grid[a] && grid[b] && reach[a] != reach[b]) {
                        missing = true;
                        if (!inHouseArea(x, y)) {
                            openSym(x < c ? x : mw - 1 - x, y);
                            joined = true;
                        }
                    }
                }
            }
            if (!missing || !joined) {
                return;
            }
        }
    }

    // ---------------------------------------------------------------- movement
    // x wraps through the tunnel; the door only lets ghosts through.
    bool passable(int x, int y, bool ghost) const {
        if (y == tunnelY && (x < 0 || x >= mw)) {
            return true;
        }
        if (!isOpen(x, y)) {
            return false;
        }
        return ghost || !(x == c && y == r - 2);
    }
    int wrapX(int x) const { return x < 0 ? mw - 1 : (x >= mw ? 0 : x); }

    void newGame() {
        level = 1;
        score = 0;
        lives = startLives;
        buildMaze();
        resetActors();
    }

    void resetActors() {
        px = c;
        py = tunnelY;
        pdir = 1;
        pacc = 0;
        wanted.store(1);
        ghosts.clear();
        const float gap = std::max(0.5f, 3.0f - 0.3f * (level - 1));
        ghosts.push_back({c, r - 3, 1, 0, GState::Active, 0, false, false});
        ghosts.push_back({c, r, 0, 0, GState::House, gap * 0.5f, false, false});
        ghosts.push_back({c - 2, r, 0, 0, GState::House, gap * 1.5f, false, false});
        ghosts.push_back({c + 2, r, 0, 0, GState::House, gap * 2.5f, false, false});
        modeT = 0;
        scaredT = 0;
        playT = 0;
        state = State::Ready;
        stateT = 0;
    }

    bool chasing() const {
        static const float plan[] = {7, 27, 34, 54, 59};   // scatter until 7, chase until 27, ...
        int k = 0;
        while (k < 5 && modeT >= plan[k]) {
            ++k;
        }
        return k & 1;
    }

    void step(float dt) {
        switch (state) {
        case State::Ready:
            if (stateT > 2.0f) {
                state = State::Play;
                stateT = 0;
            }
            return;
        case State::Dying:
            if (stateT > 1.6f) {
                if (--lives > 0) {
                    resetActors();
                } else {
                    state = State::Over;
                    stateT = 0;
                }
            }
            return;
        case State::Clear:
            if (stateT > 2.0f) {
                ++level;
                buildMaze();
                resetActors();
            }
            return;
        case State::Over:
            return;
        case State::Play:
            break;
        }
        playT += dt;
        const bool wasChasing = chasing();
        if (scaredT > 0) {
            scaredT = std::max(0.0f, scaredT - dt);
            if (scaredT == 0) {
                for (auto &g : ghosts) {
                    g.scared = false;
                }
            }
        } else {
            modeT += dt;   // the scatter/chase clock stops while ghosts are scared
        }
        if (chasing() != wasChasing) {
            for (auto &g : ghosts) {
                if (g.st == GState::Active) {
                    g.turnAround = true;
                }
            }
        }

        movePlayer(dt);
        if (state != State::Play) {
            return;
        }
        for (int k = 0; k < (int)ghosts.size() && state == State::Play; ++k) {
            moveGhost(k, dt);
        }
        if (state == State::Play && dotsLeft == 0) {
            state = State::Clear;
            stateT = 0;
        }
    }

    float levelSpeed(float base, float per, float cap) const { return std::min(cap, base + per * (level - 1)); }

    void movePlayer(float dt) {
        pacc += levelSpeed(7.5f, 0.4f, 10.0f) * dt;
        const int want = wanted.load();
        if (want == reverseOf(pdir)) {
            pdir = want;   // reversing is allowed at any moment
        }
        while (pacc >= 1.0f && state == State::Play) {
            if (passable(px + DX[want], py + DY[want], false)) {
                pdir = want;
            }
            if (!passable(px + DX[pdir], py + DY[pdir], false)) {
                pacc = 0;
                break;
            }
            pacc -= 1.0f;
            px = wrapX(px + DX[pdir]);
            py += DY[pdir];
            uint8_t &d = dots[py * mw + px];
            if (d) {
                score += d == 2 ? 50 : 10;
                if (d == 2) {
                    scaredT = std::max(1.5f, 6.0f - (level - 1));
                    combo = 0;
                    for (auto &g : ghosts) {
                        if (g.st == GState::Active || g.st == GState::Leaving || g.st == GState::House) {
                            g.scared = true;
                            if (g.st == GState::Active) {
                                g.turnAround = true;
                            }
                        }
                    }
                }
                d = 0;
                --dotsLeft;
            }
            checkHits();
        }
    }

    void target(int k, const Ghost &g, int &tx, int &ty) const {
        if (g.st == GState::Eyes) {
            tx = c;
            ty = r - 3;
            return;
        }
        if (!chasing()) {
            tx = (k == 0 || k == 2) ? mw + 1 : -2;
            ty = (k < 2) ? -3 : mh + 2;
            return;
        }
        switch (k) {
        case 0:
            tx = px;
            ty = py;
            break;
        case 1:
            tx = px + 4 * DX[pdir];
            ty = py + 4 * DY[pdir];
            break;
        case 2: {
            const int ax = px + 2 * DX[pdir], ay = py + 2 * DY[pdir];
            tx = 2 * ax - ghosts[0].x;
            ty = 2 * ay - ghosts[0].y;
            break;
        }
        default:
            if ((g.x - px) * (g.x - px) + (g.y - py) * (g.y - py) > 36) {
                tx = px;
                ty = py;
            } else {
                tx = -2;
                ty = mh + 2;
            }
        }
    }

    void moveGhost(int k, float dt) {
        Ghost &g = ghosts[k];
        float speed = levelSpeed(7.0f, 0.45f, 10.5f);
        if (g.st == GState::Eyes) {
            speed = 16.0f;
        } else if (g.st == GState::House || g.st == GState::Leaving || g.st == GState::Entering) {
            speed = 4.0f;
        } else if (g.scared) {
            speed = 4.5f;
        } else if (g.y == tunnelY && (g.x < 4 || g.x > mw - 5)) {
            speed *= 0.55f;
        }
        if (g.st == GState::House) {
            if (playT >= g.releaseAt) {
                g.st = GState::Leaving;
            }
            return;
        }
        g.acc += speed * dt;
        while (g.acc >= 1.0f && state == State::Play) {
            g.acc -= 1.0f;
            if (g.st == GState::Leaving) {
                // Over to the door, then straight up and out.
                if (g.x != c) {
                    g.x += g.x < c ? 1 : -1;
                } else if (g.y > r - 3) {
                    --g.y;
                } else {
                    g.st = GState::Active;
                    g.dir = 1;
                }
            } else if (g.st == GState::Entering) {
                if (g.y < r) {
                    ++g.y;
                } else {
                    g.st = GState::Leaving;
                }
            } else {
                if (g.st == GState::Eyes && g.x == c && g.y == r - 3) {
                    g.st = GState::Entering;
                    continue;
                }
                if (g.turnAround) {
                    g.turnAround = false;
                    g.dir = reverseOf(g.dir);
                } else {
                    g.dir = chooseDir(k, g);
                }
                if (!passable(g.x + DX[g.dir], g.y + DY[g.dir], false)) {
                    g.dir = reverseOf(g.dir);
                    if (!passable(g.x + DX[g.dir], g.y + DY[g.dir], false)) {
                        continue;
                    }
                }
                g.x = wrapX(g.x + DX[g.dir]);
                g.y += DY[g.dir];
            }
            checkHits();
        }
    }

    // At each step: never straight back; scared ghosts wander at random,
    // the rest take the way that ends nearest their target.
    int chooseDir(int k, const Ghost &g) {
        int options[4], n = 0;
        for (int d = 0; d < 4; ++d) {
            if (d != reverseOf(g.dir) && passable(g.x + DX[d], g.y + DY[d], false)) {
                options[n++] = d;
            }
        }
        if (!n) {
            return reverseOf(g.dir);
        }
        if (g.scared && g.st == GState::Active) {
            return options[rndInt(n)];
        }
        int tx, ty;
        target(k, g, tx, ty);
        int best = options[0], bestD = 1 << 30;
        for (int j = 0; j < n; ++j) {
            const int d = options[j];
            const int ddx = g.x + DX[d] - tx, ddy = g.y + DY[d] - ty;
            if (ddx * ddx + ddy * ddy < bestD) {
                bestD = ddx * ddx + ddy * ddy;
                best = d;
            }
        }
        return best;
    }

    void checkHits() {
        for (auto &g : ghosts) {
            if (g.x != px || g.y != py) {
                continue;
            }
            if (g.st != GState::Active && g.st != GState::Leaving) {
                continue;
            }
            if (g.scared) {
                g.scared = false;
                g.st = GState::Eyes;
                score += 200 << std::min(combo, 3);
                ++combo;
                flashT = 0.25f;
            } else {
                state = State::Dying;
                stateT = 0;
                return;
            }
        }
    }

    // ---------------------------------------------------------------- drawing
    void render() {
        canvas.clear();
        flashT = std::max(0.0f, flashT - 0.025f);
        const bool clearFlash = state == State::Clear && std::fmod(stateT, 0.5f) < 0.25f;
        // Light a logical unit's LEDs: one LED narrow, up to 2x2 wide.
        auto unit = [&](int x, int y, const RGB &col, float k) {
            for (int yy = 0; yy < screenSpan(y); ++yy) {
                for (int xx = 0; xx < screenSpan(x); ++xx) {
                    canvas.add(ox + screenStart(x) + xx, oy + screenStart(y) + yy, col, k);
                }
            }
        };
        for (int y = 0; y < mh; ++y) {
            for (int x = 0; x < mw; ++x) {
                if (!isOpen(x, y)) {
                    // Only walls that face a corridor; solid areas stay dark.
                    bool edge = false;
                    for (int d = 0; d < 8 && !edge; ++d) {
                        static const int ex[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, ey[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
                        edge = isOpen(x + ex[d], y + ey[d]);
                    }
                    if (edge) {
                        unit(x, y, clearFlash ? ArcadeCanvas::WHITE : WALL, clearFlash ? 0.45f : (wide ? 0.18f : 0.12f));
                    }
                    continue;
                }
                if (x == c && y == r - 2) {
                    unit(x, y, DOOR, 0.3f);
                }
                // Wide, a dot is one LED in its 2x2 cell, so the paths read as
                // dark lanes with dots in them; power pellets fill the cell.
                const uint8_t d = dots[y * mw + x];
                if (d == 1) {
                    if (wide) {
                        canvas.add(ox + screenStart(x), oy + screenStart(y), DOT, 0.22f);
                    } else {
                        unit(x, y, DOT, 0.16f);
                    }
                } else if (d == 2) {
                    unit(x, y, DOT, std::fmod(clock, 0.5f) < 0.3f ? (wide ? 0.7f : 1.0f) : 0.12f);
                }
            }
        }
        const bool showGhosts = state != State::Clear && !(state == State::Dying && stateT > 0.5f) && state != State::Over;
        if (showGhosts) {
            for (int k = 0; k < (int)ghosts.size(); ++k) {
                const Ghost &g = ghosts[k];
                RGB col = GHOST[k];
                float kk = 1.0f;
                if (g.st == GState::Eyes || g.st == GState::Entering) {
                    col = EYES;
                    kk = 0.6f;
                } else if (g.scared) {
                    const bool ending = scaredT < 1.5f && std::fmod(scaredT, 0.3f) < 0.15f;
                    col = ending ? ArcadeCanvas::WHITE : SCARED;
                }
                // Part way to the next unit when it's free to move there.
                float f = 0;
                if ((g.st == GState::Active || g.st == GState::Eyes) && passable(g.x + DX[g.dir], g.y + DY[g.dir], false)) {
                    f = std::min(1.0f, g.acc);
                }
                sprite(g.x + DX[g.dir] * f, g.y + DY[g.dir] * f, col, kk);
            }
        }
        const bool showPlayer = state != State::Over && !(state == State::Dying && (stateT > 1.0f || std::fmod(stateT, 0.2f) < 0.1f));
        if (showPlayer) {
            // A little chomp: brighter and dimmer as it moves.
            const float chomp = state == State::Play ? 0.8f + 0.2f * std::sin(clock * 25.0f) : 1.0f;
            const float f = (state == State::Play && passable(px + DX[pdir], py + DY[pdir], false)) ? std::min(1.0f, pacc) : 0.0f;
            sprite(px + DX[pdir] * f, py + DY[pdir] * f, flashT > 0 ? ArcadeCanvas::WHITE : PLAYER, chomp);
        }
        canvas.writeTo(model);

        if (state == State::Ready) {
            words({"READY"}, 255, 220, 0);
        } else if (state == State::Over && stateT > 1.0f) {
            words({"GAME", "OVER", std::to_string(score)}, 255, 255, 255);
        } else if (state == State::Clear && stateT > 0.8f) {
            words({std::to_string(score)}, 255, 220, 0);
        }
    }

    // A character: one LED narrow, 2x2 wide, at a fractional logical position.
    void sprite(float lx, float ly, const RGB &col, float k) {
        const int x0 = ox + (int)std::lround(spritePos(lx)), y0 = oy + (int)std::lround(spritePos(ly));
        for (int yy = 0; yy < spriteSize(); ++yy) {
            for (int xx = 0; xx < spriteSize(); ++xx) {
                canvas.set(x0 + xx, y0 + yy, col, k);
            }
        }
    }

    // Lines of text centred on the panel over a black box, so the maze doesn't
    // show through the letters.
    void words(const std::vector<std::string> &lines, int cr, int cg, int cb) {
        const int n = std::min<int>(lines.size(), std::max(1, (model->getHeight() + 1) / 6));
        size_t longest = 1;
        for (int k = 0; k < n; ++k) {
            longest = std::max(longest, lines[k].size());
        }
        const int w = (int)longest * 4 + 1, h = n * 6 + 1;
        const int x0 = std::max(0, (model->getWidth() - w) / 2), y0 = std::max(0, (model->getHeight() - h) / 2);
        for (int y = y0; y < std::min(model->getHeight(), y0 + h); ++y) {
            for (int x = x0; x < std::min(model->getWidth(), x0 + w); ++x) {
                model->setOverlayPixelValue(x, y, 0, 0, 0);
            }
        }
        for (int k = 0; k < n; ++k) {
            outputString(lines[k], centerTextX(lines[k], 1), y0 + 1 + k * 6, k == 2 ? 255 : cr, k == 2 ? 200 : cg,
                         k == 2 ? 60 : cb, 1);
        }
    }

    std::mt19937 rng;
    ArcadeCanvas::Canvas canvas;
    int mw = 0, mh = 0, ox = 0, oy = 0, c = 0, r = 0, tunnelY = 0;
    int narrowLevel = 4;
    bool wide = true;
    std::vector<uint8_t> grid, dots;
    int dotsLeft = 0;

    State state = State::Ready;
    float stateT = 0, clock = 0, modeT = 0, scaredT = 0, playT = 0, flashT = 0;
    int level = 1, score = 0, lives = 3, startLives = 3, combo = 0;

    int px = 0, py = 0, pdir = 1;
    float pacc = 0;
    std::atomic<int> wanted{1};
    std::vector<Ghost> ghosts;
    bool finished = false;
};


const std::string &FPPMazeChase::getName() {
    static const std::string name = "Maze Chase";
    return name;
}

void FPPMazeChase::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m == nullptr) {
        return;
    }
    MazeChaseEffect *effect = dynamic_cast<MazeChaseEffect *>(m->getRunningEffect());
    const bool starting = !effect;
    if (starting) {
        if (findOption("overlay", "Overwrite") == "Transparent") {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
        } else {
            m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
        }
        effect = new MazeChaseEffect(m, findIntOption("Lives", 3), findIntOption("Narrow From", 4));
        m->setRunningEffect(effect, 25);
    }
    // The starting press also sets the first direction - but not Start, which
    // would pause a game that has only just begun.
    if (!starting || button.compare(0, 5, "Start") != 0) {
        effect->button(button);
    }
}
