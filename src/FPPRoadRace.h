#ifndef __FPPARCADE_ROADRACE__
#define __FPPARCADE_ROADRACE__

#include "FPPArcade.h"

// Road Race: a road scrolling across the wall; change lanes past the traffic as it
// speeds up.
class FPPRoadRace : public FPPArcadeGame {
public:
    FPPRoadRace(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPRoadRace() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
