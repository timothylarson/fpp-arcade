#ifndef __FPPARCADE_STICKBRIDGE__
#define __FPPARCADE_STICKBRIDGE__

#include "FPPArcade.h"

// Stick Bridge: hold to grow a stick, let go to drop it across the gap to the next
// pillar, and walk over; too short or too long and you fall.
class FPPStickBridge : public FPPArcadeGame {
public:
    FPPStickBridge(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPStickBridge() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
