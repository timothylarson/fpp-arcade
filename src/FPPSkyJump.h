#ifndef __FPPARCADE_SKYJUMP__
#define __FPPARCADE_SKYJUMP__

#include "FPPArcade.h"

// Sky Jump: blast off from the ground and keep climbing by catching blasts on the way
// up, collecting coins, smashing through enemies with a star; miss the blasts and
// you fall off the bottom.
class FPPSkyJump : public FPPArcadeGame {
public:
    FPPSkyJump(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPSkyJump() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
