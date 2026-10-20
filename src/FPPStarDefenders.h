#ifndef __FPPARCADE_STARDEFENDERS__
#define __FPPARCADE_STARDEFENDERS__

#include "FPPArcade.h"

// Star Defenders: hold off a marching formation of aliens from behind
// crumbling shields. Left/Right move, A fires, B fires the laser (once a wave).
class FPPStarDefenders : public FPPArcadeGame {
public:
    FPPStarDefenders(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPStarDefenders() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
