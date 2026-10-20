#ifndef __FPPARCADE_BARRELCLIMB__
#define __FPPARCADE_BARRELCLIMB__

#include "FPPArcade.h"

// Barrel Climb: climb sloping girders and ladders to the top while an ape
// rolls barrels down at you. Left/Right walk, Up/Down climb ladders, A jumps.
class FPPBarrelClimb : public FPPArcadeGame {
public:
    FPPBarrelClimb(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPBarrelClimb() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
