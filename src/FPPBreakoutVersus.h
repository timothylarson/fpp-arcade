#ifndef __FPPARCADE_BREAKOUTVERSUS__
#define __FPPARCADE_BREAKOUTVERSUS__

#include "FPPArcade.h"

// Breakout Versus: two players on one controller and a wall of bricks between
// them. The bottom paddle moves on Left/Right and launches or fires with Up;
// the top one moves on B (left) and A (right), and its ball launches and its
// laser fires by themselves, as it has no button left over. Get your ball
// past the other paddle to score; first to Goals wins.
class FPPBreakoutVersus : public FPPArcadeGame {
public:
    FPPBreakoutVersus(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPBreakoutVersus() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
