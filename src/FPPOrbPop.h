#ifndef __FPPARCADE_ORBPOP__
#define __FPPARCADE_ORBPOP__

#include "FPPArcade.h"

// Orb Pop: played with your body in front of a Kinect (needs the fpp-kinect
// plugin running on the same player). When nobody is playing it shows the
// wall as a field of flickering lights with slow shaded orbs and the people in
// front of it; someone standing in view for a moment starts a timed round of
// popping orbs by touching them.
class FPPOrbPop : public FPPArcadeGame {
public:
    FPPOrbPop(Json::Value &config);
    virtual ~FPPOrbPop();

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;
    virtual void release() override;

private:
    void startEffect();
    void autoStartTick();
    std::string timerName;
};

#endif
