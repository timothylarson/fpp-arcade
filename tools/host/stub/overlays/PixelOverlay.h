#pragma once
// Stands in for FPP's overlays/PixelOverlay.h in tools/host: the models by name.

#include <map>
#include <memory>
#include <string>

#include "overlays/PixelOverlayModel.h"

class PixelOverlayManager {
public:
    static PixelOverlayManager INSTANCE;

    PixelOverlayModel *getModel(const std::string &name) {
        auto it = models.find(name);
        return it == models.end() ? nullptr : it->second.get();
    }
    PixelOverlayModel *addModel(const std::string &name, int w, int h) {
        models[name] = std::make_unique<PixelOverlayModel>(name, w, h);
        return models[name].get();
    }

private:
    std::map<std::string, std::unique_ptr<PixelOverlayModel>> models;
};
