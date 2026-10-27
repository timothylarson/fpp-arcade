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
    float getPaddleSpeed() const { return paddleSpeed; }
    int getCapsuleChance() const { return capsuleChance; }
    bool getWinCredits() const { return winCredits; }
    const std::string &getCredits() const { return credits; }
    const std::string &getThanks() const { return thanks; }
    bool getRowGaps() const { return rowGaps; }
    int getTopSpace() const { return topSpace; }

protected:
    int brickPixelWidth = 2;
    bool brickSpacing = true;
    int maxBricksPerRow = 15;
    int paddleWidth = 0; // 0 = auto
    float ballSpeed = 1.0f;
    float topSpeed = 1.6f;      // the most the levels speed the ball (and paddle) up
    float paddleSpeed = 1.15f;  // the paddle's speed: 1 is how it moved before the setting
    int capsuleChance = 20;     // % of broken bricks that drop a capsule (when one can fall)
    bool winCredits = true;     // the credits show after the last level
    std::string credits = "THANKS FOR PLAYING|MERRY CHRISTMAS";   // lines for it, | between
    std::string thanks = "SANTA|RUDOLPH";                         // its SPECIAL THANKS, | between
    bool rowGaps = false;       // a gap between brick rows (No stacks them, Arkanoid style)
    int topSpace = 3;           // with stacked rows, LEDs left open above the top row
};

#endif
