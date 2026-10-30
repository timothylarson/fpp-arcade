#pragma once
// Stands in for FPP's overlays/PixelOverlayEffects.h in tools/host: an effect
// runs on a model, and update() says when to call it again.

#include <cstdint>
#include <string>

class PixelOverlayModel;

// update()'s answers besides "call again in this many ms".
#define EFFECT_DONE 0                 // finished: the model drops it
#define EFFECT_AFTER_NEXT_OUTPUT -1   // call again once the frame has gone out

class RunningEffect {
public:
    explicit RunningEffect(PixelOverlayModel *m) : model(m) {}
    virtual ~RunningEffect() {}
    virtual const std::string &name() const = 0;
    virtual int32_t update() = 0;

    PixelOverlayModel *model;
};
