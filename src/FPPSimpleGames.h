#ifndef __FPPARCADE_SIMPLEGAMES__
#define __FPPARCADE_SIMPLEGAMES__

#include "FPPArcade.h"

// Three small games that need very little to play, for a crowd walking up:
//
// Stacker: a row of blocks slides back and forth; A drops it on the stack
//   and whatever overhangs falls off. Reach the top.
// Flap: A (or Up) flaps; fly through the gaps in the pipes.
// Light Memory: watch the sequence of lit triangles, then repeat it with the
//   directions. It grows by one each round.
class FPPStacker : public FPPArcadeGame {
public:
    FPPStacker(Json::Value &config) : FPPArcadeGame(config) {}
    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

class FPPFlap : public FPPArcadeGame {
public:
    FPPFlap(Json::Value &config) : FPPArcadeGame(config) {}
    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

class FPPLightMemory : public FPPArcadeGame {
public:
    FPPLightMemory(Json::Value &config) : FPPArcadeGame(config) {}
    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
