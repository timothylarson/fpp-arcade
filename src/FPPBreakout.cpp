#include <fpp-pch.h>

#include "FPPBreakout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <list>
#include <random>
#include <vector>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"


FPPBreakout::FPPBreakout(Json::Value &config) : FPPArcadeGame(config) {
    std::srand(time(NULL));
}
FPPBreakout::~FPPBreakout() {
}


class Block {
public:
    int r = 255;
    int g = 255;
    int b = 255;

    float y = 0;
    float x = 0;
    float width = 0;
    float height = 0;
    int row = -1;
    int col = -1;
    
    float left() const { return x; }
    float top() const { return y; }
    float right() const { return x + width - 0.1; }
    float bottom() const { return y + height - 0.1; }

    void draw(PixelOverlayModel *m, float brightness = 1.0f) const {
        int rr = std::clamp(static_cast<int>(r * brightness), 0, 255);
        int gg = std::clamp(static_cast<int>(g * brightness), 0, 255);
        int bb = std::clamp(static_cast<int>(b * brightness), 0, 255);
        for (int xp = 0; xp < width; xp++) {
            for (int yp = 0; yp < height; yp++) {
                m->setOverlayPixelValue(xp + x, yp + y, rr, gg, bb);
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

    void move() {
        if (stuck) {
            return;
        }
        x += directionX * speed;
        y += directionY * speed;
    }
};

struct Laser {
    float x = 0;
    float y = 0;
    float speed = 4.0f;
};

class PowerUp {
public:
    enum class Type { Expand, Slow, Break, Sticky, Laser, Triple };

    PowerUp(Type t, float px, float py, float size, float fall)
        : type(t), x(px), y(py), width(size), height(size), fallSpeed(fall) {
        switch (type) {
            case Type::Expand:
                r = 0; g = 200; b = 255;
                break;
            case Type::Slow:
                r = 255; g = 215; b = 0;
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
                r = 255; g = 255; b = 255;
                break;
        }
    }

    void draw(PixelOverlayModel *m) const {
        int ix = static_cast<int>(std::round(x));
        int iy = static_cast<int>(std::round(y));
        for (int xp = 0; xp < width; xp++) {
            for (int yp = 0; yp < height; yp++) {
                m->setOverlayPixelValue(ix + xp, iy + yp, r, g, b);
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


static std::array<uint32_t, 7> COLORS = {
    0xFF0000,
    0x00FF00,
    0x0000FF,
    0xFF00FF,
    0xFFFF00,
    0x00FFFF,
    0xFFFFFF,
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
        
        
        int blockW = w / 20;
        if (blockW < 2) {
            blockW = 2;
        }
        int blockH = h / 32;
        if (blockH < 1) {
            blockH = 1;
        }
        
        int numBlockW = w / (blockW+1);
        int curY = blockH*2;
        int xOff = (w - numBlockW * (blockW+1)) / 2;
        brickCols = numBlockW;
        brickRows = COLORS.size();
        brickWidth = blockW;
        brickHeight = blockH;
        columnStarts.resize(brickCols);
        for (int x = 0; x < numBlockW; x++) {
            columnStarts[x] = xOff + x * (blockW + 1);
        }
        rowStarts.resize(brickRows);
        bricksAlive.assign(brickRows, std::vector<bool>(brickCols, false));
        for (int y = 0; y < brickRows; y++, curY += (blockH+1) ) {
            rowStarts[y] = curY;
            for (int x = 0; x < brickCols; x++) {
                Block b;
                b.x = columnStarts[x];
                b.height = blockH;
                b.width = blockW;
                b.y = curY;
                b.r = (COLORS[y] >> 16) & 0xFF;
                b.g = (COLORS[y] >> 8) & 0xFF;
                b.b = COLORS[y] & 0xFF;
                b.row = y;
                b.col = x;
                blocks.push_back(b);
                bricksAlive[y][x] = true;
            }
        }
        
        Ball b;
        b.x = w / 2;
        b.y = h * 2 / 3;
        b.height = paddle.height;
        b.width = paddle.height;
        b.speed *= (float)paddle.height;
        baseBallSpeed = b.speed;
        balls.push_back(b);
        CopyToModel();
    }
    ~BreakoutEffect() {
    }
    
    const std::string &name() const override {
        static std::string NAME = "Breakout";
        return NAME;
    }

    void moveBalls() {
        constexpr float gapPadding = 0.4f;
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
                ball.move();
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
                if (!powerBallActive) {
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

                maybeSpawnPowerUp(*it);
                markBrickGone(it->row, it->col);
                it = blocks.erase(it);
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
        if (brickRows == 0 || brickCols <= 1) {
            return;
        }
        for (int row = 0; row < brickRows; ++row) {
            float topY = rowStarts[row];
            float bottomY = topY + brickHeight;
            if (ball.bottom() < topY || ball.top() > bottomY) {
                continue;
            }
            for (int col = 0; col < brickCols - 1; ++col) {
                if (!(bricksAlive[row][col] && bricksAlive[row][col + 1])) {
                    continue;
                }
                float gapStart = columnStarts[col] + brickWidth;
                float gapEnd = columnStarts[col + 1];
                if (ball.right() <= gapStart || ball.left() >= gapEnd) {
                    continue;
                }
                if (ball.directionY > 0) {
                    ball.directionY = -std::fabs(ball.directionY);
                    ball.y = gapStart - ball.height;
                } else {
                    ball.directionY = std::fabs(ball.directionY);
                    ball.y = gapEnd;
                }
                return;
            }
        }
    }

    void markBrickGone(int row, int col) {
        if (row >= 0 && row < (int)bricksAlive.size() &&
            col >= 0 && col < (int)bricksAlive[row].size()) {
            bricksAlive[row][col] = false;
        }
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
        // Approximate Arkanoid drop frequencies for E (Expand), B (Break), S (Slow)
        struct Entry { PowerUp::Type type; int weight; };
        static const std::array<Entry, 6> table = {{{PowerUp::Type::Break, 12},
                                                   {PowerUp::Type::Expand, 12},
                                                   {PowerUp::Type::Slow, 5},
                                                   {PowerUp::Type::Sticky, 5},
                                                   {PowerUp::Type::Laser, 5},
                                                   {PowerUp::Type::Triple, 4}}};
        // Only allow a drop roughly 12% of the time.
        if ((std::rand() % 100) >= 12) {
            return false;
        }
        int totalWeight = 0;
        for (const auto &entry : table) {
            totalWeight += entry.weight;
        }
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
        model->clearOverlayBuffer();
        for (auto &b : blocks) {
            b.draw(model, brightness);
        }
        paddle.draw(model, brightness);
        for (auto &ball : balls) {
            int r = 255, g = 255, bl = 255;
            int ix = static_cast<int>(std::round(ball.x));
            int iy = static_cast<int>(std::round(ball.y));
            int bw = std::max(1, static_cast<int>(std::round(ball.width)));
            int bh = std::max(1, static_cast<int>(std::round(ball.height)));
            for (int xp = 0; xp < bw; xp++) {
                for (int yp = 0; yp < bh; yp++) {
                    model->setOverlayPixelValue(ix + xp, iy + yp, r, g, bl);
                }
            }
        }
        for (auto &p : powerUps) {
            p.draw(model);
        }
        for (auto &l : lasers) {
            int ix = static_cast<int>(std::round(l.x));
            int tip = static_cast<int>(std::round(l.y));
            int start = std::clamp(tip - 3, 0, model->getHeight() - 1);
            int end = std::clamp(tip + 1, 0, model->getHeight() - 1);
            for (int y = end; y >= start; --y) {
                model->setOverlayPixelValue(ix, y, 255, 0, 0);
            }
        }
        model->flushOverlayBuffer();
    }

    void updatePowerUps() {
        auto it = powerUps.begin();
        while (it != powerUps.end()) {
            it->y += it->fallSpeed;
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

    void updatePowerUpTimers() {
        const int tick = 50;
        if (slowTimerMs > 0) {
            slowTimerMs -= tick;
            if (slowTimerMs <= 0) {
                ballSpeedMultiplier = 1.0f;
                setAllBallSpeeds();
            }
        }
        if (expandTimerMs > 0) {
            expandTimerMs -= tick;
            if (expandTimerMs <= 0) {
                paddle.width = basePaddleWidth;
                enforcePaddleBounds();
            }
        }
        if (breakTimerMs > 0) {
            breakTimerMs -= tick;
            if (breakTimerMs <= 0) {
                powerBallActive = false;
            }
        }
        if (stickyTimerMs > 0) {
            stickyTimerMs -= tick;
            if (stickyTimerMs <= 0) {
                releaseStuckBalls();
            }
        }
        if (laserTimerMs > 0) {
            laserTimerMs -= tick;
            if (laserTimerMs <= 0) {
                laserActive = false;
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
                if (std::fabs(ball.directionY) < 0.1f) {
                    ball.directionY = -0.75f;
                }
                if (ball.directionY > 0) {
                    ball.directionY = -ball.directionY;
                }
                if (std::fabs(ball.directionX) < 0.05f) {
                    ball.directionX = 0.2f;
                }
                vec2_norm(ball.directionX, ball.directionY);
            }
        }
        if (!keepEffect) {
            stickyActive = false;
            stickyTimerMs = 0;
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
        if (!laserActive) {
            return;
        }
        float centerX = paddle.x + paddle.width / 2.0f;
        lasers.push_back({centerX, paddle.y - 1, 4.0f});
    }

    void updateLasers() {
        auto it = lasers.begin();
        while (it != lasers.end()) {
            float prevY = it->y;
            it->y -= it->speed;
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
                maybeSpawnPowerUp(*targetIt);
                markBrickGone(targetIt->row, targetIt->col);
                blocks.erase(targetIt);
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
        if (scl <= 0) {
            return 0;
        }
        int glyphWidth = 4;
        int gridWidth = model->getWidth() / scl;
        int textWidth = glyphWidth * static_cast<int>(text.size());
        int pos = (gridWidth - textWidth) / 2;
        if (pos < 0) {
            pos = 0;
        }
        return pos;
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
        paddle.x += direction * paddle.height;
        if (paddle.x < 0) {
            paddle.x = 0;
        } else if ((paddle.x + paddle.width) >= model->getWidth()) {
            paddle.x = model->getWidth() - paddle.width;
        }
        moveBalls();
        updatePowerUps();
        updatePowerUpTimers();
        updateLasers();

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
        
        // make sure that length of dir stays at 1
        removeLostBalls();

        CopyToModel();
        if (balls.empty()) {
            //end game
            GameOn = false;
            float scl = paddle.height;
            CopyToModel(0.2f);
            outputString("GAME", centeredTextX("GAME", scl), (model->getHeight()/2-(6 * scl)) / scl, 255, 255, 255, scl);
            outputString("OVER", centeredTextX("OVER", scl), (model->getHeight()/2) / scl, 255, 255, 255, scl);
            model->flushOverlayBuffer();
            return 2000;
        }
        if (blocks.empty()) {
            GameOn = false;
            float scl = paddle.height;
            CopyToModel(0.2f);
            outputString("YOU", centeredTextX("YOU", scl), (model->getHeight()/2-(6 * scl)) / scl, 255, 255, 255, scl);
            outputString("WIN", centeredTextX("WIN", scl), (model->getHeight()/2) / scl, 255, 255, 255, scl);
            model->flushOverlayBuffer();
            return 2000;
        }
        
        return 50;
    }
    void vec2_norm(float& x, float &y) {
        // sets a vectors length to 1 (which means that x + y == 1)
        float length = std::sqrt((x * x) + (y * y));
        if (length != 0.0f) {
            length = 1.0f / length;
            x *= length;
            y *= length;
        }
    }
    void button(const std::string &button) {
        if (button == "Left - Pressed") {
            direction = -1;
        } else if (button == "Left - Released") {
            if (direction < 0) direction = 0;
        } else if (button == "Right - Pressed") {
            direction = 1;
        } else if (button == "Right - Released") {
            if (direction > 0) direction = 0;
        } else if (button == "A Button - Pressed" || button == "B Button - Pressed") {
            if (stickyActive) {
                releaseStuckBalls(true);
            }
            if (laserActive) {
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
    
    int direction = 0;
    float basePaddleWidth = 0;
    float baseBallSpeed = 0;
    float ballSpeedMultiplier = 1.0f;
    int slowTimerMs = 0;
    int expandTimerMs = 0;
    int breakTimerMs = 0;
    int stickyTimerMs = 0;
    int laserTimerMs = 0;
    bool powerBallActive = false;
    bool stickyActive = false;
    bool laserActive = false;
    
    bool GameOn = true;
    bool WaitingUntilOutput = false;

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
