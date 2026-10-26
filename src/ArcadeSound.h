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
//
// Controller speakers (speaker/ArcadeSpeaker, an ESP32 with an amp) keep their
// own copy of the sounds and are sent events instead of audio. Each one says
// hello to UDP port 5571 every few seconds with the model it belongs to:
//   "FAS-HELLO 1 <model>|<name>|<firmware>|<volume>"
// and is sent, from that port to wherever its hello came from:
//   "PLAY <seq> <game>/<name> <gain> <ms>"
//                                        play a clip (gain 0..1, volume applied);
//                                        ms is this player's steady clock, from
//                                        which firmware 1.3 and later skips a
//                                        short sound that arrived late
//   "STOP <game>"                        stop a game's clips ("*" for all)
//   "SYNC"                               the sound files changed: fetch them
// It fetches the files over HTTP: api/plugin-apis/arcade/sounds lists them
// (with a version that changes when a file does) and
// api/plugin-apis/arcade/sound?f=<game>/<name>&rate=<hz> returns one as raw
// 16-bit mono little-endian PCM at that rate, or with &fmt=ulaw as "ULW1"
// followed by one G.711 mu-law byte a sample (speaker firmware 1.1 and later).

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

// Play a clip. gain scales it (1 = as recorded). model routes it to that
// model's controller speakers; empty sends it to every speaker.
void play(const std::string &game, const std::string &name, float gain = 1.0f, const std::string &model = "");

// How long a clip plays, in ms: 0 when sound is off or there is no such file.
// Lets a game wait for a jingle to finish.
int lengthMs(const std::string &game, const std::string &name);

// Stop a game's clips (all clips when game is empty).
void stopAll(const std::string &game = "", const std::string &model = "");

// For the plugin's HTTP API.
std::string speakersJson();
std::string soundListJson();
// A sound as raw 16-bit mono PCM at `rate`; false if there's no such file.
bool soundPcm(const std::string &path, int rate, std::string &out);
// The same as "ULW1" and then one G.711 mu-law byte a sample: half the size,
// so every game's sounds fit in a speaker's memory.
bool soundUlaw(const std::string &path, int rate, std::string &out);

// Stop the mixer and close the sound card. Called before FPP unloads the plugin.
void shutdown();

// The sound folder name for a game's display name.
std::string folderFor(const std::string &gameName);

}  // namespace ArcadeSound
