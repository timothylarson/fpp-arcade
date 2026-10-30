#pragma once
// Demos: recordings of real games that play on a model nobody has used for a
// while, to show passers-by what the wall does.
//
// Every game played is recorded as it looks on the wall - the model's pixels,
// about 25 times a second - while it runs (not its title, nor its pause menu).
// The last run on each model is kept in memory until the next one, and the
// Arcade Games page saves it as that game's demo on that model, in
// media/plugindata/fpp-arcade/demos/<model>/<game>.demo. A recording plays
// back exactly as it was, whatever the game's code has done since; it only
// needs the model to be the same size.
//
// With Demos on, a model whose games haven't been touched for the idle time,
// and that is showing nothing of the arcade's (the sequence, or the Kinect
// picture), plays its games' demos in turn, in the order of its game list, a
// PRESS START box coming up over them now and then. Any button on the model
// stops the demo and shows that game's title, ready to play.
//
// Orb Pop and Shape Fit aren't recorded: they play the Kinect's people, and a
// recording of strangers' silhouettes isn't a demo.

#include <list>
#include <map>
#include <string>
#include <vector>

class FPPArcadeGame;
class PixelOverlayModel;

namespace ArcadeDemo {

// The Demos settings: start after idleMin minutes without a button (0: no
// demos), and wait betweenMin minutes of the sequence between one demo and
// the next (0: one after another).
void configure(int idleMin, int betweenMin);

// A button on a model: it's in use, so no demo for the idle time.
void noteInput(const std::string &model);

// Call often (every 40 ms or so) with every model's games, front first: it
// records what's running and starts and ends demos.
void tick(std::map<std::string, std::list<FPPArcadeGame *>> &games);

// If a demo is playing on this model, stop it and give back its game's name
// (else "").
std::string stopDemo(const std::string &model);

struct Info {
    std::string game;
    double seconds = 0;
};
// The run Save would keep on a model: the game being played, so far, or else
// the last one played (game "" if none).
Info lastRun(const std::string &model);
// Save that run as its game's demo on the model; false and a reason if not.
bool saveLastRun(const std::string &model, std::string &error);
// A model's saved demos.
std::vector<Info> demos(const std::string &model);
bool remove(const std::string &model, const std::string &game);
// Play a model's saved demo for a game now (from the Arcade Games page).
bool play(const std::string &model, const std::string &game, const std::string &overlay);

}  // namespace ArcadeDemo
