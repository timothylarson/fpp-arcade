#ifndef __FPPARCADE_SHAPEFIT__
#define __FPPARCADE_SHAPEFIT__

#include "FPPArcade.h"

// Shape Fit: a pose outline appears on the wall and everyone in front of the
// Kinect tries to fill it before the countdown runs out. Needs the fpp-kinect
// plugin on the same model. Any button starts five rounds.
class FPPShapeFit : public FPPArcadeGame {
public:
    FPPShapeFit(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPShapeFit() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
