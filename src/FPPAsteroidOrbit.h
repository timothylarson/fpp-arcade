#ifndef __FPPARCADE_ASTEROIDORBIT__
#define __FPPARCADE_ASTEROIDORBIT__

#include "FPPArcade.h"

// Asteroid Orbit: fly a thruster ship through a field of asteroids that each
// pull on it. Directions thrust (the ship points the way it flies), A fires,
// B sends out a shockwave. Land gently on an asteroid to reload; clear the
// sector and a black hole opens to the next one.
class FPPAsteroidOrbit : public FPPArcadeGame {
public:
    FPPAsteroidOrbit(Json::Value &config) : FPPArcadeGame(config) {}
    virtual ~FPPAsteroidOrbit() {}

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
};

#endif
