#include <fpp-pch.h>

#include "FPPBreakout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <list>
#include <random>
#include <unordered_map>
#include <vector>
#include <string>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"
#include "log.h"  // FPP logging

// Toggle to 0 if you don't want the little on-screen counter HUD
#define BREAKOUT_DEBUG_HUD 0

struct BrickTemplate {
    uint32_t color;
    int hitPoints;
    bool indestructible;
};

struct LevelDefinition {
    std::vector<std::string> rows;
};

static const std::unordered_map<char, BrickTemplate> BRICK_TYPES = {
    {'.', {0x000000, 0, false}},
    {'R', {0xFF4C4C, 1, false}},
    {'G', {0x4CFF79, 1, false}},
    {'B', {0x4CB0FF, 1, false}},
    {'Y', {0xFFF24C, 1, false}},
    {'O', {0xFF964C, 1, false}},
    {'P', {0xFF4CFF, 1, false}},
    {'C', {0x4CFFF2, 1, false}},
    {'W', {0xFFFFFF, 1, false}},
    {'2', {0xFF964C, 2, false}},
    {'H', {0x4C4CFF, 3, false}},
    {'I', {0xB0B0B0, 1, true}},
};

static const std::vector<LevelDefinition> LEVEL_LAYOUTS = {
    {   // Level 1: rainbow bands
        {
            "RRRRRRRRRRRRRRR",
            "YYYYYYYYYYYYYYY",
            "GGGGGGGGGGGGGGG",
            "BBBBBBBBBBBBBBB",
            "PPPPPPPPPPPPPPP"
        }
    },
    {   // Level 2: doorway pattern with two-hit cores
        {
            "..RRRR...RRRR..",
            "..RRRR...RRRR..",
            "G..22GGGGG22..G",
            "G..22GGGGG22..G",
            "BBBBBBBBBBBBBBB"
        }
    },
    {   // Level 3: space invader style with indestructible core
        {
            "..PP......PP...",
            "..PPPP...PPPP..",
            "..PPPPHHHPPPP..",
            "PPPPPHHHHHPPPPP",
            "PP..PP...PP..PP",
            "...PP.....PP..."
        }
    },
    {   // Level 4: maze with indestructible walls
        {
            "IPIIIIRRIIIIIPI",
            "I.............I",
            "I.RRGGPPCCYYO.I",
            "I.RRGGPPCCYYO.I",
            "I.............I",
            "IPIIIIRRIIIIIPI"
        }
    },
    {   // Level 5: diagonal dashes (good carom angles)
        {
            ".R..R..R..R..R.",
            "Y..Y..Y..Y..Y..",
            "..G..G..G..G..G",
            ".B..B..B..B..B.",
            "P..P..P..P..P.."
        }
    },
    {   // Level 6: checker with edge pillars + tougher core
        {
            "I.W2W2W2W2W2W.I",
            "W.W.W.W.W.W.W.W",
            "I.W2W2W2W2W2W.I",
            "W.W.W.W.W.W.W.W",
            "I.W2W2W2W2W2W.I"
        }
    },
    {   // Level 7: rainbow lanes with 2-hit guards
        {
            "..RRRRR2RRRRR..",
            "..YYYYY2YYYYY..",
            "..GGGGG2GGGGG..",
            "..BBBBB2BBBBB..",
            "..PPPPP2PPPPP.."
        }
    },
    {   // Level 8: tunnel (indestructible walls, 2-hit doors)
        {
            "...222...222...",
            "...222...222...",
            "...222...222...",
            "..............."
        }
    },
    {   // Level 9: staggered columns (mix of 1/2/3-hit)
        {
            "R..2..H..2..R..",
            ".R..2..H..2..R.",
            "..Y..2..H..2..Y",
            "G..H..2..H..G..",
            ".B..2..H..2..B.",
            "..P..H..2..H..P"
        }
    },
    {   // Level 10: zipper (fast side play)
        {
            "RR..RR..RR..RR.",
            ".YY..YY..YY..YY",
            "..GG..GG..GG..G",
            ".BB..BB..BB..BB",
            "PP..PP..PP..PP."
        }
    },
    {   // Level 11: center shield (break the core)
        {
            "......HHH......",
            ".....H222H.....",
            "....H21112H....",
            ".....H222H.....",
            "......HHH......"
        }
    },
    {   // Level 12: canyon (safe gutters, tough ridge)
        {
            "....CCCCCCC....",
            "...C2222222C...",
            "..C222HHH222C..",
            ".C222HHHHH222C.",
            "C.............C"
        }
    },
    {   // Level 13: rainbow chevrons
        {
            "RRRRR..........",
            ".YYYYY.........",
            "..GGGGG........",
            "...BBBBB.......",
            "....PPPPP......",
            "...BBBBB.......",
            "..GGGGG........",
            ".YYYYY.........",
            "RRRRR.........."
        }
    },
    {   // Level 14: window panes (lasers feel great here)
        {
            "IIIIIIIIIIIIIII",
            "I..RRR.I.GGG..I",
            "I..R2R.I.G2G..I",
            "I..RRR.I.GGG..I",
            "I.----III----.I",
            "I.............I"
        }
    },
    {   // Level 15: diamond core
        {
            ".......W.......",
            "......WWW......",
            ".....W2W2W.....",
            "....WWW2WWW....",
            "...W2W222W2W...",
            "....WWW2WWW....",
            ".....W2W2W.....",
            "......WWW......",
            ".......W......."
        }
    }
};

FPPBreakout::FPPBreakout(Json::Value &config) : FPPArcadeGame(config) {
    std::srand(time(NULL));
}
FPPBreakout::~FPPBreakout() {
}

struct Block {
    int r = 255;
    int g = 255;
    int b = 255;

    float y = 0;
    float x = 0;
    float width = 0;
    float height = 0;
    int row = -1;
    int col = -1;
    int hitPoints = 1;
    int maxHitPoints = 1;
    bool indestructible = false;

    float left() const { return x; }
    float top() const { return y; }
    float right() const { return x + width - 0.1f; }
    float bottom() const { return y + height - 0.1f; }

    void draw(PixelOverlayModel *m, float brightness = 1.0f) const {
        float hpScalar = maxHitPoints > 0 ? (0.6f + 0.4f * (static_cast<float>(hitPoints) / maxHitPoints)) : 1.0f;
        float adjusted = std::clamp(brightness * hpScalar, 0.0f, 1.0f);
        int rr = std::clamp(static_cast<int>(r * adjusted), 0, 255);
        int gg = std::clamp(static_cast<int>(g * adjusted), 0, 255);
        int bb = std::clamp(static_cast<int>(b * adjusted), 0, 255);
        for (int xp = 0; xp < (int)width; xp++) {
            for (int yp = 0; yp < (int)height; yp++) {
                m->setOverlayPixelValue((int)(xp + x), (int)(yp + y), rr, gg, bb);
            }
        }
    }

    bool intersects(const Block &mB) const {
        return right() >= mB.left() && left() <= mB.right() &&
               bottom() >= mB.top() && top() <= mB.bottom();
    }
};

struct Ball {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    float directionX = 0.25f;
    float directionY = 0.75f;
    float speed = 1.0f;
    bool stuck = false;
    float stickOffset = 0; // relative to paddle center

    float left() const { return x; }
    float right() const { return x + width - 0.1f; }
    float top() const { return y; }
    float bottom() const { return y + height - 0.1f; }

    void move(float scalar = 1.0f) {
        if (stuck) {
            return;
        }
        x += directionX * speed * scalar;
        y += directionY * speed * scalar;
    }
};

struct Laser {
    float x = 0;
    float y = 0;
    float speed = 4.0f;
};

class PowerUp {
public:
    enum class Type { Expand, Slow, Break, Sticky, Laser, Triple, ExtraLife, Portal };

    PowerUp(Type t, float px, float py, float size, float fall)
        : type(t), x(px), y(py), width(size), height(size), fallSpeed(fall) {
        switch (type) {
            case Type::Expand:
                r = 0; g = 200; b = 255;
                break;
            case Type::Slow:
                r = 255; g = 165; b = 0;
                break;
            case Type::Break:
                r = 255; g = 0; b = 255;
                break;
            case Type::Sticky:
                r = 0; g = 255; b = 120;
                break;
            case Type::Laser:
                r = 220; g = 0; b = 0;
                break;
            case Type::Triple:
                r = 173; g = 216; b = 230;
                break;
            case Type::ExtraLife:
                r = 255; g = 255; b = 255;
                break;
            case Type::Portal:
                r = 255; g = 20;  b = 147;
                break;
        }
    }

    void draw(PixelOverlayModel *m, float brightness = 1.0f) const {
        float scaled = std::clamp(brightness, 0.0f, 1.0f);
        int rr = std::clamp(static_cast<int>(r * scaled), 0, 255);
        int gg = std::clamp(static_cast<int>(g * scaled), 0, 255);
        int bb = std::clamp(static_cast<int>(b * scaled), 0, 255);
        int ix = static_cast<int>(std::round(x));
        int iy = static_cast<int>(std::round(y));
        for (int xp = 0; xp < (int)width; xp++) {
            for (int yp = 0; yp < (int)height; yp++) {
                m->setOverlayPixelValue(ix + xp, iy + yp, rr, gg, bb);
            }
        }
    }

    Type type;
    float x;
    float y;
    float width;
    float height;
    float fallSpeed;
    int r = 255;
    int g = 255;
    int b = 255;
};

class BreakoutEffect : public FPPArcadeGameEffect {
public:
    BreakoutEffect(PixelOverlayModel *m) : FPPArcadeGameEffect(m) {
        int w = m->getWidth();
        int h = m->getHeight();
        paddle.width = w / 8;
        paddle.x = (w - paddle.width) / 2;
        paddle.height = h / 64;
        if (paddle.height < 1) {
            paddle.height = 1;
        }
        paddle.y = h - 1 - paddle.height;
        basePaddleWidth = paddle.width;
        baseBallSpeed = std::max(1.0f, static_cast<float>(paddle.height));
        ballSpeedMultiplier = 1.0f;

        // Start new game with 3 lives
        lives = 3;

        loadLevel(0);
    }
    ~BreakoutEffect() {
    }

    const std::string &name() const override {
        static std::string NAME = "Breakout";
        return NAME;
    }

    // Spawn a fresh ball centered on the paddle; optionally stuck until button press
    void spawnBallOnPaddle(bool stuck = true) {
        Ball ball;
        ball.width  = paddle.height;
        ball.height = paddle.height;
        ball.speed  = baseBallSpeed * ballSpeedMultiplier;
        ball.x = paddle.x + paddle.width / 2.0f - ball.width / 2.0f;
        ball.y = paddle.y - ball.height - 1.0f;
        ball.directionX = 0.3f;
        ball.directionY = -0.7f;
        vec2_norm(ball.directionX, ball.directionY);
        ball.stuck = stuck;
        if (stuck) {
            float ballCenter = ball.x + ball.width / 2.0f;
            float paddleCenter = paddle.x + paddle.width / 2.0f;
            ball.stickOffset = ballCenter - paddleCenter;
            ball.y = paddle.y - ball.height;
        }
        balls.clear();
        balls.push_back(ball);
        setAllBallSpeeds();
    }

    void loadLevel(int levelIndex) {
        currentLevel = levelIndex;
        const LevelDefinition &level = LEVEL_LAYOUTS[currentLevel % LEVEL_LAYOUTS.size()];

        blocks.clear();
        powerUps.clear();
        lasers.clear();
        bricksAlive.clear();
        columnStarts.clear();
        rowStarts.clear();
        remainingDestructible = 0;

        paddle.width = basePaddleWidth;
        paddle.x = (model->getWidth() - paddle.width) / 2.0f;
        paddle.y = model->getHeight() - 1 - paddle.height;
        direction = 0;

        ballSpeedMultiplier = 1.0f + 0.1f * currentLevel;
        slowTimerMs = expandTimerMs = breakTimerMs = stickyTimerMs = laserTimerMs = 0.0;
        powerBallActive = false;
        stickyActive = false;
        laserActive = false;

        brickRows = (int)level.rows.size();
        brickCols = brickRows ? (int)level.rows.front().size() : 0;
        columnStarts.resize(brickCols, 0.0f);
        rowStarts.resize(brickRows, 0.0f);
        bricksAlive.assign(brickRows, std::vector<bool>(brickCols, false));

        if (brickCols == 0 || brickRows == 0) {
            return;
        }

        float w = static_cast<float>(model->getWidth());
        float h = static_cast<float>(model->getHeight());

        float computedWidth = std::max(2.0f, std::floor((w * 0.8f) / brickCols));
        brickWidth = static_cast<int>(computedWidth);
        float remainingWidth = w - brickWidth * brickCols;
        float horizontalGap = std::max(1.0f, std::floor(remainingWidth / (brickCols + 1)));
        float curX = horizontalGap;
        for (int c = 0; c < brickCols; ++c) {
            columnStarts[c] = curX;
            curX += brickWidth + horizontalGap;
        }

        float computedHeight = std::max(1.0f, std::floor((h * 0.35f) / brickRows));
        brickHeight = static_cast<int>(computedHeight);
        float playableHeight = static_cast<float>(paddle.y) - brickHeight;
        float remainingHeight = std::max(0.0f, playableHeight - brickRows * brickHeight);
        float verticalGap = std::max(1.0f, std::floor(remainingHeight / std::max(1, brickRows + 1)));
        float curY = verticalGap;
        for (int r = 0; r < brickRows; ++r) {
            rowStarts[r] = curY;
            curY += brickHeight + verticalGap;
        }

        for (int row = 0; row < brickRows; ++row) {
            const std::string &line = level.rows[row];
            for (int col = 0; col < brickCols && col < (int)line.size(); ++col) {
                char ch = line[col];
                auto tmplIt = BRICK_TYPES.find(ch);
                if (tmplIt == BRICK_TYPES.end()) {
                    continue;
                }
                const BrickTemplate &tmpl = tmplIt->second;
                if (tmpl.hitPoints <= 0) {
                    continue;
                }
                Block b;
                b.x = columnStarts[col];
                b.y = rowStarts[row];
                b.width = brickWidth;
                b.height = brickHeight;
                b.r = (tmpl.color >> 16) & 0xFF;
                b.g = (tmpl.color >> 8) & 0xFF;
                b.b =  tmpl.color        & 0xFF;
                b.row = row;
                b.col = col;
                b.hitPoints = tmpl.hitPoints;
                b.maxHitPoints = tmpl.hitPoints;
                b.indestructible = tmpl.indestructible;
                blocks.push_back(b);
                bricksAlive[row][col] = true;
                if (!b.indestructible) {
                    ++remainingDestructible;
                }
            }
        }

        // Start level with ball staged on the paddle
        spawnBallOnPaddle(/*stuck=*/true);

        showingLevelIntro = true;
        levelIntroTimerMs = 1500.0;
        levelIntroText = "LEVEL " + std::to_string(currentLevel + 1);
        GameOn = true;
        WaitingUntilOutput = false;
        resetFrameTimer();

        LogInfo(VB_PLUGIN, "[Breakout] Level %d loaded: destructible=%d, totalBlocks=%zu",
                currentLevel + 1, remainingDestructible, blocks.size());
    }

    void moveBalls(float frameScalar) {
        constexpr float gapPadding = 1.0f;
        auto ballIt = balls.begin();
        while (ballIt != balls.end()) {
            Ball &ball = *ballIt;
            if (ball.stuck) {
                float paddleCenter = paddle.x + paddle.width / 2.0f;
                float newCenter = paddleCenter + ball.stickOffset;
                ball.x = newCenter - ball.width / 2.0f;
                ball.y = paddle.y - ball.height;
                ++ballIt;
                continue;
            } else {
                ball.move(frameScalar);
            }
            if (ball.y < 0) {
                ball.directionY = std::fabs(ball.directionY);
                ball.y = 0;
            }
            if (ball.x < 0) {
                ball.directionX = std::fabs(ball.directionX);
                ball.x = 0;
            }
            if (ball.x >= model->getWidth()) {
                ball.directionX = -std::fabs(ball.directionX);
                ball.x = model->getWidth() - 1;
            }

            bool brickHit = false;
            auto it = blocks.begin();
            while (it != blocks.end()) {
                float bLeft = it->left() - gapPadding;
                float bRight = it->right() + gapPadding;
                float bTop = it->top() - gapPadding;
                float bBottom = it->bottom() + gapPadding;

                bool overlaps = (ball.right() >= bLeft && ball.left() <= bRight &&
                                  ball.bottom() >= bTop && ball.top() <= bBottom);
                if (!overlaps) {
                    ++it;
                    continue;
                }
                if (!powerBallActive || it->indestructible) {
                    float overlapLeft = ball.right() - bLeft;
                    float overlapRight = bRight - ball.left();
                    float overlapTop = ball.bottom() - bTop;
                    float overlapBottom = bBottom - ball.top();

                    float minHoriz = std::min(overlapLeft, overlapRight);
                    float minVert = std::min(overlapTop, overlapBottom);
                    if (minHoriz < minVert) {
                        if (overlapLeft < overlapRight) {
                            ball.directionX = -std::fabs(ball.directionX);
                            ball.x = bLeft - ball.width;
                        } else {
                            ball.directionX = std::fabs(ball.directionX);
                            ball.x = bRight;
                        }
                    } else {
                        if (overlapTop < overlapBottom) {
                            ball.directionY = -std::fabs(ball.directionY);
                            ball.y = bTop - ball.height;
                        } else {
                            ball.directionY = std::fabs(ball.directionY);
                            ball.y = bBottom;
                        }
                    }
                }

                bool destroyed = false;
                if (!it->indestructible) {
                    if (powerBallActive) {
                        destroyed = true;
                    } else {
                        if (it->hitPoints > 1) {
                            it->hitPoints--;
                        } else {
                            destroyed = true;
                        }
                    }
                }

                if (destroyed) {
                    if (remainingDestructible > 0) --remainingDestructible;
                    maybeSpawnPowerUp(*it);
                    markBrickGone(it->row, it->col);
                    it = blocks.erase(it);
                    LogDebug(VB_PLUGIN, "[Breakout] destroyed by ball, remainingDestructible=%d", remainingDestructible);
                } else {
                    ++it;
                }
                brickHit = true;
                break;
            }

            if (!brickHit) {
                handleGapCollision(ball);
            }

            if (ball.y >= model->getHeight()) {
                ballIt = balls.erase(ballIt);
                continue;
            }

            ++ballIt;
        }
    }

    void handleGapCollision(Ball &ball) {
        if (brickRows == 0 || brickCols <= 1) return;

        // If the ball overlaps the y-band of any brick row and its center is
        // inside the seam between two *alive* bricks, reflect vertically to block passage.
        const float bxCenter = ball.x + ball.width * 0.5f;

        for (int row = 0; row < brickRows; ++row) {
            float topY = rowStarts[row];
            float bottomY = topY + brickHeight;
            if (ball.bottom() < topY || ball.top() > bottomY) continue;

            for (int col = 0; col < brickCols - 1; ++col) {
                if (!(bricksAlive[row][col] && bricksAlive[row][col + 1])) continue;

                // Seam between adjacent bricks in this row
                float gapStart = columnStarts[col] + brickWidth;
                float gapEnd   = columnStarts[col + 1];

                // Treat the seam as CLOSED by giving it a small thickness.
                // This keeps the ball from entering visually dotted rows.
                float closedStart = gapStart - 0.5f;
                float closedEnd   = gapEnd   + 0.5f;

                if (bxCenter >= closedStart && bxCenter <= closedEnd) {
                    // Bounce off the “seam” like it were a solid band
                    if (ball.directionY > 0) {
                        ball.directionY = -std::fabs(ball.directionY);
                        ball.y = topY - ball.height;
                    } else {
                        ball.directionY =  std::fabs(ball.directionY);
                        ball.y = bottomY;
                    }
                    return;
                }
            }
        }
    }

    void markBrickGone(int row, int col) {
        if (row >= 0 && row < (int)bricksAlive.size() &&
            col >= 0 && col < (int)bricksAlive[row].size()) {
            bricksAlive[row][col] = false;
        }
    }

    // Helper: dump any remaining destructible bricks with coords/HP
    void dumpRemainingBricks() const {
        for (const auto& b : blocks) {
            if (!b.indestructible) {
                LogInfo(VB_PLUGIN, "[Breakout] remaining brick row=%d col=%d x=%.1f y=%.1f hp=%d",
                        b.row, b.col, b.x, b.y, b.hitPoints);
            }
        }
    }

    int32_t handleLevelClear() {
        currentLevel++;
        if (currentLevel < (int)LEVEL_LAYOUTS.size()) {
            LogInfo(VB_PLUGIN, "[Breakout] Advancing to level %d", currentLevel + 1);
            loadLevel(currentLevel);
            CopyToModel();
            return 50;
        }

        GameOn = false;
        showingLevelIntro = false;
        float scl = std::max(1.0f, paddle.height);
        CopyToModel(0.2f);
        outputString("YOU", centeredTextX("YOU", scl),
                    (model->getHeight()/2 - (6 * scl)) / scl, 255, 255, 255, scl);
        outputString("WIN", centeredTextX("WIN", scl),
                    (model->getHeight()/2) / scl, 255, 255, 255, scl);
        model->flushOverlayBuffer();
        return 2000;
    }

    void maybeSpawnPowerUp(const Block &from) {
        PowerUp::Type selected;
        if (!pickPowerUp(selected)) {
            return;
        }
        float size = std::max(2.0f, paddle.height * 1.5f);
        float px = from.x + (from.width / 2.0f) - (size / 2.0f);
        float py = from.y + from.height; // start just below the brick
        float fall = std::max(0.25f, static_cast<float>(model->getHeight()) / 160.0f);
        powerUps.emplace_back(selected, px, py, size, fall);
    }

    bool pickPowerUp(PowerUp::Type &type) {
        struct Entry { PowerUp::Type type; int weight; };
        static const std::array<Entry, 8> table = {{
            {PowerUp::Type::Break,     12},
            {PowerUp::Type::Expand,    12},
            {PowerUp::Type::Slow,       5},
            {PowerUp::Type::Sticky,     5},
            {PowerUp::Type::Laser,      5},
            {PowerUp::Type::Triple,     4},
            {PowerUp::Type::ExtraLife,  2},
            {PowerUp::Type::Portal,     2}
        }};
        if ((std::rand() % 100) >= 12) {
            return false;
        }
        int totalWeight = 0;
        for (const auto &entry : table) totalWeight += entry.weight;
        int roll = std::rand() % totalWeight;
        int accum = 0;
        for (const auto &entry : table) {
            accum += entry.weight;
            if (roll < accum) {
                type = entry.type;
                return true;
            }
        }
        return false;
    }

    void CopyToModel(float brightness = 1.0f) {
        float scaled = std::clamp(brightness, 0.0f, 1.0f);
        model->clearOverlayBuffer();
        for (auto &b : blocks) {
            b.draw(model, scaled);
        }
        paddle.draw(model, scaled);
        int ballColor = std::clamp(static_cast<int>(255 * scaled), 0, 255);
        for (auto &ball : balls) {
            int ix = static_cast<int>(std::round(ball.x));
            int iy = static_cast<int>(std::round(ball.y));
            int bw = std::max(1, static_cast<int>(std::round(ball.width)));
            int bh = std::max(1, static_cast<int>(std::round(ball.height)));
            for (int xp = 0; xp < bw; xp++) {
                for (int yp = 0; yp < bh; yp++) {
                    model->setOverlayPixelValue(ix + xp, iy + yp, ballColor, ballColor, ballColor);
                }
            }
        }
        for (auto &p : powerUps) {
            p.draw(model, scaled);
        }
        int laserColor = std::clamp(static_cast<int>(255 * scaled), 0, 255);
        for (auto &l : lasers) {
            int ix = static_cast<int>(std::round(l.x));
            int tip = static_cast<int>(std::round(l.y));
            int start = std::clamp(tip - 3, 0, model->getHeight() - 1);
            int end = std::clamp(tip + 1, 0, model->getHeight() - 1);
            for (int y = end; y >= start; --y) {
                model->setOverlayPixelValue(ix, y, laserColor, 0, 0);
            }
        }

        // --- Life indicator: top-left row, one pixel per life (white) ---
        for (int i = 0; i < lives; ++i) {
            model->setOverlayPixelValue(i, 0, 255, 255, 255);
        }

        // Draw the portal if open: three pulsing vertical pixels at bottom-right
        if (portalOpenTimerMs > 0.0) {
            int x = std::max(0, model->getWidth() - 1);
            int h = model->getHeight();
            float pulse = 0.5f + 0.5f * std::sin(portalPulsePhase);
            int pr = static_cast<int>(255 * pulse);
            int pg = static_cast<int>(20  * pulse);
            int pb = static_cast<int>(147 * pulse);
            for (int i = 0; i < 3; ++i) {
                int y = std::max(0, h - 1 - i);
                model->setOverlayPixelValue(x, y, pr, pg, pb);
            }
        }

#if BREAKOUT_DEBUG_HUD
        // Bottom-left tiny HUD showing remaining destructible bricks (scaled to glyph grid)
        int scl = std::max(1, (int)paddle.height);
        int gridH = model->getHeight() / scl;
        int hudY = std::max(0, gridH - 6);
        outputString(std::to_string(std::max(0, remainingDestructible)), 0, hudY, 255, 255, 255, scl);
#endif

        model->flushOverlayBuffer();
    }

    void updatePowerUps(float frameScalar) {
        auto it = powerUps.begin();
        while (it != powerUps.end()) {
            it->y += it->fallSpeed * frameScalar;
            if (it->y >= model->getHeight()) {
                it = powerUps.erase(it);
                continue;
            }
            bool caught = (it->y + it->height) >= paddle.y &&
                          (it->x + it->width) >= paddle.x &&
                          it->x <= (paddle.x + paddle.width);
            if (caught) {
                applyPowerUp(it->type);
                it = powerUps.erase(it);
            } else {
                ++it;
            }
        }
    }

    void applyPowerUp(PowerUp::Type type) {
        constexpr int durationMs = 8000;
        switch (type) {
            case PowerUp::Type::Expand:
                paddle.width = std::min<float>(model->getWidth() * 0.8f,
                                               paddle.width + basePaddleWidth * 0.5f);
                expandTimerMs = durationMs;
                enforcePaddleBounds();
                break;
            case PowerUp::Type::Slow:
                ballSpeedMultiplier = 0.6f;
                setAllBallSpeeds();
                slowTimerMs = durationMs;
                break;
            case PowerUp::Type::Break:
                powerBallActive = true;
                breakTimerMs = durationMs;
                break;
            case PowerUp::Type::Sticky:
                stickyActive = true;
                stickyTimerMs = durationMs;
                break;
            case PowerUp::Type::Laser:
                laserActive = true;
                laserTimerMs = durationMs;
                break;
            case PowerUp::Type::Triple:
                spawnTripleBalls();
                break;
            case PowerUp::Type::ExtraLife:
                if (lives < MAX_LIVES) {
                    ++lives;
                }
                break;
            case PowerUp::Type::Portal:
                // Open visual portal and schedule level advance shortly.
                portalOpenTimerMs = 1500.0;
                break;
        }
    }

    void enforcePaddleBounds() {
        if (paddle.x < 0) {
            paddle.x = 0;
        }
        if ((paddle.x + paddle.width) > model->getWidth()) {
            paddle.x = model->getWidth() - paddle.width;
        }
    }

    void updatePowerUpTimers(double elapsedMs) {
        if (slowTimerMs > 0.0) {
            slowTimerMs -= elapsedMs;
            if (slowTimerMs <= 0.0) {
                ballSpeedMultiplier = 1.0f;
                setAllBallSpeeds();
                slowTimerMs = 0.0;
            }
        }
        if (expandTimerMs > 0.0) {
            expandTimerMs -= elapsedMs;
            if (expandTimerMs <= 0.0) {
                paddle.width = basePaddleWidth;
                enforcePaddleBounds();
                expandTimerMs = 0.0;
            }
        }
        if (breakTimerMs > 0.0) {
            breakTimerMs -= elapsedMs;
            if (breakTimerMs <= 0.0) {
                powerBallActive = false;
                breakTimerMs = 0.0;
            }
        }
        if (stickyTimerMs > 0.0) {
            stickyTimerMs -= elapsedMs;
            if (stickyTimerMs <= 0.0) {
                releaseStuckBalls();
                stickyTimerMs = 0.0;
            }
        }
        if (laserTimerMs > 0.0) {
            laserTimerMs -= elapsedMs;
            if (laserTimerMs <= 0.0) {
                laserActive = false;
                laserTimerMs = 0.0;
            }
        }
    }

    void setAllBallSpeeds() {
        for (auto &ball : balls) {
            ball.speed = baseBallSpeed * ballSpeedMultiplier;
        }
    }

    void releaseStuckBalls(bool keepEffect = false) {
        for (auto &ball : balls) {
            if (ball.stuck) {
                ball.stuck = false;
                float offsetNorm = 0.0f;
                if (paddle.width > 0.0f) {
                    offsetNorm = ball.stickOffset / (paddle.width * 0.5f);
                }
                offsetNorm = std::clamp(offsetNorm, -1.0f, 1.0f);
                if (std::fabs(offsetNorm) < 0.1f) {
                    if (ball.stickOffset > 0.0f)      offsetNorm = 0.2f;
                    else if (ball.stickOffset < 0.0f) offsetNorm = -0.2f;
                    else                                offsetNorm = 0.0f;
                }
                ball.directionX = offsetNorm;

                float dirY = ball.directionY;
                if (std::fabs(dirY) < 0.1f) dirY = -0.75f;
                dirY = -std::fabs(dirY);
                ball.directionY = dirY;
                vec2_norm(ball.directionX, ball.directionY);
                ball.x = (paddle.x + paddle.width / 2.0f) + ball.stickOffset - ball.width / 2.0f;
                ball.y = paddle.y - ball.height;
            }
        }
        if (!keepEffect) {
            stickyActive = false;
            stickyTimerMs = 0.0;
        }
    }

    void spawnTripleBalls() {
        std::vector<Ball> additions;
        for (auto &ball : balls) {
            Ball left = ball;
            Ball right = ball;
            left.stuck = right.stuck = false;
            left.directionX -= 0.3f;
            right.directionX += 0.3f;
            left.directionY = -std::fabs(left.directionY == 0 ? 0.75f : left.directionY);
            right.directionY = -std::fabs(right.directionY == 0 ? 0.75f : right.directionY);
            vec2_norm(left.directionX, left.directionY);
            vec2_norm(right.directionX, right.directionY);
            left.speed = right.speed = baseBallSpeed * ballSpeedMultiplier;
            additions.push_back(left);
            additions.push_back(right);
        }
        balls.insert(balls.end(), additions.begin(), additions.end());
    }

    void fireLasers() {
        if (!laserActive) return;
        float centerX = paddle.x + paddle.width / 2.0f;
        lasers.push_back({centerX, paddle.y - 1, 4.0f});
    }

    void updateLasers(float frameScalar) {
        auto it = lasers.begin();
        while (it != lasers.end()) {
            float prevY = it->y;
            it->y -= it->speed * frameScalar;
            if (it->y < 0) {
                it = lasers.erase(it);
                continue;
            }

            auto targetIt = blocks.end();
            float bestBottom = -1;
            for (auto brickIt = blocks.begin(); brickIt != blocks.end(); ++brickIt) {
                if (it->x >= brickIt->left() && it->x <= brickIt->right()) {
                    if (brickIt->bottom() < prevY && brickIt->bottom() > bestBottom) {
                        bestBottom = brickIt->bottom();
                        targetIt = brickIt;
                    }
                }
            }
            if (targetIt != blocks.end()) {
                bool destroyed = false;
                if (!targetIt->indestructible) {
                    if (targetIt->hitPoints > 1) {
                        targetIt->hitPoints--;
                    } else {
                        destroyed = true;
                    }
                }
                if (destroyed) {
                    if (remainingDestructible > 0) --remainingDestructible;
                    maybeSpawnPowerUp(*targetIt);
                    markBrickGone(targetIt->row, targetIt->col);
                    blocks.erase(targetIt);
                    LogDebug(VB_PLUGIN, "[Breakout] destroyed by laser, remainingDestructible=%d", remainingDestructible);
                }
                it = lasers.erase(it);
            } else {
                ++it;
            }
        }
    }

    void removeLostBalls() {
        balls.erase(std::remove_if(balls.begin(), balls.end(), [&](const Ball &ball) {
            return ball.y >= model->getHeight();
        }), balls.end());
    }

    int centeredTextX(const std::string &text, float scl) const {
        if (scl <= 0) return 0;
        int glyphWidth = 4;
        int gridWidth = (int)(model->getWidth() / scl);
        int textWidth = glyphWidth * (int)text.size();
        int pos = (gridWidth - textWidth) / 2;
        return std::max(0, pos);
    }

    // Count live destructible bricks (belt & suspenders for win detect)
    int countDestructible() const {
        return std::count_if(blocks.begin(), blocks.end(),
            [](const Block& b){ return !b.indestructible; });
    }

    virtual int32_t update() override {
        if (!GameOn) {
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            if (WaitingUntilOutput) {
                model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
                return 0;
            }
            WaitingUntilOutput = true;
            return -1;
        }
        constexpr double baseFrameMs = 50.0;
        double elapsedMs = consumeElapsedMs(baseFrameMs);
        if (elapsedMs <= 0.0) elapsedMs = baseFrameMs;
        double frameScalar = elapsedMs / baseFrameMs;
        frameScalar = std::clamp(frameScalar, 0.1, 5.0);

        if (showingLevelIntro) {
            levelIntroTimerMs -= elapsedMs;
            CopyToModel(0.2f);
            int scl = std::max(1, (int)paddle.height);
            int introY = (int)((model->getHeight()/2 - (6 * scl)) / scl);
            if (introY < 0) introY = 0;
            outputString(levelIntroText, centeredTextX(levelIntroText, scl), introY, 255, 255, 255, scl);
            model->flushOverlayBuffer();
            if (levelIntroTimerMs <= 0.0) {
                showingLevelIntro = false;
                resetFrameTimer();
            }
            return 50;
        }

        // Advance portal pulse and handle timed jump to next level.
        portalPulsePhase += elapsedMs * 0.02;
        if (portalOpenTimerMs > 0.0) {
            portalOpenTimerMs -= elapsedMs;
            if (portalOpenTimerMs <= 0.0) {
                return handleLevelClear();
            }
        }

        paddle.x += direction * paddle.height * frameScalar;
        if (paddle.x < 0) paddle.x = 0;
        else if ((paddle.x + paddle.width) >= model->getWidth()) paddle.x = model->getWidth() - paddle.width;

        moveBalls((float)frameScalar);
        updatePowerUps((float)frameScalar);
        updatePowerUpTimers(elapsedMs);
        updateLasers((float)frameScalar);

        for (auto &ball : balls) {
            if (ball.bottom() >= paddle.y && ball.left() <= (paddle.x + paddle.width) && ball.right() >= paddle.x && ball.directionY > 0) {
                if (!stickyActive) {
                    float t = ((ball.x - paddle.x) / paddle.width) - 0.5f;
                    ball.directionY = -std::fabs(ball.directionY);
                    ball.directionX = t;
                    vec2_norm(ball.directionX, ball.directionY);
                } else {
                    ball.stuck = true;
                    ball.directionX = 0;
                    ball.directionY = 0;
                    float ballCenter = ball.x + ball.width / 2.0f;
                    float paddleCenter = paddle.x + paddle.width / 2.0f;
                    ball.stickOffset = ballCenter - paddleCenter;
                    ball.y = paddle.y - ball.height;
                }
            }
        }

        removeLostBalls();

        // When close to clear, log both counters and dump any stragglers
        if (remainingDestructible <= 10) {
            int liveDestructibleDbg = countDestructible();
            LogInfo(VB_PLUGIN, "[Breakout] Near clear: rem=%d live=%d blocks=%zu",
                    std::max(0, remainingDestructible), liveDestructibleDbg, blocks.size());
            if (liveDestructibleDbg > 0 && liveDestructibleDbg <= 6) {
                dumpRemainingBricks();
            }
        }

        // ✅ Win condition (counter or live scan)
        if (remainingDestructible < 0) remainingDestructible = 0;
        int liveDestructible = countDestructible();
        if (remainingDestructible == 0 || liveDestructible == 0 || blocks.empty()) {
            LogInfo(VB_PLUGIN, "[Breakout] LEVEL CLEARED (level=%d) rem=%d live=%d size=%zu",
                    currentLevel + 1, remainingDestructible, liveDestructible, blocks.size());
            return handleLevelClear();
        }

        // Draw the frame only if we’re staying on this level
        CopyToModel();

        // ❌ Loss condition (no balls left)
        if (balls.empty()) {
            if (lives > 0) {
                --lives;
                LogInfo(VB_PLUGIN, "[Breakout] Life lost. Lives remaining=%d (level %d)", lives, currentLevel + 1);

                // Clear transient power-ups/effects on death
                powerUps.clear();
                lasers.clear();
                powerBallActive = false;
                stickyActive = false;
                laserActive = false;
                slowTimerMs = 0.0;
                expandTimerMs = 0.0;
                breakTimerMs = 0.0;
                stickyTimerMs = 0.0;
                laserTimerMs = 0.0;

                // Reset paddle to base size so Expand doesn't persist after death
                paddle.width = basePaddleWidth;
                enforcePaddleBounds();

                // Re-center paddle, respawn a stuck ball
                paddle.x = (model->getWidth() - paddle.width) / 2.0f;
                enforcePaddleBounds();
                spawnBallOnPaddle(/*stuck=*/true);

                // Brief pause so the player perceives the life loss
                CopyToModel(0.2f);
                model->flushOverlayBuffer();
                return 300; // or 250 if you prefer snappier
            } else {
                GameOn = false;
                LogInfo(VB_PLUGIN, "[Breakout] GAME OVER on level %d", currentLevel + 1);
                float scl = paddle.height;
                CopyToModel(0.2f);
                outputString("GAME", centeredTextX("GAME", scl),
                             (model->getHeight()/2-(6 * scl)) / scl, 255, 255, 255, scl);
                outputString("OVER", centeredTextX("OVER", scl),
                             (model->getHeight()/2) / scl, 255, 255, 255, scl);
                model->flushOverlayBuffer();
                return 2000;
            }
        }

        // Normal frame cadence
        return 50;
    } // end update()

    // Returns true if at least one ball is currently stuck to the paddle.
    // This lets us launch the ball on A/B even when Sticky isn't active.
    bool anyBallStuck() const {
        for (const auto& b : balls) {
            if (b.stuck) return true;
        }
        return false;
    }

    void vec2_norm(float& x, float &y) {
        float length = std::sqrt((x * x) + (y * y));
        if (length != 0.0f) {
            length = 1.0f / length;
            x *= length;
            y *= length;
        }
    }

    void button(const std::string &button) {
        // Normalize a couple common variants so we're tolerant of old/new emitters.
        const bool isPressed = (button.find("Pressed") != std::string::npos) || (button == "Fire");
        const bool isLeftPress   = (button == "Left - Pressed");
        const bool isLeftRelease = (button == "Left - Released");
        const bool isRightPress  = (button == "Right - Pressed");
        const bool isRightRelease= (button == "Right - Released");

        // Back-compat: treat "Fire - Pressed" (and plain "Fire") the same as A/B.
        const bool isFirePress =
            isPressed && (
                button == "A Button - Pressed" ||
                button == "B Button - Pressed" ||
                button == "Fire - Pressed"     ||
                button == "Fire"               ||
                button == "Fire Button - Pressed"
            );

        if (isLeftPress) {
            direction = -1;
        } else if (isLeftRelease) {
            if (direction < 0) direction = 0;
        } else if (isRightPress) {
            direction = 1;
        } else if (isRightRelease) {
            if (direction > 0) direction = 0;
        } else if (isFirePress) {
            // Launch if any ball is stuck; otherwise fire lasers when active.
            if (anyBallStuck()) {
                // keepEffect=true so Sticky (if active) continues counting down.
                releaseStuckBalls(/*keepEffect=*/true);
            } else if (laserActive) {
                fireLasers();
            }
        }
    }

    Block paddle;
    std::list<Block> blocks;
    std::vector<Ball> balls;
    std::list<PowerUp> powerUps;
    std::vector<Laser> lasers;
    int brickRows = 0;
    int brickCols = 0;
    int brickWidth = 0;
    int brickHeight = 0;
    std::vector<float> columnStarts;
    std::vector<float> rowStarts;
    std::vector<std::vector<bool>> bricksAlive;
    int remainingDestructible = 0;
    int direction = 0;
    float basePaddleWidth = 0;
    float baseBallSpeed = 0;
    float ballSpeedMultiplier = 1.0f;
    double slowTimerMs = 0.0;
    double expandTimerMs = 0.0;
    double breakTimerMs = 0.0;
    double stickyTimerMs = 0.0;
    double laserTimerMs = 0.0;
    bool powerBallActive = false;
    bool stickyActive = false;
    bool laserActive = false;
    int currentLevel = 0;
    bool showingLevelIntro = false;
    double levelIntroTimerMs = 0.0;
    std::string levelIntroText;

    // --- Lives ---
    int lives = 3;
    static constexpr int MAX_LIVES = 5;

    bool GameOn = true;
    bool WaitingUntilOutput = false;
    // Portal visuals/timer
    double portalOpenTimerMs = 0.0;
    double portalPulsePhase = 0.0;
};

const std::string &FPPBreakout::getName() {
    static const std::string name = "Breakout";
    return name;
}

void FPPBreakout::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m != nullptr) {
        BreakoutEffect *effect = dynamic_cast<BreakoutEffect*>(m->getRunningEffect());
        if (!effect) {
            if (findOption("overlay", "Overwrite") == "Transparent") {
                m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
            } else {
                m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
            }
            effect = new BreakoutEffect(m);
            m->setRunningEffect(effect, 50);
        } else {
            effect->button(button);
        }
    }
}
