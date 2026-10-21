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
    float getTopSpeed() const { return topSpeed; }
    bool getWinCredits() const { return winCredits; }
    const std::string &getCredits() const { return credits; }

protected:
    int brickPixelWidth = 2;
    bool brickSpacing = true;
    int maxBricksPerRow = 15;
    int paddleWidth = 0; // 0 = auto
    float ballSpeed = 1.0f;
    float topSpeed = 1.6f;      // the most the levels speed the ball (and paddle) up
    bool winCredits = true;     // the credits show after the last level
    std::string credits = "THANKS FOR PLAYING|MERRY CHRISTMAS";   // lines for it, | between
};

#endif
