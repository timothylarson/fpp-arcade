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
    TetrisEffect(int r, int c, int sc, PixelOverlayModel *m) : FPPArcadeGameEffect(m) {
        // Rows/Colums ARE Tetris' playfield, so they feed setPlayfield() rather
        // than the game getting a second pair of sizing options. Either at 0 now
        // means "as tall/wide as the panel allows", and the centring that used
        // to be computed at the call site happens here for every game alike.
        setPlayfield(c, r, sc);
        rows = pfH;
        cols = pfW;
        table.resize(rows);
        for (int x = 0; x < rows; x++) {
            table[x].resize(cols);
        }
        layoutSidebar();
        levelBannerMs = bannerMs;   // show LEVEL 1 straight away, not only on a change
        newShape();
        CopyToModel();
    }
    ~TetrisEffect() {
        if (currentShape) {
            delete currentShape;
        }
        if (nextShape) {
            delete nextShape;
        }
    }

    // Place the well and the sidebar as one block and centre THAT, rather than
    // centring the well alone and hanging the sidebar off it - otherwise the
    // pair sits visibly right of centre. Falls back to a bare centred well when
    // the matrix is too narrow to carry a sidebar at all.
    void layoutSidebar() {
        const int panelW = model->getWidth();

        // Leave the well where setPlayfield() centred it. Treating the well and
        // the preview as one block and centring THAT pushed the playfield left
        // of the matrix centre to make room for four cells, which is the wrong
        // trade: the well is what the player is looking at, so it gets the
        // middle and the preview lives out in the margin.
        const int wellRight = offsetX + cols * scale;   // column of the right wall
        const int marginX   = wellRight + 1;
        const int marginW   = panelW - marginX;
        const int want      = 4 * scale;

        if (marginW >= want + 2) {
            sidebarW  = want;
            sidebarOn = true;
            // Centred in the margin rather than tucked against the wall, so it
            // reads as a separate side panel instead of clutter beside the well.
            sidebarX  = marginX + (marginW - want) / 2;
        } else {
            sidebarW  = 0;
            sidebarOn = false;
            sidebarX  = 0;
        }
    }

    // The sidebar is addressed in absolute panel pixels, but outputString()
    // applies the well's scale and offset. Borrow the transform for the call
    // rather than duplicating the glyph rendering.
    void drawSidebarText(const std::string &txt, int x, int y, int r, int g, int b) {
        const int ps = scale, px = offsetX, py = offsetY;
        scale = 1; offsetX = 0; offsetY = 0;
        outputString(txt, x, y, r, g, b, 1);
        scale = ps; offsetX = px; offsetY = py;
    }

    void drawNextPiece(int x, int y) {
        if (!nextShape) {
            return;
        }
        const uint32_t c = nextShape->getColor();
        const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        const int w = nextShape->getWidth();
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < w; j++) {
                if (!nextShape->get(i, j)) {
                    continue;
                }
                for (int sx = 0; sx < scale; sx++) {
                    for (int sy = 0; sy < scale; sy++) {
                        model->setOverlayPixelValue(x + j * scale + sx, y + i * scale + sy, r, g, b);
                    }
                }
            }
        }
    }

    void drawSidebar() {
        if (!sidebarOn) {
            return;
        }
        if (offsetY + 4 * scale <= model->getHeight()) {
            drawNextPiece(sidebarX, offsetY);
        }
    }

    // A permanent score/level/lines readout wants ~18 rows of glyphs beside the
    // well, which a 25-row matrix cannot spare without shrinking the well. So the
    // level is announced briefly when it changes, and the final score belongs on
    // the game-over screen - nothing has to sit on screen for the whole game.
    void drawLevelBanner() {
        if (levelBannerMs <= 0.0) {
            return;
        }
        char buf[24];
        snprintf(buf, sizeof(buf), "LEVEL %d", level);
        PanelTransform pt(this);

        const std::string txt(buf);
        const int textH = 5;
        // Glyphs are 3px wide on a 4px pitch, so the last one contributes 3.
        const int textW = static_cast<int>(txt.size()) * 4 - 1;
        const int tx = centerTextX(txt, 1);
        const int ty = std::max(0, (model->getHeight() - textH) / 2);

        // Blank a panel behind the text and outline it, so settled blocks and
        // the well walls do not read through the glyphs. The fill is (1,1,1)
        // rather than pure black on purpose: in the Transparent overlay mode a
        // black pixel lets the underlying sequence show through, which is
        // exactly what the box is meant to stop.
        const int pad = 1;
        const int x0 = tx - pad - 1,        y0 = ty - pad - 1;
        const int x1 = tx + textW + pad,    y1 = ty + textH + pad;
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                if (x < 0 || y < 0 || x >= model->getWidth() || y >= model->getHeight()) {
                    continue;
                }
                const bool edge = (x == x0 || x == x1 || y == y0 || y == y1);
                if (edge) {
                    model->setOverlayPixelValue(x, y, 255, 255, 255);
                } else {
                    model->setOverlayPixelValue(x, y, 1, 1, 1);
                }
            }
        }
        outputString(txt, tx, ty, 255, 220, 0, 1);
    }
    const std::string &name() const override {
        static std::string NAME = "Tetris";
        return NAME;
    }

    void newShape() {
        if (currentShape) {
            delete currentShape;
        }
        // Take the piece the sidebar has been previewing, then draw the next one.
        if (!nextShape) {
            nextShape = new Shape();
        }
        currentShape = nextShape;
        nextShape = new Shape();
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
            // Classic Tetris scoring: a single is worth 40, a tetris 1200, and
            // the whole thing scales with the level - so clearing four at once
            // is worth far more than four singles.
            static const int LINE_SCORE[5] = { 0, 40, 100, 300, 1200 };
            score += LINE_SCORE[std::min(cleared, 4)] * level;
            lines += cleared;
            // The drop speed already ramps per line cleared, so deriving the
            // level from lines the classic way keeps the number the player sees
            // consistent with how fast the game actually feels.
            const int newLevel = lines / 10 + 1;
            if (newLevel != level) {
                levelBannerMs = bannerMs;
            }
            level = newLevel;
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
        drawSidebar();
        drawLevelBanner();
        model->flushOverlayBuffer();
    }

    int32_t showGameOver() {
        resetFrameTimer();
        if (currentShape) {
            delete currentShape;
            currentShape = nullptr;
            model->clearOverlayBuffer();
            {
                // Draws across the whole matrix, so the centring reference has to
                // be the panel too. Zeroing scale/offset alone (as this did) left
                // centerTextX() measuring the 11-wide well, and every line came
                // out clamped hard against x=0.
                PanelTransform pt(this);

                char scoreBuf[20];
                snprintf(scoreBuf, sizeof(scoreBuf), "%d", score);

                // GAME / OVER / final score, dropping the tail lines on a matrix
                // too short to hold them rather than drawing off the bottom.
                const int glyphH = 5;
                const int pitch  = 7;
                const int panelH = model->getHeight();
                int wanted = 3;
                while (wanted > 1 && (wanted - 1) * pitch + glyphH > panelH) {
                    wanted--;
                }
                const int blockH = (wanted - 1) * pitch + glyphH;
                int y = std::max(0, (panelH - blockH) / 2);

                const char *rows_[3] = { "GAME", "OVER", scoreBuf };
                for (int i = 0; i < wanted; i++) {
                    outputString(rows_[i], centerTextX(rows_[i], 1), y, 255, 255, 255, 1);
                    y += pitch;
                }
            }
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

        if (levelBannerMs > 0.0) {
            levelBannerMs -= elapsedMs;
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
    int lines = 0;
    int level = 1;   // 1-based: the player sees LEVEL 1 on the first drop
    Shape *nextShape = nullptr;
    // Sidebar geometry, in absolute panel pixels (the well uses scale/offset).
    int  sidebarX = 0;
    int  sidebarW = 0;
    bool sidebarOn = false;
    double levelBannerMs = 0.0;
    static constexpr double bannerMs = 1800.0;
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
        lines = 0;
        level = 1;
        levelBannerMs = bannerMs;   // announce LEVEL 1 on restart too
        // The drop speed ramps with lines cleared and was NOT being reset, so a
        // restart said LEVEL 1 while still running at the old game's pace.
        accumulatedDrop = 0.0;
        timer = initialTimer;
        fallAccumulatorMs = 0.0;
        GameOn = true;
        WaitingUntilOutput = false;
        if (currentShape) {
            delete currentShape;
            currentShape = nullptr;
        }
        if (nextShape) {
            delete nextShape;
            nextShape = nullptr;
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
            int pixelScaling = findIntOption("Pixel Scaling", 1);
            int rows = findIntOption("Rows", 20);
            int cols = findIntOption("Colums", 11);
            effect = new TetrisEffect(rows, cols, pixelScaling, m);
            // Creating the effect IS the response to Start. Forwarding that same
            // press into the fresh effect reaches the base class pause handler,
            // which pauses the game on its first frame - it took two Start
            // presses to actually play. Other buttons still need forwarding so a
            // direction press that starts a game also registers as a move.
            if (button != "Start - Pressed" && button != "Start") {
                effect->button(button);
            }
            m->setRunningEffect(effect, 50);
        } else {
            effect->button(button);
        }
    }
}
