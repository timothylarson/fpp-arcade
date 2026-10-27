// Show audio: the music of FPP's show (a playlist's media, or a sequence's),
// played in step with it, when "Play show audio" is on.
//
// FPP's MultiSync packets (UDP 32320; multicast 239.70.80.80 or broadcast)
// say which media file is playing and how far in, every half second. The
// speaker downloads that file from the FPP that sent them
// (api/file/Music/<name>), decodes the MP3 itself (the Helix decoder that
// comes with the ESP32 core) and keeps to FPP's position: it skips or waits
// when it's out by more than 40 ms, and otherwise plays a touch faster or
// slower until it's back within 2 ms.
//
// Joining a song part-way (the speaker started late, or missed the start)
// streams the file from the top and skips whole MP3 frames by their headers,
// without decoding them, so it's quick and exact even for variable bitrate.
#pragma once

#include <HTTPClient.h>
#include <mp3dec.h>

#include <atomic>
#include <cmath>

constexpr uint16_t MULTISYNC_PORT = 32320;
constexpr int SHOW_RING = RATE * 8;        // 8 s of decoded music, in PSRAM
constexpr int OUTPUT_LATENCY_MS = 40;      // from mixing a block to hearing it (I2S DMA + a block)
constexpr uint32_t SHOW_TIMEOUT_MS = 8000; // no word from FPP for this long: the show stopped (a Wi-Fi
                                           // blip is shorter; a real stop sends a stop packet)

// What FPP says is playing. Written by the loop (MultiSync packets), read by
// the show task (which file to fetch) and the audio task (where to be).
portMUX_TYPE showMux = portMUX_INITIALIZER_UNLOCKED;
char showFile[160] = "";               // media file name, as FPP gives it
IPAddress showFrom;                    // the FPP that sent it: the file comes from there
volatile uint32_t showGen = 0;         // changes with every new file or stop
volatile bool showStarted = false;     // FPP is playing it (not just opened)
volatile uint32_t showLastHeard = 0;   // millis() of the last packet about it
char showStatusText[200] = "Off";      // for the page; set from two tasks, so under showMux

void setShowStatus(const String &s) {
    portENTER_CRITICAL(&showMux);
    strlcpy(showStatusText, s.c_str(), sizeof(showStatusText));
    portEXIT_CRITICAL(&showMux);
}

String showStatus() {
    char s[sizeof(showStatusText)];
    portENTER_CRITICAL(&showMux);
    strlcpy(s, showStatusText, sizeof(s));
    portEXIT_CRITICAL(&showMux);
    return s;
}

// FPP's position against our clock: fppMs - millis() for each sync packet.
// A packet the Wi-Fi held up gives a smaller value, so the largest lately is
// the truest; lately is these 4 s and the 4 before, so drift is followed.
double clockBest[2] = {-1e18, -1e18};
uint32_t clockSince = 0;
volatile bool clockValid = false;

void clockReset() {
    clockBest[0] = clockBest[1] = -1e18;
    clockValid = false;
}

void clockSample(double fppMs) {
    const uint32_t now = millis();
    const double off = fppMs - now;
    portENTER_CRITICAL(&showMux);
    const double best = std::max(clockBest[0], clockBest[1]);
    if (!clockValid || std::fabs(off - best) > 1000) {
        clockBest[0] = clockBest[1] = off;   // first word, or FPP jumped (a seek, a new song)
        clockSince = now;
        clockValid = true;
    } else {
        if (now - clockSince > 4000) {
            clockBest[1] = clockBest[0];
            clockBest[0] = -1e18;
            clockSince = now;
        }
        clockBest[0] = std::max(clockBest[0], off);
    }
    portEXIT_CRITICAL(&showMux);
}

// The song position, in samples at RATE, that should be heard at atMs, or
// -1 before FPP's clock is known.
int64_t showTargetIdx(uint32_t atMs) {
    if (!clockValid || !showStarted) {
        return -1;
    }
    portENTER_CRITICAL(&showMux);
    const double best = std::max(clockBest[0], clockBest[1]);
    portEXIT_CRITICAL(&showMux);
    return (int64_t)std::floor(((double)atMs + best) * RATE / 1000.0);
}

// ------------------------------------------------------------------ ring

// Decoded music waiting to play: one writer (the show task), one reader (the
// audio task). ringFirst is the song position of sample 0 of the counters.
int16_t *showRing = nullptr;
std::atomic<uint32_t> ringW{0}, ringR{0};
volatile int64_t ringFirst = 0;
volatile bool ringValid = false;
volatile int32_t showErrMs = 0;   // how far out it was, last block

void ringRestart(int64_t songIdx) {
    portENTER_CRITICAL(&showMux);
    ringR = 0;
    ringW = 0;
    ringFirst = songIdx;
    ringValid = true;
    portEXIT_CRITICAL(&showMux);
}

void ringStop() {
    portENTER_CRITICAL(&showMux);
    ringValid = false;
    ringR = ringW.load();
    portEXIT_CRITICAL(&showMux);
}

// Fills out[BLOCK] with the music for the block about to be mixed and says
// whether there was any. Called by the audio task.
bool showMix(int16_t *out) {
    if (!showRing || !cfg.show) {
        return false;
    }
    const int64_t target = showTargetIdx(millis() + OUTPUT_LATENCY_MS - cfg.showOffset);
    if (target < 0) {
        return false;
    }
    bool played = false;
    portENTER_CRITICAL(&showMux);
    const uint32_t r = ringR, w = ringW;
    if (ringValid && w != r) {
        const int64_t err = ringFirst + r - target;   // > 0: ahead of FPP
        constexpr int64_t JUMP = RATE * 40 / 1000, FAR = RATE * 15 / 1000, NEAR = RATE * 2 / 1000;
        showErrMs = (int32_t)(err * 1000 / RATE);
        if (err < -JUMP) {
            ringR = r + (uint32_t)std::min<int64_t>(w - r, -err);   // behind: skip to it
        } else if (err <= JUMP) {                                    // (ahead: wait for it)
            // Close: one or two samples more or fewer per block (0.4 or 0.8%)
            // until within 2 ms - too little to hear as a change of pitch.
            const int n = BLOCK - (err > FAR ? 2 : err > NEAR ? 1 : err < -FAR ? -2 : err < -NEAR ? -1 : 0);
            if (w - r > (uint32_t)n) {
                for (int i = 0; i < BLOCK; ++i) {
                    const uint32_t pos = (uint32_t)i * n * 256 / BLOCK;   // in 1/256ths of a sample
                    const int32_t a = showRing[(r + (pos >> 8)) % SHOW_RING];
                    const int32_t b = showRing[(r + (pos >> 8) + 1) % SHOW_RING];
                    out[i] = (int16_t)(a + (((b - a) * (int32_t)(pos & 255)) >> 8));
                }
                ringR = r + n;
                played = true;
            }
        }
    }
    portEXIT_CRITICAL(&showMux);
    return played;
}

// ------------------------------------------------------------------ MultiSync

void showStop() {
    portENTER_CRITICAL(&showMux);
    showFile[0] = 0;
    showStarted = false;
    ++showGen;
    portEXIT_CRITICAL(&showMux);
    clockReset();
    ringStop();
}

void showOpen(const char *name, IPAddress from) {
    portENTER_CRITICAL(&showMux);
    strlcpy(showFile, name, sizeof(showFile));
    showFrom = from;
    showStarted = false;
    ++showGen;
    portEXIT_CRITICAL(&showMux);
    clockReset();
    ringStop();
}

// A MultiSync packet: "FPPD", type 1 (sync), length; then the sync packet:
// action, file type (1 = media), frame, seconds (float), file name.
void handleMultiSync(const uint8_t *p, int n, IPAddress from) {
    if (n < 7 + 10 + 1 || memcmp(p, "FPPD", 4) || p[4] != 1) {
        return;
    }
    const uint8_t *s = p + 7;
    const uint8_t action = s[0], fileType = s[1];
    if (fileType != 1) {
        return;   // sequences: the lights' business
    }
    float seconds;
    memcpy(&seconds, s + 6, 4);
    char name[sizeof(showFile)];
    const int len = std::min<int>(n - 17, sizeof(name) - 1);
    memcpy(name, s + 10, len);
    name[len] = 0;
    const bool same = !strcmp(name, showFile);
    switch (action) {
        case 3:   // open: get it ready
            if (!same) {
                showOpen(name, from);
            }
            break;
        case 0:   // start
            if (!same) {
                showOpen(name, from);
            }
            showStarted = true;
            clockSample(0);
            break;
        case 2:   // sync: where FPP is
            if (!same) {
                showOpen(name, from);
            }
            showStarted = true;
            clockSample(seconds * 1000.0);
            break;
        case 1:   // stop
            if (same) {
                showStop();
            }
            break;
    }
    if (showFile[0]) {
        showLastHeard = millis();
    }
}

// ------------------------------------------------------------------ the show task

String urlEncode(const char *s) {
    String o;
    for (; *s; ++s) {
        const uint8_t c = *s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += (char)c;
        } else {
            char h[4];
            snprintf(h, sizeof(h), "%%%02X", c);
            o += h;
        }
    }
    return o;
}

// An MPEG audio layer III frame header: its length in bytes, and its samples
// per channel and sample rate; -1 if it isn't one.
int mp3FrameBytes(const uint8_t *h, int &spf, int &sr) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) {
        return -1;
    }
    const int ver = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3;   // ver 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5
    const int bri = h[2] >> 4, sri = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) {
        return -1;
    }
    static const int16_t KBPS1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const int16_t KBPS2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const int32_t RATES[] = {44100, 48000, 32000};
    sr = RATES[sri] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
    spf = ver == 3 ? 1152 : 576;
    return (ver == 3 ? 144 : 72) * (ver == 3 ? KBPS1 : KBPS2)[bri] * 1000 / sr + pad;
}

// Fetch one file and decode it into the ring, as long as it's still the one
// wanted (gen). Returns false if it couldn't be fetched.
bool showStream(uint32_t gen, const char *name, IPAddress from, HMP3Decoder dec) {
    HTTPClient http;
    http.setTimeout(5000);
    http.begin("http://" + from.toString() + "/api/file/Music/" + urlEncode(name));
    const int code = http.GET();
    if (code != 200) {
        setShowStatus("Couldn't fetch " + String(name) + " from " + from.toString() + " (" + String(code) + ")");
        http.end();
        return false;
    }
    NetworkClient *in = http.getStreamPtr();
    static uint8_t buf[8192];
    static int16_t pcm[MAX_NSAMP * MAX_NGRAN * MAX_NCHAN];
    int have = 0;
    double songIdx = 0;         // song position of the next sample, at RATE
    bool restart = true;        // the ring starts again at songIdx with the next sample
    bool id3Checked = false;
    uint32_t id3Skip = 0;
    uint32_t got = 0, since = millis(), told = 0;   // bytes fetched, for the catch-up line
    double rsT = 0;             // resampler: where the next output falls, from prev (0) to cur (1)
    int32_t rsPrev = 0, rsLast = 0;
    setShowStatus("Playing " + String(name));

    auto more = [&]() -> bool {   // false at the end of the file
        while (have < (int)sizeof(buf)) {
            const int a = in->available();
            if (a > 0) {
                const int n = in->read(buf + have, std::min<int>(a, sizeof(buf) - have));
                have += std::max(0, n);
                got += std::max(0, n);
                continue;
            }
            if (!http.connected()) {
                return have > 0;
            }
            if (have >= 2048) {
                return true;
            }
            vTaskDelay(2);
            if (showGen != gen) {
                return false;
            }
        }
        return true;
    };
    // One sample into the ring, waiting while it's full; false if FPP moved on.
    auto push = [&](int16_t v) -> bool {
        while (ringW - ringR >= (uint32_t)SHOW_RING - 1) {   // full: the audio task will make room
            if (showGen != gen) {
                return false;
            }
            vTaskDelay(5);
        }
        showRing[ringW % SHOW_RING] = v;
        ++ringW;
        return true;
    };
    auto atEnd = [&]() { return !http.connected() && in->available() <= 0; };
    auto drop = [&](int n) {
        memmove(buf, buf + n, have - n);
        have -= n;
    };

    while (showGen == gen && cfg.show) {
        if (!more() && have == 0) {
            break;
        }
        // An ID3v2 tag (often with cover art) before the first frame.
        if (!id3Checked && have >= 10) {
            id3Checked = true;
            if (!memcmp(buf, "ID3", 3)) {
                id3Skip = 10 + ((buf[6] & 0x7f) << 21 | (buf[7] & 0x7f) << 14 | (buf[8] & 0x7f) << 7 | (buf[9] & 0x7f));
            }
        }
        if (id3Skip) {
            const int n = std::min<uint32_t>(id3Skip, have);
            drop(n);
            id3Skip -= n;
            continue;
        }
        const int at = MP3FindSyncWord(buf, have);
        if (at < 0) {
            if (atEnd()) {
                break;
            }
            drop(std::max(0, have - 3));
            continue;
        }
        drop(at);
        int spf, sr;
        const int bytes = have >= 4 ? mp3FrameBytes(buf, spf, sr) : -1;
        if (bytes < 0) {
            drop(1);
            continue;
        }
        if (have < bytes) {
            if (atEnd()) {
                break;   // a part frame at the very end
            }
            continue;    // the rest of it is still coming
        }
        const double frameIdx = (double)spf * RATE / sr;   // this frame's length, at RATE

        // Behind FPP by more than a second (joined part-way, or fell
        // behind): pass over whole frames without decoding them.
        const int64_t target = showTargetIdx(millis() + OUTPUT_LATENCY_MS);
        if (target >= 0 && target - (int64_t)songIdx > RATE) {
            drop(std::min(bytes, have));
            songIdx += frameIdx;
            restart = true;
            if (millis() - told > 500) {   // FPP serves files whole, so the speaker reads its way there
                told = millis();
                const auto mmss = [](int64_t idx) { char t[8]; snprintf(t, sizeof(t), "%d:%02d", (int)(idx / RATE / 60), (int)(idx / RATE % 60)); return String(t); };
                setShowStatus("Catching up with " + String(name) + ": " + mmss((int64_t)songIdx) + " of " + mmss(target) +
                              " (" + String(got / std::max<uint32_t>(1, millis() - since)) + " KB/s)");
            }
            continue;
        }
        if (restart && told) {
            setShowStatus("Playing " + String(name));
            told = 0;
        }

        unsigned char *ptr = buf;
        int left = have;
        const int err = MP3Decode(dec, &ptr, &left, pcm, 0);
        drop(have - left);
        if (err == ERR_MP3_INDATA_UNDERFLOW) {
            if (atEnd()) {
                break;
            }
            continue;
        }
        if (err != ERR_MP3_NONE) {
            // A frame that won't decode (the first after a skip has no bit
            // reservoir to draw on) is a frame of silence, so what follows
            // still lands at the right time.
            if (err != ERR_MP3_MAINDATA_UNDERFLOW && left == have) {
                drop(1);
            }
            if (!restart) {
                for (int i = (int)frameIdx; i > 0; --i) {
                    if (!push(0)) {
                        http.end();
                        return true;
                    }
                }
            }
            songIdx += frameIdx;
            continue;
        }
        MP3FrameInfo info;
        MP3GetLastFrameInfo(dec, &info);
        const int chans = std::max(1, info.nChans), frames = info.outputSamps / chans;
        const double step = (double)info.samprate / RATE;   // source samples per output sample
        if (restart) {
            // The ring starts again here: what's in it is from before a skip.
            ringRestart((int64_t)songIdx);
            rsT = 0;
            rsPrev = rsLast = 0;
            restart = false;
        }
        for (int i = 0; i < frames; ++i) {
            int32_t mono = 0;
            for (int c = 0; c < chans; ++c) {
                mono += pcm[i * chans + c];
            }
            mono /= chans;
            // Above 30 kHz, average neighbours first: a rough low-pass, so
            // halving the rate doesn't fold the top end back down as hiss.
            int32_t cur = info.samprate > 30000 ? (mono + rsLast) / 2 : mono;
            rsLast = mono;
            while (rsT <= 1.0) {
                if (!push((int16_t)(rsPrev + (cur - rsPrev) * rsT))) {
                    http.end();
                    return true;
                }
                rsT += step;
            }
            rsT -= 1.0;
            rsPrev = cur;
        }
        songIdx += frameIdx;
    }
    http.end();
    return true;   // the rest plays from the ring, still "Playing" until FPP stops it
}

void showTask(void *) {
    HMP3Decoder dec = MP3InitDecoder();
    if (!dec) {
        setShowStatus("No memory for the MP3 decoder");
        vTaskDelete(nullptr);
    }
    uint32_t done = 0;   // the generation already played (or failed), so it isn't fetched again
    for (;;) {
        const uint32_t gen = showGen;
        if (!cfg.show || !showFile[0] || gen == done || WiFi.status() != WL_CONNECTED) {
            if (!showFile[0]) {
                setShowStatus(cfg.show ? "Waiting for FPP's show" : "Off");
            }
            vTaskDelay(50);
            continue;
        }
        char name[sizeof(showFile)];
        IPAddress from;
        portENTER_CRITICAL(&showMux);
        strlcpy(name, showFile, sizeof(name));
        from = showFrom;
        portEXIT_CRITICAL(&showMux);
        const size_t len = strlen(name);
        if (len < 4 || strcasecmp(name + len - 4, ".mp3")) {
            setShowStatus("Only MP3s play here, not " + String(name));
            done = gen;
            continue;
        }
        setShowStatus("Fetching " + String(name));
        if (!showStream(gen, name, from, dec)) {
            vTaskDelay(3000);   // try again shortly, unless FPP moves on
            continue;
        }
        MP3FreeDecoder(dec);    // fresh for the next song
        dec = MP3InitDecoder();
        done = gen;
    }
}

WiFiUDP multiSync;

// Called from the loop: packets from FPP, and stopping if it falls silent.
void showPoll() {
    static bool listening = false;
    if (!cfg.show || !showRing) {
        return;
    }
    // Listened for once on the Wi-Fi, and again after it reconnects (which
    // drops the multicast group).
    if (WiFi.status() != WL_CONNECTED) {
        if (listening) {
            multiSync.stop();
            listening = false;
        }
        return;
    }
    if (!listening) {
        listening = multiSync.beginMulticast(IPAddress(239, 70, 80, 80), MULTISYNC_PORT);   // broadcast and direct too
    }
    uint8_t pkt[512];
    while (int n = multiSync.parsePacket()) {
        n = multiSync.read(pkt, sizeof(pkt) - 1);
        if (n > 0) {
            pkt[n] = 0;
            handleMultiSync(pkt, n, multiSync.remoteIP());
        }
    }
    if (showFile[0] && millis() - showLastHeard > SHOW_TIMEOUT_MS) {
        showStop();
        setShowStatus("Waiting for FPP's show");
    }
}

void showBegin() {
    if (!cfg.show) {
        return;
    }
    showRing = (int16_t *)ps_malloc(SHOW_RING * sizeof(int16_t));
    if (!showRing) {
        setShowStatus("No room for show audio");
        return;
    }
    // On core 0 beside the Wi-Fi, below it; the mixer has core 1.
    xTaskCreatePinnedToCore(showTask, "show", 16384, nullptr, 2, nullptr, 0);
    setShowStatus("Waiting for FPP's show");
}
