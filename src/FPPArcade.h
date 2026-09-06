#ifndef __FPPARCADE__
#define __FPPARCADE__

#include <string>
#include <chrono>
#include <cstdint>

#include "overlays/PixelOverlayEffects.h"
#include "FPPArcadePause.h"

class FPPArcadeGame {
public:
    FPPArcadeGame(Json::Value &config);
    virtual ~FPPArcadeGame() {}
    
    virtual const std::string &getName() = 0;
    
    virtual void button(const std::string &button) {}
    virtual void axis(const std::string &axis, int value);

    
    virtual bool isRunning();
    virtual void stop();

    int getIdx() const { return idx; };
    void setIdx(int i) { idx = i; }
    const std::string &getModelName() const { return modelName; }
protected:
    std::string findOption(const std::string &s, const std::string &def = "");
    // findOption() as an int, tolerating a blank or non-numeric value in the
    // config rather than throwing std::invalid_argument out of a button press.
    int findIntOption(const std::string &s, int def);
    // The two universal sizing options. 0 means "fill the panel"; see
    // FPPArcadeGameEffect::setPlayfield().
    int playfieldWidthOption()  { return findIntOption("Playfield Width", 0); }
    int playfieldHeightOption() { return findIntOption("Playfield Height", 0); }
    
    std::string modelName;    
    Json::Value config;
    int lastValues[2];
    int idx;
};


class FPPArcadeGameEffect : public RunningEffect {
public:
    FPPArcadeGameEffect(PixelOverlayModel *m);
    virtual ~FPPArcadeGameEffect();
    
    virtual void button(const std::string &button);

    void outputString(const std::string &s, int x, int y, int r = 255, int g = 255, int b = 255, int scl = -1);
    void outputLetter(int x, int y, char letter, int r = 255, int g = 255, int b = 255, int scl = -1);
    void drawUnderline(int x, int y, int length, int r = 255, int g = 255, int b = 255, int scl = -1);
    int centerTextX(const std::string &s, int scl = -1);
    void outputPixel(int x, int y, int r, int g, int b, int scl = -1);
    double consumeElapsedMs(double fallbackMs, double maxClampMs = 250.0);
    void resetFrameTimer();
    
    // Pause menu methods
    virtual void pause();
    virtual void resume();
    virtual void restart();
    void activatePauseSelection();
    bool isPaused() const { return paused; }
    
    // Size the logical playfield and centre it on the model. A game should
    // bound itself against pfW/pfH rather than model->getWidth()/getHeight(),
    // so that what it collides with and what outputPixel() draws can never
    // disagree - that mismatch is what let Breakout's ball travel through dead
    // panel beside its brick field.
    //
    // logicalW/logicalH are in game cells, not panel pixels; 0 (or anything
    // larger than the panel can hold) means "fill the panel", so a misconfigured
    // value degrades to full screen instead of drawing off-model.
    void setPlayfield(int logicalW, int logicalH, int scl);
    int pfW = 0;
    int pfH = 0;

    // Draw in absolute panel pixels for the lifetime of the object: zeroes the
    // transform AND widens the centring reference, so centerTextX() measures the
    // panel instead of the playfield. Zeroing the transform alone left
    // full-screen text jammed against x=0 whenever the playfield was narrower
    // than the text.
    struct PanelTransform {
        explicit PanelTransform(FPPArcadeGameEffect *eff);
        ~PanelTransform();
    private:
        FPPArcadeGameEffect *e;
        int s, ox, oy, w, h;
    };

    int scale;
    int offsetX;
    int offsetY;
protected:
    void drawPauseMenu();
    virtual void handlePauseInput(const std::string &button);

    bool paused = false;
    PauseMenu pauseMenu;

private:
    std::chrono::steady_clock::time_point lastFrameTime;
    bool firstFrame = true;
};

#endif
