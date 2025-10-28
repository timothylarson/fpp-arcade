#ifndef __FPPARCADE_BREAKOUT_
#define __FPPARCADE_BREAKOUT_

#include "FPPArcade.h"

class FPPBreakout : public FPPArcadeGame {
public:
    FPPBreakout(Json::Value &config);
    virtual ~FPPBreakout() override = default;

    virtual const std::string &getName() override;
    virtual void button(const std::string &button) override;

    int getBrickPixelWidth() const { return brickPixelWidth; }
    bool getBrickSpacing() const { return brickSpacing; }
    int getMaxBricksPerRow() const { return maxBricksPerRow; }
    int getPaddleWidth() const { return paddleWidth; }
    float getBallSpeed() const { return ballSpeed; }

protected:
    int brickPixelWidth = 2;
    bool brickSpacing = true;
    int maxBricksPerRow = 15;
    int paddleWidth = 0; // 0 = auto
    float ballSpeed = 1.0f;
};

#endif
