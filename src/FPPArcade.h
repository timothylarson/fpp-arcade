#ifndef __FPPARCADE__
#define __FPPARCADE__

#include <string>

#include "overlays/PixelOverlayEffects.h"

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
protected:
    std::string findOption(const std::string &s, const std::string &def = "");
    
    std::string modelName;    
    Json::Value config;
    int lastValues[2];
    int idx;
};


class FPPArcadeGameEffect : public RunningEffect {
public:
    FPPArcadeGameEffect(PixelOverlayModel *m);
    virtual ~FPPArcadeGameEffect();
    

    void outputString(const std::string &s, int x, int y, int r = 255, int g = 255, int b = 255, int scl = -1);
    void outputLetter(int x, int y, char letter, int r = 255, int g = 255, int b = 255, int scl = -1);
    int centerTextX(const std::string &s, int scl = -1);
    void outputPixel(int x, int y, int r, int g, int b, int scl = -1);
    
    int scale;
    int offsetX;
    int offsetY;
};

#endif
