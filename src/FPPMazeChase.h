#ifndef __FPPARCADE_MAZECHASE__
#define __FPPARCADE_MAZECHASE__

#include "FPPArcade.h"

// Maze Chase: eat every dot in a maze while four ghosts hunt you; a power
// pellet turns the tables for a few seconds. Directions steer. Each level is a
// new symmetric maze generated to fit the model.
class FPPMazeChase : public FPPArcadeGame {
public:
    FPPMazeChase(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPMazeChase() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
