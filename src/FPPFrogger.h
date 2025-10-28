#ifndef FPPFROGGER_H
#define FPPFROGGER_H
#include "FPPArcade.h"
class FPPFrogger : public FPPArcadeGame {
public:
    FPPFrogger(Json::Value &config) : FPPArcadeGame(config) {}
    ~FPPFrogger() override = default;
    const std::string &getName() override;
    void button(const std::string &button) override;
};
#endif
