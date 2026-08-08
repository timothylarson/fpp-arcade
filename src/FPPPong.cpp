#include <fpp-pch.h>

#include "FPPPong.h"
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
    PongEffect(int sc, int c, PixelOverlayModel *m) : FPPArcadeGameEffect(m), controls(c) {
        m->getSize(cols, rows);
        scale = sc;
        cols /= sc;
        rows /= sc;
        
        racketSize = rows / 5;
        if (racketSize < 3) {
            racketSize = 3;
        }
        racketP1Pos = racketP2Pos = (rows - racketSize)/2;
        offsetX = 0;
        offsetY = 0;
        
        ballPosX = cols / 2;
        ballPosY = rows / 2;
    }
    ~PongEffect() {
    }
    
    
    void CopyToModel() {
        model->clearOverlayBuffer();
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
        
        outputString(buf, (model->getWidth()/2 - len*2) / scl, 0, 128, 128, 128, scl);
        
        for (int y = 0; y < racketSize; y++) {
            outputPixel(0, racketP1Pos + y, 255, 255, 255);
            outputPixel(cols-1, racketP2Pos + y, 255, 255, 255);
        }
        outputPixel(ballPosX, ballPosY, 255, 255, 255);
        
    }
    const std::string &name() const override {
        static std::string NAME = "Pong";
        return NAME;
    }

    virtual int32_t update() override {
        if (GameOn) {
            moveRackets();
            moveBall();
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
            outputString("GAME", (cols-8)/ 2, rows/2-6);
            outputString("OVER", (cols-8)/ 2, rows/2);
            model->flushOverlayBuffer();
            return 2000;
        }
        model->flushOverlayBuffer();
        return timer;
    }
    
    void moveRackets() {
        racketP1Pos += racketP1Speed;
        if (racketP1Pos < 0) {
            racketP1Pos = 0;
        } else if ((racketP1Pos + racketSize) > rows) {
            racketP1Pos = rows - racketSize;
        }
        racketP2Pos += racketP2Speed;
        if (racketP2Pos < 0) {
            racketP2Pos = 0;
        } else if ((racketP2Pos + racketSize) > rows) {
            racketP2Pos = rows - racketSize;
        }
    }
    void moveBall() {
        ballPosX += ballDirX * ballSpeed;
        ballPosY += ballDirY * ballSpeed;
        
        // hit by left racket?
         if (ballPosX <= 1 &&
             ballPosY <= (racketP1Pos + racketSize) &&
             ballPosY >= racketP1Pos) {
             // set fly direction depending on where it hit the racket
             // (t is 0.5 if hit at top, 0 at center, -0.5 at bottom)
             float t = ((ballPosY - racketP1Pos) / racketSize) - 0.5f;
             ballDirX = std::fabs(ballDirX);
             ballDirY = t;
         }
        
         // hit by right racket?
         if (ballPosX >= (cols-2) &&
             ballPosY <= (racketP2Pos + racketSize) &&
             ballPosY >= racketP2Pos) {
             // set fly direction depending on where it hit the racket
             // (t is 0.5 if hit at top, 0 at center, -0.5 at bottom)
             float t = ((ballPosY - racketP2Pos) / racketSize) - 0.5f;
             ballDirX = -std::fabs(ballDirX);
             ballDirY = t;
         }

         if (ballPosX < 0) {
             //left wall
             ++p2Score;
             ballPosX = cols / 2;
             ballPosY = rows / 2;
             ballDirX = std::fabs(ballDirX);
             ballDirY = 0;
         }

         // hit right wall?
         if (ballPosX >= cols) {
             //right wall
             ++p1Score;
             ballPosX = cols / 2;
             ballPosY = rows / 2;
             ballDirX = -std::fabs(ballDirX);
             ballDirY = 0;
         }

         if (ballPosY >= rows) {
             //hit bottom
             ballDirY = -std::fabs(ballDirY);
             ballPosY = rows-1;
         }
         if (ballPosY < 0) {
             //hit top
             ballDirY = std::fabs(ballDirY);
             ballPosY = 0;
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

    int rows = 20;
    int cols = 20;
    
    int p1Score = 0;
    int p2Score = 0;
    
    int racketSize = 1;
    int racketP1Pos;
    int racketP1Speed = 0;
    int racketP2Pos;
    int racketP2Speed = 0;
    
    float ballPosX = 0;
    float ballPosY = 0;
    float ballDirX = 1;
    float ballDirY = 0;
    float ballSpeed = 1;

    
    bool GameOn = true;
    bool WaitingUntilOutput = false;
    
    long long timer = 50;
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
            int controls = std::stoi(findOption("Controls", "1"));
            int pixelScaling = std::stoi(findOption("Pixel Scaling", "1"));
            effect = new PongEffect(pixelScaling, controls, m);
            effect->button(button);
            m->setRunningEffect(effect, 50);
        } else {
            effect->button(button);
        }
    }
}
