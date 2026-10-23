#ifndef __FPPARCADE_SNOWBALLFIGHT__
#define __FPPARCADE_SNOWBALLFIGHT__

#include "FPPArcade.h"

// Snowball Fight: two throwers on snowy hills take turns lobbing snowballs, aiming
// for angle and power against the wind; every hit blows a crater out of the hill.
class FPPSnowballFight : public FPPArcadeGame {
public:
    FPPSnowballFight(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPSnowballFight() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
