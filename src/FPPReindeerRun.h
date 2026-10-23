#ifndef __FPPARCADE_REINDEERRUN__
#define __FPPARCADE_REINDEERRUN__

#include "FPPArcade.h"

// Reindeer Run: an endless runner - jump and duck past the obstacles as the run
// speeds up.
class FPPReindeerRun : public FPPArcadeGame {
public:
    FPPReindeerRun(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPReindeerRun() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
