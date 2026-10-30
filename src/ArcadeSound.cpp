#include <fpp-pch.h>

#include "ArcadeSound.h"
#include "ArcadeDemo.h"
#include "ShowTheme.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <map>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <poll.h>
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
constexpr int SPEAKER_PORT = 5571;   // controller speakers say hello here
constexpr int SPEAKER_TIMEOUT_S = 15;

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

// Sounds put on the player win over the ones that come with the plugin, so
// any bundled sound can be replaced by dropping a file of the same name in.
std::string soundsDir() { return FPP_DIR_MEDIA("/plugindata/fpp-arcade/sounds"); }
std::string bundledDir() { return FPP_DIR_MEDIA("/plugins/fpp-arcade/sounds"); }
std::string soundFile(const std::string &key) {
    const std::string own = soundsDir() + "/" + key + ".wav";
    struct stat st{};
    return stat(own.c_str(), &st) == 0 ? own : bundledDir() + "/" + key + ".wav";
}

// Only letters, digits, '_', '-' and one '/': a sound path from a request
// can't reach outside the sounds folder.
bool safePath(const std::string &p) {
    int slashes = 0;
    for (char c : p) {
        if (c == '/') {
            ++slashes;
        } else if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            return false;
        }
    }
    return slashes == 1 && p.front() != '/' && p.back() != '/';
}

std::string jsonEscape(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
        }
        if ((unsigned char)c >= 0x20) {
            o += c;
        }
    }
    return o;
}

struct Speaker {
    sockaddr_in addr{};
    std::string model, name, firmware;
    int volume = 0;
    std::chrono::steady_clock::time_point seen;
};

class Engine {
public:
    ~Engine() {
        stop();
        stopListener();
    }

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
        startListener();
        sendToSpeakers("SYNC", "");   // files may have changed with the settings
        if (enabled) {
            running = true;
            th = std::thread([this]() { mixLoop(); });
            LogInfo(VB_PLUGIN, "Arcade sound: on, device %s, volume %d%%%s%s\n", device.c_str(), (int)(volume * 100),
                    streamHost.empty() ? "" : ", streaming to ", streamHost.c_str());
        }
    }

    void play(const std::string &game, const std::string &name, float gain, const std::string &model) {
        if (!enabled) {
            return;
        }
        {
            // Stamped with this player's clock, so a speaker can tell a sound
            // that sat in the Wi-Fi from one that didn't (see ArcadeSound.h).
            const auto ms = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
            char msg[256];
            snprintf(msg, sizeof(msg), "PLAY %u %s/%s %.3f %u", ++eventSeq, game.c_str(), name.c_str(),
                     std::clamp(gain, 0.0f, 4.0f) * volume, ms);
            sendToSpeakers(msg, model);
        }
        std::lock_guard<std::mutex> l(mu);
        auto clip = clipFor(game, name);
        if (!clip) {
            return;
        }
        if (voices.size() >= MAX_VOICES) {
            voices.erase(voices.begin());   // the oldest makes way
        }
        voices.push_back({clip, 0, gain, game});
    }

    int lengthMs(const std::string &game, const std::string &name) {
        if (!enabled) {
            return 0;
        }
        std::lock_guard<std::mutex> l(mu);
        auto clip = clipFor(game, name);
        return clip ? (int)(clip->samples.size() * 1000 / RATE) : 0;
    }

    // Call with mu held.
    std::shared_ptr<const Clip> clipFor(const std::string &game, const std::string &name) {
        const std::string key = game + "/" + name;
        auto it = cache.find(key);
        if (it == cache.end()) {
            // Loaded on first use and kept; a missing file is remembered too,
            // so a game without sounds doesn't look on disk every event.
            it = cache.emplace(key, loadWav(soundFile(key))).first;
        }
        return it->second;
    }

    void stopAll(const std::string &game, const std::string &model) {
        sendToSpeakers("STOP " + (game.empty() ? std::string("*") : game), model);
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

    // ------------------------------------------------ controller speakers
    void startListener() {
        if (listening) {
            return;
        }
        const int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0) {
            return;
        }
        int yes = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        timeval tv{0, 500000};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(SPEAKER_PORT);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, (sockaddr *)&a, sizeof(a)) != 0) {
            LogWarn(VB_PLUGIN, "Arcade sound: can't listen for speakers on UDP %d\n", SPEAKER_PORT);
            close(s);
            return;
        }
        eventSock = s;
        listening = true;
        listener = std::thread([this]() { listenLoop(); });
    }

    void stopListener() {
        listening = false;
        if (listener.joinable()) {
            listener.join();
        }
        if (eventSock >= 0) {
            close(eventSock);
            eventSock = -1;
        }
    }

    // Hellos from speakers: who they are and which model they play for.
    void listenLoop() {
        char buf[512];
        while (listening) {
            sockaddr_in from{};
            socklen_t len = sizeof(from);
            const ssize_t n = recvfrom(eventSock, buf, sizeof(buf) - 1, 0, (sockaddr *)&from, &len);
            if (n <= 0) {
                continue;
            }
            buf[n] = 0;
            if (strncmp(buf, "FAS-HELLO 1 ", 12) != 0) {
                continue;
            }
            std::vector<std::string> f;
            std::stringstream ss(buf + 12);
            std::string part;
            while (std::getline(ss, part, '|')) {
                f.push_back(part);
            }
            Speaker sp;
            sp.addr = from;
            sp.model = f.size() > 0 ? f[0] : "";
            sp.name = f.size() > 1 ? f[1] : "";
            sp.firmware = f.size() > 2 ? f[2] : "";
            sp.volume = f.size() > 3 ? atoi(f[3].c_str()) : 0;
            sp.seen = std::chrono::steady_clock::now();
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            const std::string key = std::string(ip) + ":" + std::to_string(ntohs(from.sin_port));
            std::lock_guard<std::mutex> l(speakerMu);
            if (!speakers.count(key)) {
                LogInfo(VB_PLUGIN, "Arcade sound: speaker %s (%s) at %s for %s\n", sp.name.c_str(), sp.firmware.c_str(),
                        key.c_str(), sp.model.c_str());
            }
            speakers[key] = sp;
        }
    }

    // To every speaker for `model` (or every speaker), straight away: a
    // datagram per speaker, no waiting on the mixer.
    void sendToSpeakers(const std::string &msg, const std::string &model) {
        if (eventSock < 0) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> l(speakerMu);
        for (auto it = speakers.begin(); it != speakers.end();) {
            if (now - it->second.seen > std::chrono::seconds(SPEAKER_TIMEOUT_S)) {
                it = speakers.erase(it);   // gone quiet: switched off or out of range
                continue;
            }
            if (model.empty() || it->second.model == model) {
                sendto(eventSock, msg.data(), msg.size(), 0, (const sockaddr *)&it->second.addr, sizeof(sockaddr_in));
            }
            ++it;
        }
    }

    // ip:port keys for the speakers still heard from whose name or model is
    // `who` ("" or "All": every one).
    std::vector<std::string> speakerIps(const std::string &who) {
        const auto now = std::chrono::steady_clock::now();
        std::vector<std::string> out;
        std::lock_guard<std::mutex> l(speakerMu);
        for (auto &[key, sp] : speakers) {
            if (now - sp.seen > std::chrono::seconds(SPEAKER_TIMEOUT_S)) {
                continue;
            }
            if (who.empty() || who == "All" || sp.name == who || sp.model == who) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sp.addr.sin_addr, ip, sizeof(ip));
                if (std::find(out.begin(), out.end(), ip) == out.end()) {
                    out.push_back(ip);
                }
            }
        }
        return out;
    }

    std::string speakersJson() {
        const auto now = std::chrono::steady_clock::now();
        std::string o = "[";
        std::lock_guard<std::mutex> l(speakerMu);
        for (auto &[key, sp] : speakers) {
            const long ago = (long)std::chrono::duration_cast<std::chrono::seconds>(now - sp.seen).count();
            if (ago > SPEAKER_TIMEOUT_S) {
                continue;
            }
            if (o.size() > 1) {
                o += ",";
            }
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sp.addr.sin_addr, ip, sizeof(ip));
            o += "{\"ip\":\"" + std::string(ip) + "\",\"model\":\"" + jsonEscape(sp.model) + "\",\"name\":\"" +
                 jsonEscape(sp.name) + "\",\"firmware\":\"" + jsonEscape(sp.firmware) + "\",\"volume\":" +
                 std::to_string(sp.volume) + ",\"secondsAgo\":" + std::to_string(ago) + "}";
        }
        return o + "]";
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

    std::mutex speakerMu;
    std::map<std::string, Speaker> speakers;
    std::atomic<bool> listening{false};
    std::thread listener;
    int eventSock = -1;
    std::atomic<uint32_t> eventSeq{0};

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
void play(const std::string &game, const std::string &name, float gain, const std::string &model) {
    ArcadeDemo::noteSound(model, false, game, name, gain);   // for a demo being recorded there
    engine().play(game, name, gain, model);
}
int lengthMs(const std::string &game, const std::string &name) { return engine().lengthMs(game, name); }
void stopAll(const std::string &game, const std::string &model) {
    ArcadeDemo::noteSound(model, true, game, "", 0);
    engine().stopAll(game, model);
}
void shutdown() {
    engine().stop();
    engine().stopListener();
}
std::string speakersJson() { return engine().speakersJson(); }
std::vector<std::string> speakerIps(const std::string &who) { return engine().speakerIps(who); }

namespace {
// Waits up to ms for fd to be ready for `events`.
bool waitFor(int fd, short events, int ms) {
    pollfd p{fd, events, 0};
    return poll(&p, 1, ms) == 1;
}
}  // namespace

bool speakerRequest(const std::string &ip, const std::string &pathQuery, const std::string &form, int &status,
                    std::string &body) {
    status = 0;
    body.clear();
    const std::vector<std::string> known = speakerIps("");
    if (std::find(known.begin(), known.end(), ip) == known.end() || pathQuery.empty() || pathQuery[0] != '/' ||
        pathQuery.find_first_of("\r\n ") != std::string::npos) {
        return false;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(80);
    if (inet_pton(AF_INET, ip.c_str(), &to.sin_addr) != 1) {
        return false;
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    // Short timeouts: this runs on FPP's web or command thread, and a speaker
    // that's off shouldn't hold it up. HTTP/1.0, so the reply is the plain body
    // and ends when the speaker closes the connection.
    bool ok = false;
    if (connect(fd, (const sockaddr *)&to, sizeof(to)) == 0 || (errno == EINPROGRESS && waitFor(fd, POLLOUT, 1500))) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (!err) {
            std::string req = (form.empty() ? "GET " : "POST ") + pathQuery + " HTTP/1.0\r\nHost: " + ip +
                              "\r\nConnection: close\r\n";
            if (!form.empty()) {
                req += "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " + std::to_string(form.size()) +
                       "\r\n";
            }
            req += "\r\n" + form;
            size_t sent = 0;
            while (sent < req.size() && waitFor(fd, POLLOUT, 1500)) {
                const ssize_t n = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
                if (n <= 0) {
                    break;
                }
                sent += (size_t)n;
            }
            std::string reply;
            char buf[2048];
            while (sent == req.size() && reply.size() < 65536 && waitFor(fd, POLLIN, 2500)) {
                const ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    ok = n == 0;   // closed: the whole reply is in
                    break;
                }
                reply.append(buf, (size_t)n);
                // Done once Content-Length's worth is in, closed or not.
                const size_t h = reply.find("\r\n\r\n");
                const size_t cl = h == std::string::npos ? std::string::npos : reply.find("Content-Length:");
                if (cl != std::string::npos && cl < h && reply.size() - (h + 4) >= (size_t)atol(reply.c_str() + cl + 15)) {
                    ok = true;
                    break;
                }
            }
            const size_t head = reply.find("\r\n\r\n");
            if (ok && head != std::string::npos && reply.compare(0, 5, "HTTP/") == 0) {
                const size_t sp = reply.find(' ');
                status = sp != std::string::npos ? atoi(reply.c_str() + sp + 1) : 0;
                body = reply.substr(head + 4);
            } else {
                ok = false;
            }
        }
    }
    close(fd);
    return ok;
}

// Every <game>/<name>.wav, the player's own and the bundled ones (the
// player's winning), with a version (where it's from, size and modification
// time) a speaker compares to decide what to fetch again.
std::string soundListJson() {
    std::map<std::string, std::string> found;
    for (const std::string &root : {bundledDir(), soundsDir()}) {   // own last, so it wins
        const char tag = root == soundsDir() ? 'u' : 'b';
        DIR *games = opendir(root.c_str());
        if (!games) {
            continue;
        }
        while (dirent *g = readdir(games)) {
            if (g->d_name[0] == '.') {
                continue;
            }
            const std::string gdir = root + "/" + g->d_name;
            DIR *files = opendir(gdir.c_str());
            if (!files) {
                continue;
            }
            while (dirent *f = readdir(files)) {
                std::string n = f->d_name;
                if (n.size() < 5 || n.compare(n.size() - 4, 4, ".wav") != 0) {
                    continue;
                }
                const std::string path = std::string(g->d_name) + "/" + n.substr(0, n.size() - 4);
                struct stat st{};
                if (!safePath(path) || stat((gdir + "/" + n).c_str(), &st) != 0) {
                    continue;
                }
                found[path] = std::string(1, tag) + std::to_string((long long)st.st_size) + "-" +
                              std::to_string((long long)st.st_mtime);
            }
            closedir(files);
        }
        closedir(games);
    }
    // Of each game's end-show music, only the one the theme plays: a speaker
    // has room for one set (Christmas, Halloween or Everyday), not all three.
    // It fetches the other when the theme changes.
    const std::string themed = showThemeSound(arcadeShowTheme());
    for (auto it = found.begin(); it != found.end();) {
        const size_t slash = it->first.find('/');
        const std::string game = it->first.substr(0, slash), name = it->first.substr(slash + 1);
        const bool endShow = name == "EndShow" || name == "EndShowHalloween" || name == "EndShowEveryday";
        const std::string wanted = found.count(game + "/" + themed) ? themed : "EndShow";
        it = endShow && name != wanted ? found.erase(it) : std::next(it);
    }
    std::string o = "{\"files\":[";
    bool first = true;
    for (auto &[path, version] : found) {
        o += first ? "" : ",";
        first = false;
        o += "{\"path\":\"" + path + "\",\"version\":\"" + version + "\"}";
    }
    return o + "]}";
}

bool soundPcm(const std::string &path, int rate, std::string &out) {
    if (!safePath(path) || rate < 8000 || rate > RATE) {
        return false;
    }
    auto clip = loadWav(soundFile(path));
    if (!clip) {
        return false;
    }
    const auto &s = clip->samples;
    const size_t frames = (size_t)((double)s.size() * rate / RATE);
    out.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        // Average the samples this output one covers, a simple low-pass so
        // halving the rate doesn't alias.
        const size_t a = (size_t)((double)i * RATE / rate), b = std::max(a + 1, (size_t)((double)(i + 1) * RATE / rate));
        long sum = 0;
        size_t n = 0;
        for (size_t k = a; k < b && k < s.size(); ++k, ++n) {
            sum += s[k];
        }
        const int16_t v = (int16_t)(n ? sum / (long)n : 0);
        memcpy(&out[i * 2], &v, 2);
    }
    return true;
}

// G.711 mu-law: 8 bits a sample, about 38 dB of signal to noise at any level.
// (4-bit ADPCM would be half the size again, but the games' square waves and
// noise come out of it at 10-13 dB - an audible buzz.)
static uint8_t ulawByte(int s) {
    const int BIAS = 0x84, CLIP = 32635;
    const int sign = s < 0 ? 0x80 : 0;
    s = std::min(sign ? -s : s, CLIP) + BIAS;
    int exp = 7;
    for (int mask = 0x4000; !(s & mask) && exp > 0; --exp, mask >>= 1) {
    }
    return (uint8_t)~(sign | (exp << 4) | ((s >> (exp + 3)) & 0x0F));
}

bool soundUlaw(const std::string &path, int rate, std::string &out) {
    std::string pcm;
    if (!soundPcm(path, rate, pcm)) {
        return false;
    }
    const size_t frames = pcm.size() / 2;
    out.assign("ULW1");
    out.resize(4 + frames);
    for (size_t i = 0; i < frames; ++i) {
        int16_t v;
        memcpy(&v, &pcm[i * 2], 2);
        out[4 + i] = (char)ulawByte(v);
    }
    return true;
}

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
