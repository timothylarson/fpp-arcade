#pragma once
// End shows come in three looks, each with its own music: Christmas,
// Halloween and Everyday. The Arcade Games page picks one for every game, or
// By date. A show reads the theme from its ShowConfig (ArcadeShow.h), which
// takes it from here when it's made.

#include <string>

enum class ShowTheme { Christmas, Halloween, Everyday };

// The theme now: the setting, with By date worked out from today - October
// is Halloween, 15 November to 6 January Christmas, and the rest Everyday.
ShowTheme arcadeShowTheme();
// From the page: "christmas", "halloween", "everyday" or "date".
void arcadeSetShowTheme(const std::string &setting);

// A game's end-show music for a theme. EndShow is the Christmas one; a game
// with no file of the theme's own falls back to it (its EndShow is a plain
// fanfare rather than a carol).
inline const char *showThemeSound(ShowTheme t) {
    return t == ShowTheme::Halloween ? "EndShowHalloween" : t == ShowTheme::Everyday ? "EndShowEveryday" : "EndShow";
}
