#include <fpp-pch.h>

#include "FPPPong.h"
#include "PongShow.h"
#include <algorithm>
#include <array>
#include <cmath>

#include "overlays/PixelOverlay.h"
#include "overlays/PixelOverlayModel.h"
#include "overlays/PixelOverlayEffects.h"


FPPPong::FPPPong(Json::Value &config) : FPPArcadeGame(config) {
}
FPPPong::~FPPPong() {
}

class PongEffect : public FPPArcadeGameEffect {
public:
    PongEffect(int sc, int c, int pfw, int pfh, PixelOverlayModel *m, int showMode = 1, int goodMargin = 3)
        : FPPArcadeGameEffect(m), controls(c), endShow(showMode), goodGame(goodMargin) {
        // 0/0 keeps the historical behaviour of playing across the whole panel;
        // a configured size is centred on it.
        setPlayfield(pfw, pfh, sc);
        cols = pfW;
        rows = pfH;
        
        racketSize = rows / 5;
        if (racketSize < 3) {
            racketSize = 3;
        }
        racketP1Pos = racketP2Pos = (rows - racketSize)/2;
        racketP1PosF = static_cast<float>(racketP1Pos);
        racketP2PosF = static_cast<float>(racketP2Pos);
        
        ballPosX = cols / 2;
        ballPosY = rows / 2;
        startSound();
    }
    ~PongEffect() {
    }
    
    
    void CopyToModel() {
        model->clearOverlayBuffer();
        // The court's edge, dimly lit, so the walls the ball bounces off and
        // the goal lines behind the paddles can be seen. Everything else is
        // drawn over it.
        for (int x = 0; x < cols; x++) {
            outputPixel(x, 0, 35, 35, 70);
            outputPixel(x, rows - 1, 35, 35, 70);
        }
        for (int y = 1; y < rows - 1; y++) {
            outputPixel(0, y, 35, 35, 70);
            outputPixel(cols - 1, y, 35, 35, 70);
        }
        char buf[25];
        snprintf(buf, sizeof(buf), "%d:%d", p1Score, p2Score);
        int len = strlen(buf);
        float scl = scale;
        
        while (((model->getHeight() / scl) < 40) && scl > 1) {
            scl *= 0.80;
        }
        if (scl < 1) {
            scl = 1;
        }
        
        // Below the border, with a row clear of it.
        outputString(buf, (model->getWidth()/2 - len*2) / scl, scl <= 1 ? 2 : 1, 128, 128, 128, scl);
        
        // The paddles stand one in from the border, on the court.
        for (int y = 0; y < racketSize; y++) {
            outputPixel(1, racketP1Pos + y, 255, 255, 255);
            outputPixel(cols-2, racketP2Pos + y, 255, 255, 255);
        }
        outputPixel(ballPosX, ballPosY, 255, 255, 255);
        
    }
    const std::string &name() const override {
        static std::string NAME = "Pong";
        return NAME;
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
            return 50;
        }
        double frameScalar = 1.0;
        if (GameOn) {
            double elapsedMs = consumeElapsedMs(static_cast<double>(timer));
            if (elapsedMs <= 0.0) {
                elapsedMs = static_cast<double>(timer);
            }
            frameScalar = std::clamp(elapsedMs / static_cast<double>(timer), 0.1, 5.0);
        } else {
            // Ensure timer resets when restarting after game over.
            resetFrameTimer();
        }
        if (GameOn) {
            moveRackets(frameScalar);
            moveBall(frameScalar);
        }
        CopyToModel();
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
        if (p1Score >= 5 || p2Score >= 5) {
            GameOn = false;
            // Someone always wins Pong, so a good game is a clear win: by the
            // Good Game margin or more. There's no one player's best score to
            // keep, so no best is recorded and newBest is never set.
            const int margin = std::abs(p1Score - p2Score);
            if (arcadeWantShow(endShow, margin >= goodGame, false)) {
                sfxStop("pong");
                sfxEndShow("pong");
                show.start(model, PongShow::config(p1Score, p2Score, model->getWidth()),
                           std::make_unique<PongShow::Story>(p1Score > p2Score, p1Score, p2Score));
                resetFrameTimer();
                return 50;
            }
            // Centred across the court, and below the score (rows 2 to 6)
            // rather than on top of it. The old x of (cols - 8) / 2 assumed
            // two-letter words and sat four columns right of centre.
            const int y0 = std::max(8, (rows - 11) / 2 + 1);
            outputString("GAME", centerTextX("GAME"), y0);
            outputString("OVER", centerTextX("OVER"), y0 + 6);
            sfxStop("pong");
            sfx("pong", "GameOver");
            model->flushOverlayBuffer();
            return 2000;
        }
        model->flushOverlayBuffer();
        return timer;
    }
    
    void moveRackets(double frameScalar) {
        float deltaP1 = static_cast<float>(racketP1Speed * frameScalar);
        float deltaP2 = static_cast<float>(racketP2Speed * frameScalar);
        racketP1PosF += deltaP1;
        racketP2PosF += deltaP2;
        // Inside the border: rows 1 to rows - 2.
        float maxPos = static_cast<float>(rows - 1 - racketSize);
        racketP1PosF = std::clamp(racketP1PosF, 1.0f, maxPos);
        racketP2PosF = std::clamp(racketP2PosF, 1.0f, maxPos);
        racketP1Pos = static_cast<int>(std::round(racketP1PosF));
        racketP2Pos = static_cast<int>(std::round(racketP2PosF));
    }
    void moveBall(double frameScalar) {
        ballPosX += ballDirX * ballSpeed * frameScalar;
        ballPosY += ballDirY * ballSpeed * frameScalar;
        
        // The court is inside the border: the paddles are in columns 1 and
        // cols - 2, the ball bounces between rows 1 and rows - 2, and it
        // scores when it reaches the border column behind a paddle.
        // hit by left racket?
         if (ballPosX <= 2 &&
             ballPosY <= (racketP1Pos + racketSize) &&
             ballPosY >= racketP1Pos) {
             // set fly direction depending on where it hit the racket
             // (t is 0.5 if hit at top, 0 at center, -0.5 at bottom)
             float t = ((ballPosY - racketP1Pos) / racketSize) - 0.5f;
             if (ballDirX < 0) {
                 sfx("pong", "Paddle");
             }
             ballDirX = std::fabs(ballDirX);
             ballDirY = t;
         }
        
         // hit by right racket?
         if (ballPosX >= (cols-3) &&
             ballPosY <= (racketP2Pos + racketSize) &&
             ballPosY >= racketP2Pos) {
             // set fly direction depending on where it hit the racket
             // (t is 0.5 if hit at top, 0 at center, -0.5 at bottom)
             float t = ((ballPosY - racketP2Pos) / racketSize) - 0.5f;
             if (ballDirX > 0) {
                 sfx("pong", "Paddle");
             }
             ballDirX = -std::fabs(ballDirX);
             ballDirY = t;
         }

         if (ballPosX < 1) {
             //left wall
             ++p2Score;
             sfx("pong", "Point");
             ballPosX = cols / 2;
             ballPosY = rows / 2;
             ballDirX = std::fabs(ballDirX);
             ballDirY = 0;
         }

         // hit right wall?
         if (ballPosX >= cols - 1) {
             //right wall
             ++p1Score;
             sfx("pong", "Point");
             ballPosX = cols / 2;
             ballPosY = rows / 2;
             ballDirX = -std::fabs(ballDirX);
             ballDirY = 0;
         }

         if (ballPosY >= rows - 1) {
             //hit bottom
             if (ballDirY > 0) {
                 sfx("pong", "Wall");
             }
             ballDirY = -std::fabs(ballDirY);
             ballPosY = rows-2;
         }
         if (ballPosY < 1) {
             //hit top
             if (ballDirY < 0) {
                 sfx("pong", "Wall");
             }
             ballDirY = std::fabs(ballDirY);
             ballPosY = 1;
         }

         // make sure that length of dir stays at 1
         vec2_norm(ballDirX, ballDirY);
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
        if (controls == 2) {
            if (button == "Up/Right - Pressed") {
                racketP2Speed = -1;
            } else if (button == "Down/Right - Pressed") {
                racketP2Speed = 1;
            } else if (button == "Up/Right - Released" || button == "Down/Right - Released") {
                racketP2Speed = 0;
            } else if (button == "Up/Left - Pressed") {
                racketP1Speed = -1;
            } else if (button == "Down/Left - Pressed") {
                racketP1Speed = 1;
            } else if (button == "Down/Left - Released" || button == "Up/Left - Released") {
                racketP1Speed = 0;
            }
        } else if (controls == 4) {
            // One player on Up/Down, the other on B (up) and A (down). A
            // release only stops the paddle if it is that button's direction,
            // so letting go of one while holding the other keeps it moving.
            if (button == "B Button - Pressed") {
                racketP2Speed = -1;
            } else if (button == "A Button - Pressed") {
                racketP2Speed = 1;
            } else if ((button == "B Button - Released" && racketP2Speed < 0) ||
                       (button == "A Button - Released" && racketP2Speed > 0)) {
                racketP2Speed = 0;
            } else if (button == "Up - Pressed") {
                racketP1Speed = -1;
            } else if (button == "Down - Pressed") {
                racketP1Speed = 1;
            } else if ((button == "Up - Released" && racketP1Speed < 0) ||
                       (button == "Down - Released" && racketP1Speed > 0)) {
                racketP1Speed = 0;
            }
        } else if (controls == 3) {
            if (button == "Right - Pressed") {
                racketP2Speed = -1;
            } else if (button == "Down - Pressed") {
                racketP2Speed = 1;
            } else if (button == "Right - Released" || button == "Down - Released") {
                racketP2Speed = 0;
            } else if (button == "Up - Pressed") {
                racketP1Speed = -1;
            } else if (button == "Left - Pressed") {
                racketP1Speed = 1;
            } else if (button == "Left - Released" || button == "Up - Released") {
                racketP1Speed = 0;
            }
        } else {
            if (button == "Left - Pressed") {
                racketP2Speed = -1;
            } else if (button == "Right - Pressed") {
                racketP2Speed = 1;
            } else if (button == "Right - Released" || button == "Left - Released") {
                racketP2Speed = 0;
            } else if (button == "Up - Pressed") {
                racketP1Speed = -1;
            } else if (button == "Down - Pressed") {
                racketP1Speed = 1;
            } else if (button == "Down - Released" || button == "Up - Released") {
                racketP1Speed = 0;
            }
        }
    }
    
    int controls;
    int endShow = 1, goodGame = 3;   // the End Show and Good Game settings
    ArcadeShowPlayer show;

    int rows = 20;
    int cols = 20;
    
    int p1Score = 0;
    int p2Score = 0;
    
    int racketSize = 1;
    int racketP1Pos;
    float racketP1PosF = 0.0f;
    int racketP1Speed = 0;
    int racketP2Pos;
    float racketP2PosF = 0.0f;
    int racketP2Speed = 0;
    
    float ballPosX = 0;
    float ballPosY = 0;
    float ballDirX = 1;
    float ballDirY = 0;
    float ballSpeed = 1;

    
    bool GameOn = true;
    bool WaitingUntilOutput = false;
    
    long long timer = 50;

    void startSound() {
        // Cuts off the title jingle (or a game-over one) still playing.
        sfxStop("pong");
        sfx("pong", "StartGame");
    }

    void restart() override {
        // Reset scores and positions
        p1Score = 0;
        p2Score = 0;
        racketSize = rows / 5;
        if (racketSize < 3) racketSize = 3;
        racketP1Pos = racketP2Pos = (rows - racketSize)/2;
        racketP1PosF = static_cast<float>(racketP1Pos);
        racketP2PosF = static_cast<float>(racketP2Pos);
        ballPosX = cols / 2;
        ballPosY = rows / 2;
        ballDirX = 1;
        ballDirY = 0;
        GameOn = true;
        WaitingUntilOutput = false;
        resetFrameTimer();
        startSound();
    }
};

const std::string &FPPPong::getName() {
    static const std::string name = "Pong";
    return name;
}


void FPPPong::button(const std::string &button) {
    PixelOverlayModel *m = PixelOverlayManager::INSTANCE.getModel(modelName);
    if (m != nullptr) {
        PongEffect *effect = dynamic_cast<PongEffect*>(m->getRunningEffect());
        if (!effect) {
            if (findOption("overlay", "Overwrite") == "Transparent") {
                m->setState(PixelOverlayState(PixelOverlayState::PixelState::TransparentRGB));
            } else {
                m->setState(PixelOverlayState(PixelOverlayState::PixelState::Enabled));
            }
            int pixelScaling = std::stoi(findOption("Pixel Scaling", "1"));
            int controls = std::stoi(findOption("Controls", "1"));
            effect = new PongEffect(pixelScaling, controls,
                                    playfieldWidthOption(), playfieldHeightOption(), m,
                                    endShowOption(), goodGameOption(3));
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
