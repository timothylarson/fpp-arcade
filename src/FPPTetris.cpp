#include <fpp-pch.h>

#include "FPPTetris.h"
#include <algorithm>
#include <array>
#include <random>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"


FPPTetris::FPPTetris(Json::Value &config) : FPPArcadeGame(config) {
    std::srand(time(NULL));
}
FPPTetris::~FPPTetris() {
}

static std::array<uint32_t, 7> COLORS = {
    0xFF0000,
    0x00FF00,
    0x0000FF,
    0xFF00FF,
    0xFFFF00,
    0x00FFFF,
    0xFFFFFF,
};

class Shape {
public:
    Shape() : row(0), col(0) {
        for (int x = 0; x < 16; x++) {
            data[x] = 0;
        }
        setRandomShape();
    }
    
    int row, col;
    
    
    uint8_t get(int x, int y) {
        return data[x * width + y];
    }
    void set(int x, int y, uint8_t d) {
        data[x * width + y] = d;
    }
    
    void rotate() {
        std::array<uint8_t, 16> d2(data);
        for (int x = 0; x < 16; x++) {
            data[x] = 0;
        }
        for (int x = 0; x < width; x++) {
            for (int y = 0, ny = width - 1; y < width; y++, ny--) {
                set(x, y, d2[ny * width + x]);
            }
        }
    }
    void rotateCounterClockwise() {
        // Reverse rotation by applying CW transform three times (shapes are small enough).
        rotate();
        rotate();
        rotate();
    }
    
    int getWidth() { return width;}

    uint32_t getColor() {
        return COLORS[type];
    }
private:
    void setRandomShape() {
        type = std::rand() % 7;
        switch (type) {
            case 0:
                width = 3;  // s shape
                set(1, 0, 1);
                set(2, 0, 1);
                set(0, 1, 1);
                set(1, 1, 1);
                break;
            case 1:
                width = 3;  // z shape
                set(0, 0, 1);
                set(1, 0, 1);
                set(1, 1, 1);
                set(2, 1, 1);
                break;
            case 2:
                width = 3;  // L shape 1
                set(0, 0, 1);
                set(1, 0, 1);
                set(1, 1, 1);
                set(1, 2, 1);
                break;
            case 3:
                width = 3;  // L shape 2
                set(1, 0, 1);
                set(0, 0, 1);
                set(0, 1, 1);
                set(0, 2, 1);
                break;
            case 4:
                width = 2;  // sq shape
                set(0, 0, 1);
                set(0, 1, 1);
                set(1, 0, 1);
                set(1, 1, 1);
                break;
            case 5:
                width = 3;  // T shape
                set(1, 0, 1);
                set(0, 1, 1);
                set(1, 1, 1);
                set(2, 1, 1);
                break;
            case 6: // I shape
            default:
                type = 6;
                width = 4;
                set(1, 0, 1);
                set(1, 1, 1);
                set(1, 2, 1);
                set(1, 3, 1);
                break;
        };
    }
    
    int width = 4;
    int type = 0;
    std::array<uint8_t, 16> data;
};




class TetrisEffect : public FPPArcadeGameEffect {
public:
    TetrisEffect(int r, int c, int offx, int offy, int sc, PixelOverlayModel *m) : FPPArcadeGameEffect(m), rows(r), cols(c) {
        table.resize(r);
        for (int x = 0; x < r; x++) {
            table[x].resize(c);
        }
        scale = sc;
        offsetX = offx;
        offsetY = offy;
        newShape();
        CopyToModel();
    }
    ~TetrisEffect() {
        if (currentShape) {
            delete currentShape;
        }
    }
    const std::string &name() const override {
        static std::string NAME = "Tetris";
        return NAME;
    }

    void newShape() {
        if (currentShape) {
            delete currentShape;
        }
        currentShape = new Shape();
        currentShape->col = cols / 2 - 1;
        currentShape->row = 0;
        if (!CheckPosition(currentShape)) {
            GameOn = false;
        }
    }
    bool CheckPosition(Shape *shape) {
        for (int i = 0; i < shape->getWidth(); i++) {
            for(int j = 0; j < shape->getWidth(); j++) {
                if ((shape->col+j < 0 || shape->col+j >= cols || shape->row+i >= rows)){ //Out of borders
                    if (shape->get(i, j)) {
                        return false;
                    }
                } else if (table[shape->row+i][shape->col+j] && shape->get(i, j)) {
                    return false;
                }
            }
        }
        return true;
    }
    void WriteToTable(){
        for (int i = 0; i < currentShape->getWidth(); i++) {
            for (int j = 0; j < currentShape->getWidth(); j++) {
                if (currentShape->get(i, j)) {
                    table[currentShape->row+i][currentShape->col+j] = currentShape->getColor();
                }
            }
        }
    }
    
    void CheckLines() {
        int count = 0;
        int cleared = 0;
        for(int i = 0; i < rows; i++) {
            int sum = 0;
            for(int j = 0; j < cols; j++) {
                if (table[i][j]) {
                    sum++;
                }
            }
            if (sum == cols){
                score++;
                cleared++;

                for (int k = i; k >= 1; k--) {
                    for (int l = 0; l < cols; l++) {
                        table[k][l] = table[k-1][l];
                    }
                }
                for (int l = 0; l < cols; l++) {
                    table[0][l] = 0;
                }
            }
        }
        if (cleared > 0) {
            increaseSpeed(cleared);
        }
    }

    void increaseSpeed(int lines) {
        accumulatedDrop += static_cast<double>(lines) * perLineDrop;
        if (accumulatedDrop > maxSpeedDrop) {
            accumulatedDrop = maxSpeedDrop;
        }
        timer = initialTimer - accumulatedDrop;
        if (timer < minTimer) {
            timer = minTimer;
        }
    }
    
    void CopyToModel() {
        model->clearOverlayBuffer();
        if (offsetX) {
            for (int y = 0; y < rows*scale; y++) {
                model->setOverlayPixelValue(offsetX-1, offsetY + y, 128, 128, 128);
                model->setOverlayPixelValue(offsetX+cols*scale, offsetY + y, 128, 128, 128);
            }
            for (int x = -1; x <= cols*scale; x++) {
                model->setOverlayPixelValue(offsetX+x, offsetY + rows*scale, 128, 128, 128);
                if (offsetY) {
                    model->setOverlayPixelValue(offsetX+x, offsetY - 1, 128, 128, 128);
                }
            }
        }
        for(int i = 0; i < rows; i++) {
            for(int j = 0; j < cols; j++) {
                if (table[i][j]) {
                    int r = (table[i][j] >> 16) & 0xFF;
                    int g = (table[i][j] >> 8) & 0xFF;
                    int b = (table[i][j]) & 0xFF;
                    outputPixel(j, i, r, g, b);
                }
            }
        }
        if (currentShape) {
            uint32_t color = currentShape->getColor();
            int r = (color >> 16) & 0xFF;
            int g = (color >> 8) & 0xFF;
            int b = (color) & 0xFF;

            for (int i = 0; i < currentShape->getWidth(); i++) {
                for (int j = 0; j < currentShape->getWidth(); j++) {
                    if (currentShape->get(i, j)) {
                        outputPixel(currentShape->col+j, currentShape->row+i, r, g, b);
                    }
                }
            }

        }
        model->flushOverlayBuffer();
    }

    int32_t showGameOver() {
        resetFrameTimer();
        if (currentShape) {
            delete currentShape;
            currentShape = nullptr;
            model->clearOverlayBuffer();
            int prevScale = scale;
            int prevOffsetX = offsetX;
            int prevOffsetY = offsetY;
            scale = 1;
            offsetX = 0;
            offsetY = 0;

            int totalHeight = 5 + 2 + 5 + 6; // GAME + spacing + OVER + score spacing
            int startY = (model->getHeight() - totalHeight) / 2;
            if (startY < 0) startY = 0;
            int yGame = startY;
            int yOver = yGame + 7;
            int yScore = yOver + 7;
            outputString("GAME", centerTextX("GAME", 1), yGame);
            outputString("OVER", centerTextX("OVER", 1), yOver);

            char buf[20];
            snprintf(buf, sizeof(buf), "%d", score);
            outputString(buf, centerTextX(buf, 1), yScore);

            scale = prevScale;
            offsetX = prevOffsetX;
            offsetY = prevOffsetY;
            model->flushOverlayBuffer();
            return 3000;
        }
        model->clearOverlayBuffer();
        model->flushOverlayBuffer();

        if (WaitingUntilOutput) {
            model->setState(PixelOverlayState(PixelOverlayState::PixelState::Disabled));
            return 0;
        }
        WaitingUntilOutput = true;
        return -1;
    }

    virtual int32_t update() override {
        if (isPaused()) {
            drawPauseMenu();
            model->flushOverlayBuffer();
            return 50;
        }

        if (!GameOn) {
            return showGameOver();
        }

        double desiredFrame = std::max(minFrameInterval, timer / 2.0);
        if (desiredFrame <= 0.0) {
            desiredFrame = minFrameInterval;
        }
        double elapsedMs = consumeElapsedMs(desiredFrame);
        if (elapsedMs <= 0.0) {
            elapsedMs = desiredFrame;
        }

        double maxCatchup = std::max(timer * 3.0, desiredFrame);
        fallAccumulatorMs = std::min(fallAccumulatorMs + elapsedMs, maxCatchup);
        while (fallAccumulatorMs >= timer && GameOn) {
            fallAccumulatorMs -= timer;
            dropOneRow();
        }

        if (softDropHeld && GameOn) {
            double softMax = std::max(softDropIntervalMs * 3.0, softDropIntervalMs);
            softDropAccumulatorMs = std::min(softDropAccumulatorMs + elapsedMs, softMax);
            while (softDropAccumulatorMs >= softDropIntervalMs && GameOn) {
                softDropAccumulatorMs -= softDropIntervalMs;
                dropOneRow();
            }
        } else {
            softDropAccumulatorMs = 0.0;
        }

        if (!GameOn) {
            return showGameOver();
        }

        handleHeldMovement(elapsedMs);
        if (GameOn) {
            CopyToModel();
        }
        return static_cast<int>(desiredFrame);
    }
    
    void button(const std::string &button) {
        if (!GameOn || !currentShape) {
            return;
        }

            // allow base to handle pause/start behavior first (only while game is active)
            if (GameOn) {
                FPPArcadeGameEffect::button(button);
                if (isPaused()) return;
            }
        if (button == "Left - Pressed") {
            moveHorizontal(-1);
            leftHeld = true;
            leftAccumulatorMs = 0.0;
            leftInitialDelay = true;
        } else if (button == "Left - Released") {
            leftHeld = false;
            leftAccumulatorMs = 0.0;
            leftInitialDelay = true;
        } else if (button == "Right - Pressed") {
            moveHorizontal(1);
            rightHeld = true;
            rightAccumulatorMs = 0.0;
            rightInitialDelay = true;
        } else if (button == "Right - Released") {
            rightHeld = false;
            rightAccumulatorMs = 0.0;
            rightInitialDelay = true;
        } else if (button == "Up - Pressed" || button == "A Button - Pressed" || button == "Fire - Pressed") {
            Shape tmp(*currentShape);
            tmp.rotate();
            if (CheckPosition(&tmp)) {
                currentShape->rotate();
            }
        } else if (button == "B Button - Pressed") {
            Shape tmp(*currentShape);
            tmp.rotateCounterClockwise();
            if (CheckPosition(&tmp)) {
                currentShape->rotateCounterClockwise();
            }
        } else if (button == "Down - Pressed") {
            softDropHeld = true;
            dropOneRow();
        } else if (button == "Down - Released") {
            softDropHeld = false;
            softDropAccumulatorMs = 0.0;
        }
        CopyToModel();
    }

    bool moveHorizontal(int delta) {
        if (!currentShape) {
            return false;
        }
        Shape tmp(*currentShape);
        tmp.col += delta;
        if (CheckPosition(&tmp)) {
            currentShape->col += delta;
            return true;
        }
        return false;
    }

    void dropOneRow() {
        if (!currentShape) {
            return;
        }
        Shape tmp(*currentShape);
        tmp.row++;
        if (CheckPosition(&tmp)) {
            currentShape->row++;
            CopyToModel();
        } else {
            WriteToTable();
            CheckLines();
            newShape();
            CopyToModel();
        }
    }

    void handleHeldMovement(double elapsedMs) {
        bool moved = false;
        if (leftHeld && !rightHeld) {
            leftAccumulatorMs += elapsedMs;
            double threshold = leftInitialDelay ? holdInitialDelayMs : holdRepeatIntervalMs;
            while (leftAccumulatorMs >= threshold) {
                if (!moveHorizontal(-1)) {
                    leftAccumulatorMs = threshold;
                    break;
                }
                leftAccumulatorMs -= threshold;
                leftInitialDelay = false;
                threshold = holdRepeatIntervalMs;
                moved = true;
            }
        } else {
            leftAccumulatorMs = 0.0;
            leftInitialDelay = true;
        }

        if (rightHeld && !leftHeld) {
            rightAccumulatorMs += elapsedMs;
            double threshold = rightInitialDelay ? holdInitialDelayMs : holdRepeatIntervalMs;
            while (rightAccumulatorMs >= threshold) {
                if (!moveHorizontal(1)) {
                    rightAccumulatorMs = threshold;
                    break;
                }
                rightAccumulatorMs -= threshold;
                rightInitialDelay = false;
                threshold = holdRepeatIntervalMs;
                moved = true;
            }
        } else {
            rightAccumulatorMs = 0.0;
            rightInitialDelay = true;
        }

        if (moved) {
            CopyToModel();
        }
    }
    
    

    int rows = 20;
    int cols = 11;
    std::vector<std::vector<uint32_t>> table;
    int score = 0;
    bool GameOn = true;
    bool WaitingUntilOutput = false;
    
    Shape *currentShape = nullptr;
    double timer = 500.0; // half second
    const double initialTimer = 500.0;
    const double perLineDrop = 25.0;
    const double maxSpeedDrop = 360.0;
    const double minTimer = 150.0;
    const double minFrameInterval = 80.0;
    double fallAccumulatorMs = 0.0;
    double accumulatedDrop = 0.0;
    bool leftHeld = false;
    bool rightHeld = false;
    double leftAccumulatorMs = 0.0;
    double rightAccumulatorMs = 0.0;
    bool leftInitialDelay = true;
    bool rightInitialDelay = true;
    const double holdInitialDelayMs = 200.0;
    const double holdRepeatIntervalMs = 90.0;
    bool softDropHeld = false;
    double softDropAccumulatorMs = 0.0;
    const double softDropIntervalMs = 60.0;

    void restart() override {
        // Clear table and reset game state
        for (auto &row : table) {
            std::fill(row.begin(), row.end(), 0);
        }
        score = 0;
        GameOn = true;
        WaitingUntilOutput = false;
        if (currentShape) {
            delete currentShape;
            currentShape = nullptr;
        }
        newShape();
        resetFrameTimer();
    }

};

const std::string &FPPTetris::getName() {
    static const std::string name = "Tetris";
    return name;
}


void FPPTetris::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m != nullptr) {
        TetrisEffect *effect = dynamic_cast<TetrisEffect*>(m->getRunningEffect());
        if (!effect) {
            if (findOption("overlay", "Overwrite") == "Transparent") {
                m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
            } else {
                m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
            }
            int pixelScaling = std::stoi(findOption("Pixel Scaling", "1"));
            int rows = std::stoi(findOption("Rows", "20"));
            int cols = std::stoi(findOption("Colums", "11"));
            int offsetX = (m->getWidth() - (cols * pixelScaling)) / 2;
            if (offsetX < 0) {
                offsetX = 0;
            }
            int offsetY = (m->getHeight() - (rows * pixelScaling)) / 2;
            if (offsetY < 0) {
                offsetY = 0;
            }
            effect = new TetrisEffect(rows, cols, offsetX, offsetY, pixelScaling, m);
            effect->button(button);
            m->setRunningEffect(effect, 50);
        } else {
            effect->button(button);
        }
    }
}
