#include <fpp-pch.h>

#include "FPPSnake.h"
#include "SnakeShow.h"
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
    SnakeEffect(int sc, int pfw, int pfh, PixelOverlayModel *m, int showMode = 1, int goodLength = 20)
        : FPPArcadeGameEffect(m), endShow(showMode), goodGame(goodLength) {
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
        startSound();
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
            for (auto &a : snake2) {
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
        // The blue snake: a bright cyan head on a blue body that fades along it.
        count = 0;
        size = snake2.size();
        for (auto &a : snake2) {
            if (count == 0) {
                outputPixel(a.first, a.second, 120, 220, 255);
            } else {
                const int c = 50 + (size - count) * 200 / std::max(1, size);
                outputPixel(a.first, a.second, 0, c / 3, c);
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
    
    using Cell = std::pair<int, int>;
    static Cell ahead(Cell c, int dir) {
        switch (dir) {
            case 0: --c.first; break;
            case 1: --c.second; break;
            case 2: ++c.first; break;
            case 3: ++c.second; break;
        }
        return c;
    }
    bool isWall(const Cell &c) const { return c.first <= 0 || c.second <= 0 || c.first >= cols - 1 || c.second >= rows - 1; }
    static bool contains(const std::list<Cell> &body, const Cell &c) {
        return std::find(body.begin(), body.end(), c) != body.end();
    }
    bool isFood(const Cell &c) const { return contains(food, c); }

    // One step. With two snakes both move at once, so meeting head on is a
    // tie rather than a win for whichever happened to move first; the first
    // to hit a wall, itself or the other ends the duel.
    void moveSnake() {
        const Cell h1 = ahead(snake.front(), direction);
        const Cell h2 = duel ? ahead(snake2.front(), direction2) : Cell{-1, -1};
        const bool ate1 = isFood(h1);
        const bool ate2 = duel && h2 != h1 && isFood(h2);
        // Tails move first (unless eating), so following a tail is safe.
        if (!ate1) {
            snake.pop_back();
        }
        if (duel && !ate2) {
            snake2.pop_back();
        }
        const bool dead1 = isWall(h1) || contains(snake, h1) || (duel && (contains(snake2, h1) || h1 == h2));
        const bool dead2 = duel && (isWall(h2) || contains(snake2, h2) || contains(snake, h2) || h1 == h2);
        snake.push_front(h1);
        if (duel) {
            snake2.push_front(h2);
        }
        // The way each one went this step, and a second quick turn, held back
        // for this step, now.
        moved = direction;
        moved2 = direction2;
        if (queued >= 0 && queued != (moved + 2) % 4) {
            direction = queued;
        }
        if (queued2 >= 0 && queued2 != (moved2 + 2) % 4) {
            direction2 = queued2;
        }
        queued = queued2 = -1;
        for (const Cell &h : {h1, h2}) {
            if ((h == h1 && ate1) || (h == h2 && ate2)) {
                food.remove(h);
                addFood();
                // Every tenth bite gets a fanfare, the only progress marker a
                // game without levels has.
                ++eaten;
                sfx("snake", eaten % 10 == 0 ? "Milestone" : "Eat");
            }
        }
        if (dead1 || dead2) {
            GameOn = false;
            winner = dead1 && dead2 ? 0 : (dead2 ? 1 : 2);
        }
    }

    // A or B mid-game: the blue snake joins, in whichever corner is clear,
    // three long and heading away from the red one's head.
    void joinBlue() {
        struct Spot {
            int x, y, dir;
        };
        const Spot spots[] = {{cols * 3 / 4, rows * 3 / 4, 0}, {cols / 4, rows / 4, 2},
                              {cols * 3 / 4, rows / 4, 0}, {cols / 4, rows * 3 / 4, 2}};
        const Cell red = snake.front();
        for (const Spot &sp : spots) {
            std::list<Cell> body;
            for (int i = 0; i < 3; ++i) {
                body.push_back({sp.x + (sp.dir == 0 ? i : -i), sp.y});   // the tail trails behind the way it's going
            }
            bool clear = std::abs(sp.x - red.first) + std::abs(sp.y - red.second) >= 6;
            for (const Cell &c : body) {
                clear = clear && !isWall(c) && !contains(snake, c) && !isFood(c) && !isWall(ahead(c, sp.dir));
            }
            if (clear) {
                snake2 = body;
                direction2 = moved2 = sp.dir;
                queued2 = -1;
                duel = true;
                sfx("snake", "StartGame");
                return;
            }
        }
    }

    virtual int32_t update() override {
        if (show.on()) {
            if (show.step(model, consumeElapsedMs(50.0))) {
                return 50;
            }
            resetFrameTimer();   // over: the game ends below, as it would have
        }
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
            sfxStop("snake");
            // A good game (or a new best) gets the end show instead of GAME OVER.
            // In a duel the longer snake counts.
            const int length = duel ? (int)std::max(snake.size(), snake2.size()) : (int)snake.size();
            int best = 0;
            const bool newBest = arcadeRecordBest(model->getName(), "Snake", length, &best);
            if (arcadeWantShow(endShow, length >= goodGame, newBest)) {
                sfxEndShow("snake");
                if (duel) {
                    show.start(model, SnakeShow::config2((int)snake.size(), (int)snake2.size(), winner, newBest, best),
                               std::make_unique<SnakeShow::Story>(true));
                } else {
                    show.start(model, SnakeShow::config(length, newBest, best), std::make_unique<SnakeShow::Story>());
                }
                resetFrameTimer();
                return 50;
            }
            sfx("snake", "GameOver");
            char buf[25];
            snprintf(buf, sizeof(buf), "%u", (uint32_t)snake.size());
            // A duel says who won, and both lengths.
            std::string line1 = "GAME", line2 = "OVER";
            if (duel) {
                line1 = winner == 0 ? "TIE" : (winner == 1 ? "RED" : "BLUE");
                line2 = winner == 0 ? "GAME" : "WINS";
                snprintf(buf, sizeof(buf), "%u:%u", (uint32_t)snake.size(), (uint32_t)snake2.size());
            }
            // Centred in the playfield, so fit it to the playfield's width.
            const int scl = fitTextScale({line1, line2, buf}, scale == 0 ? 1 : scale,
                                         pfW > 0 ? pfW * std::max(1, scale) : 0);
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
            outputString(line1, centerTextX(line1, scl), y0, 255, 255, 255, scl);
            if (lines >= 2) {
                outputString(line2, centerTextX(line2, scl), y0 + lineH, 255, 255, 255, scl);
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
        if (show.on()) {
            if (button.find("Pressed") != std::string::npos) {
                show.skip();
            }
            return;
        }
        // allow base to handle pause/start behavior first (only while game is active)
        if (GameOn) {
            FPPArcadeGameEffect::button(button);
            if (isPaused()) return;
        }

        // A and B are the blue snake's: the first press brings it in, then B
        // turns it left and A right - two buttons can't give four directions,
        // but they can steer, the way light cycles do.
        if (GameOn && (button == "A Button - Pressed" || button == "B Button - Pressed")) {
            if (!duel) {
                joinBlue();
            } else {
                // Turns are from the way it last moved: two quick presses
                // before it steps would otherwise turn it back into itself.
                // The second waits for the next step.
                const int turn = button == "A Button - Pressed" ? 1 : 3;
                if (direction2 == moved2) {
                    direction2 = (moved2 + turn) % 4;
                } else {
                    queued2 = (direction2 + turn) % 4;
                }
                sfx("snake", "Turn", 0.5f);
            }
            return;
        }
        int want = -1;
        if (button == "Left - Pressed") {
            want = 0;
        } else if (button == "Up - Pressed") {
            want = 1;
        } else if (button == "Right - Pressed") {
            want = 2;
        } else if (button == "Down - Pressed") {
            want = 3;
        }
        if (want < 0) {
            return;
        }
        // Against the way it last moved, not the last press: heading right, a
        // quick Up then Left before it steps used to turn it back into its
        // own neck. A second turn in the same step waits for the next one.
        bool turned = false;
        if (direction == moved) {
            if (want != moved && want != (moved + 2) % 4) {
                direction = want;
                turned = true;
            }
        } else if (want != direction && want != (direction + 2) % 4) {
            queued = want;
            turned = true;
        }
        if (GameOn && turned) {
            // Quiet: turns are frequent, and the bites should stand out.
            sfx("snake", "Turn", 0.5f);
        }
    }
    
    
    int endShow = 1, goodGame = 20;   // the End Show and Good Game settings
    ArcadeShowPlayer show;

    std::list<std::pair<int, int>> food;
    std::list<std::pair<int, int>> snake;
    int direction = 0;   // 0 left, 1 up, 2 right, 3 down
    int moved = 0;       // the way it went on its last step
    int queued = -1;     // a second turn pressed before that step, or -1
    // The blue snake, once A or B brings it in, and the duel's result:
    // 0 a tie, 1 red won, 2 blue won.
    bool duel = false;
    std::list<std::pair<int, int>> snake2;
    int direction2 = 0, moved2 = 0, queued2 = -1, winner = 0;

    int rows = 20;
    int cols = 20;
    double moveAccumulatorMs = 0.0;

    bool GameOn = true;
    bool WaitingUntilOutput = false;
    long long timer = 100;
    int eaten = 0;

    void startSound() {
        // Cuts off the title jingle (or a game-over one) still playing.
        sfxStop("snake");
        sfx("snake", "StartGame");
    }
    
    void restart() override {
        // Reinitialize snake to starting state
        snake.clear();
        snake2.clear();
        duel = false;
        winner = 0;
        food.clear();
        direction = moved = 0;
        queued = queued2 = -1;
        snake.push_back(std::pair<int, int>(cols / 2, rows / 2));
        snake.push_back(std::pair<int, int>(cols / 2 + 1, rows / 2));
        snake.push_back(std::pair<int, int>(cols / 2 + 2, rows / 2));
        addFood(); addFood(); addFood();
        eaten = 0;
        moveAccumulatorMs = 0.0;
        GameOn = true;
        WaitingUntilOutput = false;
        resetFrameTimer();
        startSound();
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
                                     playfieldWidthOption(), playfieldHeightOption(), m,
                                     endShowOption(), goodGameOption(20));
            m->setRunningEffect(effect, 50);
        } else {
            effect->button(button);
        }
    }
}
