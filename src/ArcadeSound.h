#pragma once
// Sound effects for the games: short WAV clips mixed together and played on a
// sound card (Linux/ALSA), and optionally streamed over UDP so a computer can
// play them - the Matrix Viewer on a Mac does - for testing without a speaker
// on the player.
//
// Clips are read from <media>/plugindata/fpp-arcade/sounds/<game>/<name>.wav,
// where <game> is the game's name in lower case without spaces ("Breakout",
// "Maze Chase" -> "mazechase"). A clip that isn't there is silently skipped, so
// a game can call play() for every event and the sounds are whatever files
// have been put in its folder. Clips should be 16-bit PCM WAV, ideally mono at
// 44.1 kHz; stereo is mixed down and other rates are resampled.
//
// Stream format, one UDP datagram per 10 ms block while anything is playing:
//   "FAS1", u32 sequence, u32 sample rate, u16 frames, u16 channels (1),
//   then frames x int16 samples, all little-endian.

#include <string>

namespace Json {
class Value;
}

namespace ArcadeSound {

// Apply the "sound" object from plugin.fpp-arcade.json:
//   enabled (bool), device ("auto", "none" or an ALSA name such as
//   "plughw:1,0"), volume (0-100), streamHost (IP or name, blank for none),
//   streamPort (default 5570). Safe to call again with new settings.
void configure(const Json::Value &cfg);

// Play a clip. gain scales it (1 = as recorded).
void play(const std::string &game, const std::string &name, float gain = 1.0f);

// Stop a game's clips (all clips when game is empty).
void stopAll(const std::string &game = "");

// Stop the mixer and close the sound card. Called before FPP unloads the plugin.
void shutdown();

// The sound folder name for a game's display name.
std::string folderFor(const std::string &gameName);

}  // namespace ArcadeSound
