#pragma once
// Tetris's end show (see ArcadeShow.h): NEW BEST! or GREAT GAME!, the score
// and lines (and the best, when this wasn't it), then a Christmas tree built
// the Tetris way - a trunk drops in, then green tetrominoes fall one after
// another and lock into the tree, presents drop in beside it when the wall is
// wide enough, and last of all the star lands on top and lights up, the
// ornaments switch on and fireworks go off - the credits, and GAME OVER.
//
// At Halloween the pieces build a giant candy corn instead - yellow at the
// bottom, orange, then white at the tip - on a purple stand, with
// jack-o'-lanterns for presents and a bat landing on top. On an everyday
// show they keep the game's own colours, gold trophies stand either side,
// and the star still lands.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ArcadeShow.h"

namespace TetrisShow {

using RGB = ArcadeCanvas::RGB;

// The tree in cells of a well 11 wide. Each letter is one tetromino, and they
// drop in alphabetical order: bottom up, so every piece falls straight down
// past nothing that is already there, the way it would in the game.
static const char *const TREE[] = {
    "     l     ",
    "    kll    ",
    "   ikljj   ",
    "  iikkjhh  ",
    "   iggjh   ",
    "  eggffhd  ",
    " eeebffddc ",
    "aaaabbbdccc",
};
static const int TREE_W = 11, TREE_ROWS = 8;
static const int TRUNK_ROWS = 2;   // the trunk is an O piece under the middle

// The star, 6 wide so it sits square on the two-LED tip of the tree.
static const char *const STAR[] = {
    "..##..",
    "######",
    ".####.",
    ".####.",
    "##..##",
};
static const int STAR_W = 6, STAR_H = 5;

class Story : public ShowStory {
public:
    void begin(ArcadeShow &s) override {
        const int W = s.W, H = s.H;
        theme = s.theme();
        pieces.clear();
        lights.clear();
        starLit = false;
        nextBang = 0;

        // As big as fits: the tree, its trunk and the star stacked up the
        // full height, and the tree no wider than the wall.
        cell = std::max(1, std::min((H - STAR_H) / (TREE_ROWS + TRUNK_ROWS), (W - 2) / TREE_W));
        const int treeW = TREE_W * cell;
        x0 = (W - treeW) / 2;
        y0 = H - (TREE_ROWS + TRUNK_ROWS) * cell;
        starX = x0 + (treeW - STAR_W) / 2;
        starY = y0 - STAR_H;

        // The trunk first, half a cell over so it's centred like the tip.
        static const RGB BROWN = {0.55f, 0.25f, 0.05f};
        pieces.push_back(square(x0 + (TREE_W * cell) / 2 - cell, y0 + TREE_ROWS * cell,
                                theme == ShowTheme::Halloween ? ShowArt::HAT : theme == ShowTheme::Everyday ? ShowArt::GOLDEN : BROWN));

        // Then the tree, piece by piece in letter order.
        static const RGB GREENS[3] = {{0.05f, 0.75f, 0.15f}, {0.0f, 0.45f, 0.1f}, {0.3f, 0.85f, 0.2f}};
        const size_t treeFrom = pieces.size();
        for (char id = 'a'; id <= 'z'; ++id) {
            Piece p;
            p.x = x0;
            p.y = y0;
            for (int r = 0; r < TREE_ROWS; ++r) {
                for (int col = 0; col < TREE_W; ++col) {
                    if (TREE[r][col] == id) {
                        p.cells.push_back({col, r});
                    }
                }
            }
            if (p.cells.empty()) {
                break;
            }
            pieces.push_back(p);
        }
        // Shade each tree piece unlike its neighbours already shaded, so the
        // tetrominoes stay tellable apart once they're all in.
        for (size_t i = treeFrom; i < pieces.size(); ++i) {
            bool used[3] = {false, false, false};
            for (size_t j = treeFrom; j < i; ++j) {
                if (touching(pieces[i], pieces[j])) {
                    used[pieces[j].shade] = true;
                }
            }
            int pick = 0;
            while (pick < 2 && used[pick]) {
                ++pick;
            }
            pieces[i].shade = pick;
            pieces[i].col = GREENS[pick];
            if (theme == ShowTheme::Halloween) {
                // Candy corn: banded by how high the piece sits, a touch
                // darker or lighter by its shade so the pieces still show.
                float row = 0;
                for (const auto &q : pieces[i].cells) {
                    row += q.y;
                }
                row /= pieces[i].cells.size();
                const RGB band = row < TREE_ROWS * 0.3f ? RGB{1, 1, 0.9f} : row < TREE_ROWS * 0.65f ? ShowArt::ORANGE : ShowArt::CANDLE;
                const float sh = pick == 0 ? 1.0f : pick == 1 ? 0.75f : 0.9f;
                pieces[i].col = {band.r * sh, band.g * sh, band.b * sh};
            } else if (theme == ShowTheme::Everyday) {
                // The game's seven colours, in turn.
                static const RGB GAME[7] = {{0.2f, 1, 1}, {1, 1, 0.2f}, {0.95f, 0.3f, 1}, {0.3f, 1, 0.3f},
                                            {1, 0.25f, 0.2f}, {0.25f, 0.4f, 1}, {1, 0.6f, 0.1f}};
                pieces[i].col = GAME[(i - treeFrom) % 7];
            }
        }

        // Presents either side, on a wall with room for them: O pieces with
        // a ribbon, nearest the tree first.
        static const RGB WRAP[4][2] = {{{1, 0.1f, 0.1f}, {1, 0.85f, 0.2f}},
                                       {{0.15f, 0.3f, 1}, {1, 1, 1}},
                                       {{0.9f, 0.15f, 0.9f}, {1, 0.85f, 0.2f}},
                                       {{1, 0.75f, 0.1f}, {1, 0.1f, 0.1f}}};
        const int box = presentW(), gap = std::max(2, cell);
        int n = 0;
        for (int k = 0; k < 2; ++k) {
            const int left = x0 - gap - box - k * (box + gap);
            const int right = x0 + treeW + gap + k * (box + gap);
            if (left < 1 || right + box > W - 1) {
                break;
            }
            for (int x : {left, right}) {
                Piece p = square(x, H - 2 * cell, WRAP[n % 4][0]);
                p.ribbon = WRAP[n % 4][1];
                p.present = true;
                pieces.push_back(p);
                ++n;
            }
        }

        // The ornaments, a light on about one LED in nine of the tree,
        // switched on top to bottom once the star is lit.
        static const RGB BULBS[] = {{1, 0.1f, 0.1f}, {1, 0.8f, 0.15f}, {0.2f, 0.4f, 1},
                                    {1, 1, 1}, {1, 0.2f, 0.9f}, {0.2f, 0.9f, 1}};
        std::vector<std::pair<int, int>> spots;
        for (int r = 0; r < TREE_ROWS; ++r) {
            for (int col = 0; col < TREE_W; ++col) {
                if (TREE[r][col] != ' ') {
                    for (int py = 0; py < cell; ++py) {
                        for (int px = 0; px < cell; ++px) {
                            spots.push_back({x0 + col * cell + px, y0 + r * cell + py});
                        }
                    }
                }
            }
        }
        // The moon goes top left if it would be behind the tree, as on a
        // narrow wall where the tree takes the middle and most of the width.
        bool behind = false;
        for (const auto &p : spots) {
            behind = behind || std::hypot(p.first + 0.5f - s.moonX, p.second + 0.5f - s.moonY) < s.moonR + 1.5f;
        }
        if (behind) {
            s.moonR = std::min(s.moonR, 3.0f);
            s.moonX = s.moonR + 1.5f;
            s.moonY = s.moonR + 1.5f;
        }

        const int want = std::max(4, (int)spots.size() / 9);
        for (int i = 0; i < want && !spots.empty(); ++i) {
            const size_t at = std::min(spots.size() - 1, (size_t)(s.rnd() * spots.size()));
            const auto spot = spots[at];
            spots.erase(spots.begin() + at);
            // Not right beside another, so each reads as a bulb of its own.
            bool near = false;
            for (const auto &l : lights) {
                near = near || (std::abs(l.x - spot.first) <= 1 && std::abs(l.y - spot.second) <= 1);
            }
            if (near) {
                continue;
            }
            static const RGB GLINT[2] = {{0.65f, 0.2f, 1}, {0.35f, 1, 0.15f}};
            const RGB col = theme == ShowTheme::Halloween ? GLINT[i % 2] : theme == ShowTheme::Everyday ? RGB{1, 1, 1} : BULBS[i % 6];
            lights.push_back({spot.first, spot.second, col, s.rnd() * 6.28f});
        }
        std::sort(lights.begin(), lights.end(), [](const Light &a, const Light &b) { return a.y < b.y; });
    }

    void draw(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, double ms, double len, float k) override {
        if (spacing == 0) {
            // Pace the drops so the star lands a little past half way, leaving
            // the rest for the tree lit up.
            spacing = std::max(150.0, (len * 0.6 - 500.0) / (pieces.size() + 1));
            fall = std::max(250.0, spacing * 1.4);
            for (size_t i = 0; i < pieces.size(); ++i) {
                pieces[i].land = 400.0 + fall + i * spacing;
            }
            starLand = 400.0 + fall + pieces.size() * spacing + spacing * 0.5;
        }
        s.drawNight(c, dt, k);

        for (const auto &p : pieces) {
            if (ms < p.land - fall) {
                continue;
            }
            // Falls from just above the wall, speeding up a little, to land on
            // time; a white flash as it locks.
            const int bottom = p.bottom(cell);
            const float f = std::clamp((float)((ms - (p.land - fall)) / fall), 0.0f, 1.0f);
            const int dy = -(int)std::lround((1.0f - std::pow(f, 1.4f)) * bottom);
            const float flash = ms >= p.land ? std::max(0.0f, 1.0f - (float)(ms - p.land) / 200.0f) : 0.0f;
            drawPiece(c, p, dy, flash, k);
        }

        // The star: dim gold as it drops, lit with a burst as it lands.
        const float f = std::clamp((float)((ms - (starLand - fall)) / fall), 0.0f, 1.0f);
        if (ms >= starLand - fall) {
            const int dy = -(int)std::lround((1.0f - std::pow(f, 1.4f)) * (starY + STAR_H));
            if (!starLit && ms >= starLand) {
                starLit = true;
                s.burst(starX + STAR_W / 2.0f, starY + STAR_H / 2.0f, {1, 0.85f, 0.2f}, 36, true);
            }
            drawStar(c, dy, ms - starLand, k);
        }
        if (starLit) {
            drawLights(c, ms - starLand, k);
            // Fireworks to finish, off to the sides of the tree where there's room.
            const double since = ms - starLand;
            if (since >= 700 + nextBang * 650.0 && ms < len - 900) {
                ++nextBang;
                float x;
                if (x0 > 10) {
                    const float side = s.rnd() < 0.5f ? 0.0f : (float)(x0 + TREE_W * cell);
                    x = side + 2 + s.rnd() * (x0 - 4);
                } else {
                    x = 2 + s.rnd() * (s.W - 4);
                }
                static const RGB COLS[] = {{1, 0.15f, 0.1f}, {0.2f, 1, 0.3f}, {1, 0.8f, 0.2f}, {0.3f, 0.5f, 1}, {1, 1, 1}};
                s.burst(x, s.H * (0.15f + 0.3f * s.rnd()), COLS[nextBang % 5], 22, nextBang % 3 == 0);
            }
        }
        s.drawFireworks(c, k);
    }

    // Behind the credits and the ending: the finished tree, lit.
    void backdrop(ArcadeShow &s, ArcadeCanvas::Canvas &c, float dt, float k) override {
        s.drawNight(c, dt, k);
        for (const auto &p : pieces) {
            drawPiece(c, p, 0, 0, k);
        }
        drawStar(c, 0, 5000 + s.t, k);
        drawLights(c, 5000 + s.t, k);
    }

private:
    struct Cell {
        int x, y;
    };
    struct Piece {
        int x = 0, y = 0;            // where its cells start, in LEDs
        std::vector<Cell> cells;     // in cells from there
        RGB col = {1, 1, 1};
        RGB ribbon = {1, 1, 1};
        bool present = false;
        int shade = 0;
        double land = 0;             // ms into the story it locks
        int bottom(int cell) const {
            int r = 0;
            for (const auto &c : cells) {
                r = std::max(r, c.y + 1);
            }
            return y + r * cell;
        }
    };
    struct Light {
        int x, y;
        RGB col;
        float phase;
    };

    static Piece square(int x, int y, const RGB &col) {
        Piece p;
        p.x = x;
        p.y = y;
        p.cells = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        p.col = col;
        return p;
    }
    static bool touching(const Piece &a, const Piece &b) {
        for (const auto &p : a.cells) {
            for (const auto &q : b.cells) {
                if (std::abs(p.x - q.x) + std::abs(p.y - q.y) == 1) {
                    return true;
                }
            }
        }
        return false;
    }

    // A block the way the game's blocks look from across the yard: lit from
    // the top left, so each cell reads as a tile and not a smear of colour.
    void drawPiece(ArcadeCanvas::Canvas &c, const Piece &p, int dy, float flash, float k) const {
        if (p.present) {
            drawPresent(c, p, dy, flash, k);
            return;
        }
        for (const auto &q : p.cells) {
            const int bx = p.x + q.x * cell, by = p.y + q.y * cell + dy;
            for (int py = 0; py < cell; ++py) {
                for (int px = 0; px < cell; ++px) {
                    RGB col = p.col;
                    if (cell > 1 && px == 0 && py == 0) {
                        col = ArcadeCanvas::mix(col, {1, 1, 1}, 0.3f);
                    } else if (cell > 1 && px == cell - 1 && py == cell - 1) {
                        col = {col.r * 0.55f, col.g * 0.55f, col.b * 0.55f};
                    }
                    c.set(bx + px, by + py, ArcadeCanvas::mix(col, {1, 1, 1}, 0.75f * flash), k);
                }
            }
        }
    }

    // A present one LED wider than an O piece, so its ribbon can run down the
    // middle, with a band across and a bow on top.
    // At Halloween a jack-o'-lantern, lit once the bat is down; on an
    // everyday show a trophy.
    void drawPresent(ArcadeCanvas::Canvas &c, const Piece &p, int dy, float flash, float k) const {
        const int w = presentW(), h = 2 * cell, mid = w / 2;
        if (theme == ShowTheme::Halloween) {
            ShowArt::pumpkin(c, p.x + (w - 5) / 2, p.y + h - 5 + dy, k, starLit);
            return;
        }
        if (theme == ShowTheme::Everyday) {
            ShowArt::trophy(c, p.x + (w - 5) / 2, p.y + h - 5 + dy, k);
            return;
        }
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const RGB col = (x == mid || y == cell - 1) ? p.ribbon : p.col;
                c.set(p.x + x, p.y + y + dy, ArcadeCanvas::mix(col, {1, 1, 1}, 0.75f * flash), k);
            }
        }
        c.set(p.x + mid - 1, p.y + dy - 1, p.ribbon, k);
        c.set(p.x + mid + 1, p.y + dy - 1, p.ribbon, k);
    }
    int presentW() const { return 2 * cell + 1; }

    // Dim before it lands; then bright gold with a slow shimmer and, every so
    // often, a glint flying out from its corners.
    void drawStar(ArcadeCanvas::Canvas &c, int dy, double since, float k) const {
        const bool lit = since >= 0;
        if (theme == ShowTheme::Halloween) {
            // A big bat, its eyes glowing once it's down, flapping now and then.
            static const char *const UP[] = {"bb..bb", "bbyybb", ".bbbb.", "..bb.."};
            static const char *const DOWN[] = {"..bb..", "bbyybb", "bbbbbb", "b.bb.b"};
            const bool flap = !lit || std::fmod(since, 1600.0) < 500.0;
            ShowArt::sprite(c, flap && ((int)(since / 120.0) & 1) ? DOWN : UP, 4, starX, starY + 1 + dy,
                            {{'b', ShowArt::BAT}, {'y', lit ? ShowArt::CANDLE : ShowArt::BAT}}, k);
            return;
        }
        const float flash = lit ? std::max(0.0f, 1.0f - (float)since / 400.0f) : 0.0f;
        for (int y = 0; y < STAR_H; ++y) {
            for (int x = 0; x < STAR_W; ++x) {
                if (STAR[y][x] != '#') {
                    continue;
                }
                RGB col = {0.5f, 0.38f, 0.08f};
                if (lit) {
                    const float sh = 0.85f + 0.15f * std::sin((float)since * 0.006f - (x + y) * 0.9f);
                    col = ArcadeCanvas::mix({1.0f * sh, 0.82f * sh, 0.2f * sh}, {1, 1, 1}, flash);
                }
                c.set(starX + x, starY + y + dy, col, k);
            }
        }
        if (lit && since > 300) {
            const float g = std::pow(std::max(0.0f, std::sin((float)since * 0.004f)), 6.0f);
            const int cx = starX, cy = starY + dy;
            c.add(cx - 1, cy - 1, {1, 1, 0.8f}, g * k);
            c.add(cx + STAR_W, cy - 1, {1, 1, 0.8f}, g * k);
            c.add(cx - 1, cy + 1, {1, 0.9f, 0.5f}, 0.6f * g * k);
            c.add(cx + STAR_W, cy + 1, {1, 0.9f, 0.5f}, 0.6f * g * k);
        }
    }

    void drawLights(ArcadeCanvas::Canvas &c, double since, float k) const {
        const double each = 900.0 / std::max<size_t>(1, lights.size());
        for (size_t i = 0; i < lights.size(); ++i) {
            const double on = since - 150.0 - i * each;
            if (on < 0) {
                continue;
            }
            const auto &l = lights[i];
            const float pop = std::max(0.0f, 1.0f - (float)on / 250.0f);
            const float tw = 0.7f + 0.3f * std::sin((float)since * 0.005f + l.phase);
            c.set(l.x, l.y, ArcadeCanvas::mix(l.col, {1, 1, 1}, pop), tw * k);
        }
    }

    ShowTheme theme = ShowTheme::Christmas;
    std::vector<Piece> pieces;
    std::vector<Light> lights;
    int cell = 2, x0 = 0, y0 = 0, starX = 0, starY = 0;
    double spacing = 0, fall = 0, starLand = 0;
    bool starLit = false;
    int nextBang = 0;
};

// score and lines at the end, previousBest the best score before this game.
inline ShowConfig config(int score, int lines, bool newBest, int previousBest) {
    using namespace ShowColours;
    ShowConfig cfg;
    cfg.headline = newBest ? std::vector<std::string>{"NEW", "BEST!"} : std::vector<std::string>{"GREAT", "GAME!"};
    cfg.stats.push_back({"SCORE", score, false, GOLD});
    cfg.stats.push_back({"LINES", lines, false, GREEN});
    if (!newBest && previousBest > 0) {
        cfg.stats.push_back({"BEST", previousBest, false, GOLD});
    }
    // The cast in the colours the game gives them.
    cfg.roll = {{"TETRIS", HEAD}, {"", WHITE}, {"CAST", HEAD},
                {"THE I", WHITE}, {"THE O", {1, 1, 0.2f}}, {"THE T", {0.2f, 1, 1}},
                {"S AND Z", {1, 0.25f, 0.2f}}, {"J AND L", {0.95f, 0.3f, 1}},
                {"AND", HEAD}, {std::to_string(lines) + " LINES", GREEN},
                {"", WHITE}, {"THANKS FOR PLAYING", WHITE}};
    cfg.ending = "GAME OVER";
    // A longer cast than most, so a little more time for it to roll.
    cfg.pRoll = 0.70;
    cfg.pEnd = 0.91;
    return cfg;
}

// For tools/shows/preview.sh.
inline void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story) {
    cfg = config(18420, 47, true, 12060);
    story = std::make_unique<Story>();
}

}  // namespace TetrisShow
