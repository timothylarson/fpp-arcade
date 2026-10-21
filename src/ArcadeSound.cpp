#include <fpp-pch.h>

#include "ArcadeSound.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifndef PLATFORM_OSX
#include <alsa/asoundlib.h>
#endif

#include "log.h"
#include "settings.h"

namespace ArcadeSound {
namespace {

constexpr int RATE = 44100;
constexpr int BLOCK = 441;          // 10 ms
constexpr int MAX_VOICES = 12;

struct Clip {
    std::vector<int16_t> samples;   // mono, RATE
};

struct Voice {
    std::shared_ptr<const Clip> clip;
    size_t pos = 0;
    float gain = 1.0f;
    std::string game;
};

uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

// A 16-bit PCM WAV, mixed down to mono and resampled to RATE if need be.
std::shared_ptr<Clip> loadWav(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return nullptr;
    }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4)) {
        LogWarn(VB_PLUGIN, "Arcade sound: %s is not a WAV file\n", path.c_str());
        return nullptr;
    }
    int channels = 0, rate = 0, bits = 0, format = 0;
    const uint8_t *data = nullptr;
    size_t dataLen = 0;
    for (size_t p = 12; p + 8 <= d.size();) {
        const uint32_t len = rd32(&d[p + 4]);
        const uint8_t *body = &d[p + 8];
        if (p + 8 + len > d.size()) {
            break;
        }
        if (!memcmp(&d[p], "fmt ", 4) && len >= 16) {
            format = rd16(body);
            channels = rd16(body + 2);
            rate = (int)rd32(body + 4);
            bits = rd16(body + 14);
        } else if (!memcmp(&d[p], "data", 4)) {
            data = body;
            dataLen = len;
        }
        p += 8 + len + (len & 1);
    }
    if (format != 1 || bits != 16 || channels < 1 || channels > 2 || rate <= 0 || !data) {
        LogWarn(VB_PLUGIN, "Arcade sound: %s must be 16-bit PCM, mono or stereo\n", path.c_str());
        return nullptr;
    }
    const size_t frames = dataLen / (2 * channels);
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        int sum = 0;
        for (int c = 0; c < channels; ++c) {
            sum += (int16_t)rd16(data + (i * channels + c) * 2);
        }
        mono[i] = (float)sum / channels;
    }
    auto clip = std::make_shared<Clip>();
    const size_t outFrames = rate == RATE ? frames : (size_t)((double)frames * RATE / rate);
    clip->samples.resize(outFrames);
    for (size_t i = 0; i < outFrames; ++i) {
        const double src = rate == RATE ? (double)i : (double)i * rate / RATE;
        const size_t a = std::min(frames - 1, (size_t)src);
        const size_t b = std::min(frames - 1, a + 1);
        const float t = (float)(src - a);
        clip->samples[i] = (int16_t)std::lround(mono[a] * (1 - t) + mono[b] * t);
    }
    return clip;
}

class Engine {
public:
    ~Engine() { stop(); }

    void configure(const Json::Value &cfg) {
        stop();
        {
            std::lock_guard<std::mutex> l(mu);
            enabled = cfg.isObject() && cfg.get("enabled", false).asBool();
            device = cfg.isObject() ? cfg.get("device", "auto").asString() : "auto";
            volume = std::clamp(cfg.isObject() ? cfg.get("volume", 80).asFloat() : 80.0f, 0.0f, 100.0f) / 100.0f;
            streamHost = cfg.isObject() ? cfg.get("streamHost", "").asString() : "";
            streamPort = cfg.isObject() ? cfg.get("streamPort", 5570).asInt() : 5570;
            cache.clear();   // pick up sound files added since
            voices.clear();
        }
        if (enabled) {
            running = true;
            th = std::thread([this]() { mixLoop(); });
            LogInfo(VB_PLUGIN, "Arcade sound: on, device %s, volume %d%%%s%s\n", device.c_str(), (int)(volume * 100),
                    streamHost.empty() ? "" : ", streaming to ", streamHost.c_str());
        }
    }

    void play(const std::string &game, const std::string &name, float gain) {
        if (!enabled) {
            return;
        }
        std::lock_guard<std::mutex> l(mu);
        const std::string key = game + "/" + name;
        auto it = cache.find(key);
        if (it == cache.end()) {
            // Loaded on first use and kept; a missing file is remembered too,
            // so a game without sounds doesn't look on disk every event.
            it = cache.emplace(key, loadWav(FPP_DIR_MEDIA("/plugindata/fpp-arcade/sounds/" + key + ".wav"))).first;
        }
        if (!it->second) {
            return;
        }
        if (voices.size() >= MAX_VOICES) {
            voices.erase(voices.begin());   // the oldest makes way
        }
        voices.push_back({it->second, 0, gain, game});
    }

    void stopAll(const std::string &game) {
        std::lock_guard<std::mutex> l(mu);
        voices.erase(std::remove_if(voices.begin(), voices.end(),
                                    [&](const Voice &v) { return game.empty() || v.game == game; }),
                     voices.end());
    }

    void stop() {
        running = false;
        if (th.joinable()) {
            th.join();
        }
    }

private:
    // Mix 10 ms at a time. With a sound card, its blocking write keeps time;
    // without one the loop sleeps to the clock, so streaming alone still runs
    // at the right rate.
    void mixLoop() {
        openSocket();
#ifndef PLATFORM_OSX
        openCard();
        auto nextCardTry = std::chrono::steady_clock::now() + std::chrono::seconds(5);
#endif
        std::vector<float> mix(BLOCK);
        std::vector<int16_t> mono(BLOCK), stereo(BLOCK * 2);
        auto next = std::chrono::steady_clock::now();
        int quietBlocks = 1000;
        uint32_t seq = 0;
        while (running) {
            std::fill(mix.begin(), mix.end(), 0.0f);
            bool active = false;
            {
                std::lock_guard<std::mutex> l(mu);
                for (auto &v : voices) {
                    const auto &s = v.clip->samples;
                    const size_t n = std::min<size_t>(BLOCK, s.size() - v.pos);
                    for (size_t i = 0; i < n; ++i) {
                        mix[i] += s[v.pos + i] * v.gain;
                    }
                    v.pos += n;
                    active = true;
                }
                voices.erase(std::remove_if(voices.begin(), voices.end(),
                                            [](const Voice &v) { return v.pos >= v.clip->samples.size(); }),
                             voices.end());
            }
            for (int i = 0; i < BLOCK; ++i) {
                const int v = (int)std::lround(mix[i] * volume);
                mono[i] = (int16_t)std::clamp(v, -32768, 32767);
                stereo[2 * i] = stereo[2 * i + 1] = mono[i];
            }
            quietBlocks = active ? 0 : quietBlocks + 1;
            // Stream while anything plays and for a moment after, so the
            // listener hears each clip out; nothing is sent when it's quiet.
            if (sock >= 0 && quietBlocks < 20) {
                sendBlock(seq++, mono);
            }
            bool paced = false;
#ifndef PLATFORM_OSX
            if (pcm) {
                snd_pcm_sframes_t w = snd_pcm_writei(pcm, stereo.data(), BLOCK);
                if (w == -EPIPE) {
                    snd_pcm_prepare(pcm);   // an underrun: carry on
                } else if (w < 0) {
                    LogWarn(VB_PLUGIN, "Arcade sound: sound card write failed (%s), closing it\n", snd_strerror((int)w));
                    snd_pcm_close(pcm);
                    pcm = nullptr;
                } else {
                    paced = true;
                }
            } else if (std::chrono::steady_clock::now() > nextCardTry) {
                openCard();   // plugged in later, or back after a hiccup
                nextCardTry = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            }
#endif
            if (!paced) {
                next += std::chrono::milliseconds(10);
                const auto now = std::chrono::steady_clock::now();
                if (next < now - std::chrono::milliseconds(100)) {
                    next = now;   // fell behind (a stall): don't race to catch up
                }
                std::this_thread::sleep_until(next);
            } else {
                next = std::chrono::steady_clock::now();
            }
        }
#ifndef PLATFORM_OSX
        if (pcm) {
            snd_pcm_drop(pcm);
            snd_pcm_close(pcm);
            pcm = nullptr;
        }
#endif
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
    }

#ifndef PLATFORM_OSX
    // "auto" is the first sound card that isn't ALSA's Dummy - a USB sound
    // card when one is plugged in. "none" streams only.
    void openCard() {
        std::string name = device;
        if (name == "none" || name.empty()) {
            return;
        }
        if (name == "auto") {
            name.clear();
            int card = -1;
            while (snd_card_next(&card) == 0 && card >= 0) {
                char *cardName = nullptr;
                if (snd_card_get_name(card, &cardName) == 0) {
                    const bool dummy = cardName && strstr(cardName, "Dummy");
                    free(cardName);
                    if (!dummy) {
                        name = "plughw:" + std::to_string(card) + ",0";
                        break;
                    }
                }
            }
            if (name.empty()) {
                return;   // no card yet
            }
        }
        if (snd_pcm_open(&pcm, name.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
            pcm = nullptr;
            return;
        }
        // 60 ms of buffer: short enough that a bounce is heard with the hit.
        if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, RATE, 1, 60000) < 0) {
            LogWarn(VB_PLUGIN, "Arcade sound: %s won't play 44.1 kHz stereo\n", name.c_str());
            snd_pcm_close(pcm);
            pcm = nullptr;
            return;
        }
        LogInfo(VB_PLUGIN, "Arcade sound: playing on %s\n", name.c_str());
    }
#endif

    void openSocket() {
        if (streamHost.empty()) {
            return;
        }
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(streamHost.c_str(), std::to_string(streamPort).c_str(), &hints, &res) != 0 || !res) {
            LogWarn(VB_PLUGIN, "Arcade sound: can't find %s to stream to\n", streamHost.c_str());
            return;
        }
        memcpy(&dest, res->ai_addr, sizeof(dest));
        freeaddrinfo(res);
        sock = socket(AF_INET, SOCK_DGRAM, 0);
    }

    void sendBlock(uint32_t seq, const std::vector<int16_t> &mono) {
        uint8_t pkt[16 + BLOCK * 2];
        memcpy(pkt, "FAS1", 4);
        const uint32_t r = RATE;
        const uint16_t frames = BLOCK, ch = 1;
        memcpy(pkt + 4, &seq, 4);   // little-endian hosts throughout
        memcpy(pkt + 8, &r, 4);
        memcpy(pkt + 12, &frames, 2);
        memcpy(pkt + 14, &ch, 2);
        memcpy(pkt + 16, mono.data(), BLOCK * 2);
        sendto(sock, pkt, sizeof(pkt), 0, (const sockaddr *)&dest, sizeof(dest));
    }

    std::mutex mu;
    std::map<std::string, std::shared_ptr<const Clip>> cache;
    std::vector<Voice> voices;
    std::atomic<bool> running{false};
    std::thread th;
    std::atomic<bool> enabled{false};
    std::string device = "auto";
    float volume = 0.8f;
    std::string streamHost;
    int streamPort = 5570;
    int sock = -1;
    sockaddr_in dest{};
#ifndef PLATFORM_OSX
    snd_pcm_t *pcm = nullptr;
#endif
};

Engine &engine() {
    static Engine e;
    return e;
}

}  // namespace

void configure(const Json::Value &cfg) { engine().configure(cfg); }
void play(const std::string &game, const std::string &name, float gain) { engine().play(game, name, gain); }
void stopAll(const std::string &game) { engine().stopAll(game); }
void shutdown() { engine().stop(); }

std::string folderFor(const std::string &gameName) {
    std::string s;
    for (char ch : gameName) {
        if (ch != ' ') {
            s += (char)std::tolower((unsigned char)ch);
        }
    }
    return s;
}

}  // namespace ArcadeSound
