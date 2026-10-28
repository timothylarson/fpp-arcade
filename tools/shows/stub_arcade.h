#pragma once
// The parts of FPPArcade.h a show uses, for previewing off the player.
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "ShowTheme.h"
bool arcadeGlyph(char c, int x, int y);
bool arcadeGlyphScaled(char c, int px, int py, int scl);
int arcadeGlyphWidth(char c, int scl);
