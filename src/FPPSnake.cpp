#include <fpp-pch.h>

#include "FPPSnake.h"
#include <algorithm>
#include <array>
#include <random>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"


FPPSnake::FPPSnake(Json::Value &config) : FPPArcadeGame(config) {
    std::srand(time(NULL));
}
FPPSnake::~FPPSnake() {
}

class SnakeEffect : public FPPArcadeGameEffect {
public:
    SnakeEffect(int sc, int pfw, int pfh, PixelOverlayModel *m) : FPPArcadeGameEffect(m) {
        // 0/0 keeps the historical behaviour of playing across the whole panel;
        // a configured size is centred on it. The wall Snake draws at cols-1 /
        // rows-1 is therefore the playfield edge, not the panel edge.
        setPlayfield(pfw, pfh, sc);
        cols = pfW;
        rows = pfH;
        
        direction = 0;
        snake.push_back(std::pair<int, int>(cols / 2, rows / 2));
        snake.push_back(std::pair<int, int>(cols / 2 + 1, rows / 2));
        snake.push_back(std::pair<int, int>(cols / 2 + 2, rows / 2));
        
        addFood();
        addFood();
        addFood();
    }
    ~SnakeEffect() {
    }
    
    void addFood() {
        bool OK = false;
        int x, y;
        while (!OK) {
            OK = true;
            x = rand() % (cols - 2) + 1;
            y = rand() % (rows - 2) + 1;
            for (auto &a : food) {
                if (a.first == x && a.second == y) {
                    OK = false;
                }
            }
            for (auto &a : snake) {
                if (a.first == x && a.second == y) {
                    OK = false;
                }
            }
        }
        food.push_back({x, y});
    }
    const std::string &name() const override {
        static std::string NAME = "Snake";
        return NAME;
    }

    
    void CopyToModel() {
        model->clearOverlayBuffer();
        for (auto &a : food) {
            outputPixel(a.first, a.second, 0, 255, 0);
        }
        int size = snake.size();
        int count = 0;
        for (auto &a : snake) {
            if (count == 0) {
                outputPixel(a.first, a.second, 0, 0, 255);
            } else {
                int c = size - count;
                c *= 200;
                c /= size;
                c += 50;
                outputPixel(a.first, a.second, c, 0, 0);
            }
            count++;
        }
        for (int r = 0; r < rows; r++) {
            outputPixel(0, r, 128, 128, 128);
            outputPixel(cols-1, r, 128, 128, 128);
        }
        for (int c = 0; c < cols; c++) {
            outputPixel(c, 0, 128, 128, 128);
            outputPixel(c, rows-1, 128, 128, 128);
        }
    }
    
    void moveSnake() {
        int x = snake.front().first;
        int y = snake.front().second;
        switch (direction) {
            case 0:
                --x;
                break;
            case 1:
                --y;
                break;
            case 2:
                ++x;
                break;
            case 3:
                ++y;
                break;
        }


        bool foundFood = false;
        for (auto &a : food) {
            if (a.first == x && a.second == y) {
                foundFood = true;
            }
        }
        if (!foundFood) {
            snake.pop_back();
            for (auto &a : snake) {
                if (a.first == x && a.second == y) {
                    GameOn = false;
                }
            }
        } else {
            food.remove({x, y});
            addFood();
        }
        snake.push_front({x, y});
        if (x == 0 || y == 0 || x == (cols-1) || y == (rows-1)) {
            //hit a wall
            GameOn = false;
        }
        
    }
    
    virtual int32_t update() override {
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return timer;
        }

        if (!GameOn) {
            resetFrameTimer();
            if (WaitingUntilOutput) {
                model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
                return 0;
            }
            model->clearOverlayBuffer();
            model->flushOverlayBuffer();
            WaitingUntilOutput = true;
            return -1;
        }
        double elapsedMs = consumeElapsedMs(static_cast<double>(timer));
        if (elapsedMs <= 0.0) {
            elapsedMs = static_cast<double>(timer);
        }
        double maxCatchup = static_cast<double>(timer) * 3.0;
        moveAccumulatorMs = std::min(moveAccumulatorMs + elapsedMs, maxCatchup);
        while (moveAccumulatorMs >= static_cast<double>(timer) && GameOn) {
            moveSnake();
            moveAccumulatorMs -= static_cast<double>(timer);
        }
        CopyToModel();
        if (!GameOn) {
            moveAccumulatorMs = 0.0;
            int scl = scale == 0 ? 1 : scale;
            char buf[25];
            snprintf(buf, sizeof(buf), "%u", (uint32_t)snake.size());
            // Lay GAME / OVER / score out as one block and centre it, clamped
            // into the playfield. The old fixed rows/2-9 offset assumed a tall
            // field and put GAME above the top wall as soon as the playfield was
            // shorter than about 19 rows.
            const int glyphH = 5;
            // Fit what the playfield can actually hold. Three lines at the roomy
            // 6-row pitch want 17 rows; tighten to a 5-row pitch first, and on a
            // very short field drop the score, then OVER, rather than drawing
            // outside the playfield the way the old fixed rows/2-9 offset did.
            const int lines  = rows >= glyphH * 3 ? 3 : (rows >= glyphH * 2 ? 2 : 1);
            const int lineH  = std::max(glyphH, std::min(6, (rows - glyphH) / std::max(1, lines - 1)));
            const int blockH = lineH * (lines - 1) + glyphH;
            const int y0     = std::max(0, (rows - blockH) / 2);
            outputString("GAME", centerTextX("GAME", scl), y0, 255, 255, 255, scl);
            if (lines >= 2) {
                outputString("OVER", centerTextX("OVER", scl), y0 + lineH, 255, 255, 255, scl);
            }
            if (lines >= 3) {
                outputString(buf, centerTextX(buf, scl), y0 + lineH * 2, 255, 255, 255, scl);
            }
            resetFrameTimer();
            model->flushOverlayBuffer();
            return 2000;
        }
        model->flushOverlayBuffer();
        return timer;
    }
    
    
    void button(const std::string &button) {
        // allow base to handle pause/start behavior first (only while game is active)
        if (GameOn) {
            FPPArcadeGameEffect::button(button);
            if (isPaused()) return;
        }

        if (button == "Left - Pressed" && direction != 2) {
            direction = 0;
        } else if (button == "Right - Pressed" && direction != 0) {
            direction = 2;
        } else if (button == "Up - Pressed" && direction != 3) {
            direction = 1;
        } else if (button == "Down - Pressed" && direction != 1) {
            direction = 3;
        }
    }
    
    
    std::list<std::pair<int, int>> food;
    std::list<std::pair<int, int>> snake;
    int direction = 0;

    int rows = 20;
    int cols = 20;
    double moveAccumulatorMs = 0.0;

    bool GameOn = true;
    bool WaitingUntilOutput = false;
    long long timer = 100;
    
    void restart() override {
        // Reinitialize snake to starting state
        snake.clear();
        food.clear();
        direction = 0;
        snake.push_back(std::pair<int, int>(cols / 2, rows / 2));
        snake.push_back(std::pair<int, int>(cols / 2 + 1, rows / 2));
        snake.push_back(std::pair<int, int>(cols / 2 + 2, rows / 2));
        addFood(); addFood(); addFood();
        moveAccumulatorMs = 0.0;
        GameOn = true;
        WaitingUntilOutput = false;
        resetFrameTimer();
    }
};
const std::string &FPPSnake::getName() {
    static const std::string name = "Snake";
    return name;
}


void FPPSnake::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m != nullptr) {
        SnakeEffect *effect = dynamic_cast<SnakeEffect*>(m->getRunningEffect());
        if (!effect) {
            if (findOption("overlay", "Overwrite") == "Transparent") {
                m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
            } else {
                m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
            }
            int pixelScaling = std::stoi(findOption("Pixel Scaling", "1"));
            effect = new SnakeEffect(pixelScaling,
                                     playfieldWidthOption(), playfieldHeightOption(), m);
            m->setRunningEffect(effect, 50);
        } else {
            effect->button(button);
        }
    }
}
