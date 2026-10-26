// ArcadeSpeaker: a speaker for an FPP Arcade controller.
//
// An ESP32-S3 with a MAX98357A I2S amp and a small speaker, inside the
// controller. It keeps a copy of the game sounds from FPP in its flash and
// plays them when FPP says so, mixing several at once - so a lost network
// packet costs at most one sound, and sounds play the moment the event lands.
//
//   - Says hello to FPP (UDP 5571) every 3 s: which model it plays for.
//   - Gets "PLAY <seq> <game>/<name> <gain> <ms>", "STOP <game>" and "SYNC"
//     back, and skips a short sound that arrives late (see lateness()).
//   - Fetches the sound files over HTTP from the arcade plugin, at 22.05 kHz
//     as 8-bit mu-law (half the size of 16-bit, so every game's sounds fit),
//     when it starts, when told to, and when the list changes (checked every
//     minute), and keeps them in PSRAM to play.
//   - Serves a settings page on port 80. With no Wi-Fi set up, or none it can
//     reach, it makes its own network "ArcadeSpeaker-XXXX" with that page.
//   - Shows what it's doing on the board's RGB light (see README.md).
//
// Build for "ESP32S3 Dev Module" with Flash Size 16MB, PSRAM "OPI PSRAM" and
// Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)" - see README.md.

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <mdns.h>
#include <ESP_I2S.h>
#include <FFat.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>

#include <algorithm>
#include <map>
#include <vector>

#include "page.h"

#define FIRMWARE "1.3"

// Pins for an ESP32-S3-DevKitC-1; any free GPIOs will do.
constexpr int PIN_BCLK = 5;    // MAX98357A BCLK
constexpr int PIN_LRC = 6;     // MAX98357A LRC
constexpr int PIN_DIN = 7;     // MAX98357A DIN
constexpr int PIN_AMP_SD = 8;  // MAX98357A SD: high plays, low switches the amp off
constexpr int PIN_LED = 48;    // the board's RGB status light (38 on some boards)

constexpr int RATE = 22050;       // the sounds, as fetched and kept
constexpr int OUT_RATE = 44100;   // to the amp: one of the MAX98357A's rates, as 22.05 kHz isn't
constexpr uint16_t LISTEN_PORT = 5572;
constexpr uint16_t FPP_PORT = 5571;
constexpr int MAX_VOICES = 10;
constexpr int BLOCK = 256;   // frames per write, about 12 ms

// ------------------------------------------------------------------ settings

struct Settings {
    String ssid, pass, fpp, model, name;   // name also gives its address: <name>.local
    int volume = 70;   // 0..100
} cfg;
Preferences prefs;

void loadSettings() {
    prefs.begin("speaker", true);
    cfg.ssid = prefs.getString("ssid", "");
    cfg.pass = prefs.getString("pass", "");
    cfg.fpp = prefs.getString("fpp", "");
    cfg.model = prefs.getString("model", "Matrix-Left");
    cfg.name = prefs.getString("name", "");
    if (cfg.name.isEmpty() || cfg.name == "Controller speaker") {   // unset, or 1.0's default
        cfg.name = "Controller Speaker " + boardId();   // unique from the start
    }
    cfg.volume = prefs.getInt("volume", 70);
    prefs.end();
}

void saveSettings() {
    prefs.begin("speaker", false);
    prefs.putString("ssid", cfg.ssid);
    prefs.putString("pass", cfg.pass);
    prefs.putString("fpp", cfg.fpp);
    prefs.putString("model", cfg.model);
    prefs.putString("name", cfg.name);
    prefs.putInt("volume", cfg.volume);
    prefs.end();
}

// ------------------------------------------------------------------ sounds

// A clip is 16-bit samples, or one mu-law byte a sample (what FPP sends from
// plugin versions that have it: the file starts "ULW1").
struct Clip {
    uint8_t *mem = nullptr;        // what to free
    const uint8_t *data = nullptr;
    size_t frames = 0;
    bool ulaw = false;
};
std::map<String, Clip> clips;   // "breakout/BounceFromPaddle" -> samples in PSRAM
int16_t ULAW[256];              // mu-law byte -> sample

void makeUlawTable() {
    for (int i = 0; i < 256; ++i) {
        const int u = ~i & 0xff, exp = (u >> 4) & 7;
        const int t = (((u & 0x0f) << 3) + 0x84) << exp;
        ULAW[i] = (int16_t)((u & 0x80) ? 0x84 - t : t - 0x84);
    }
}
SemaphoreHandle_t clipLock;     // held while clips are swapped out

struct Voice {
    const uint8_t *data = nullptr;
    bool ulaw = false;
    size_t frames = 0, pos = 0;
    int gainQ8 = 256;     // gain in 1/256ths
    char game[24] = "";
};
Voice voices[MAX_VOICES];
portMUX_TYPE voiceMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t eventsReceived = 0;
volatile uint32_t lastEventMs = 0;
uint32_t lastPlayMs = 0;
String lastEvent;
volatile bool audioOk = true;   // false if I2S wouldn't start
bool storageOk = true;          // false if there's no FAT partition
uint64_t storageUsed = 0;       // FFat.usedBytes(), kept: working it out reads the flash

void stopVoices(const char *game) {
    portENTER_CRITICAL(&voiceMux);
    for (auto &v : voices) {
        if (v.data && (!strcmp(game, "*") || !strcmp(game, v.game))) {
            v.data = nullptr;
        }
    }
    portEXIT_CRITICAL(&voiceMux);
}

constexpr int LATE_MS = 150;              // a short sound arriving later than this is skipped
constexpr size_t SHORT_FRAMES = RATE;     // under a second is an effect; longer (music) always plays
uint32_t lateSkipped = 0;

// late: how far behind the event this arrived, in ms (see lateness()).
void playClip(const String &path, float gain, int32_t late = 0) {
    xSemaphoreTake(clipLock, portMAX_DELAY);
    auto it = clips.find(path);
    if (it == clips.end() || !it->second.data) {
        xSemaphoreGive(clipLock);
        return;   // no such sound: silent, as on FPP
    }
    const Clip c = it->second;
    xSemaphoreGive(clipLock);
    if (late > LATE_MS && c.frames < SHORT_FRAMES) {
        ++lateSkipped;   // a bounce heard after the bounce is worse than none
        return;
    }
    const int q = constrain((int)(gain * 256), 0, 1024);   // the volume is applied as it plays
    const String game = path.substring(0, path.indexOf('/'));
    portENTER_CRITICAL(&voiceMux);
    // A free voice, or else the one that's been playing longest.
    int pick = 0;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (!voices[i].data) {
            pick = i;
            break;
        }
        if (voices[i].pos > voices[pick].pos) {
            pick = i;
        }
    }
    Voice &v = voices[pick];
    v.data = c.data;
    v.ulaw = c.ulaw;
    v.frames = c.frames;
    v.pos = 0;
    v.gainQ8 = q;
    strlcpy(v.game, game.c_str(), sizeof(v.game));
    portEXIT_CRITICAL(&voiceMux);
}

// Mix and write 12 ms at a time; the I2S write blocks, which keeps time. The
// mix goes out at twice its rate, with a point halfway between each pair of
// samples. The amp is switched off after two quiet seconds - no hiss, and
// less battery.
void audioTask(void *) {
    static I2SClass i2s;
    i2s.setPins(PIN_BCLK, PIN_LRC, PIN_DIN);
    if (!i2s.begin(I2S_MODE_STD, OUT_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        Serial.println("I2S failed to start");
        audioOk = false;
        vTaskDelete(nullptr);
    }
    static_assert(OUT_RATE == 2 * RATE, "the mix is written out at twice its rate");
    static int16_t out[BLOCK * 4];   // left and right, two frames a sample
    int32_t mix[BLOCK];
    uint32_t quietSince = millis();
    bool ampOn = false;
    int16_t prev = 0;   // the last sample written, for the next halfway point
    for (;;) {
        memset(mix, 0, sizeof(mix));
        bool active = false;
        // The volume is read every block, so moving the slider is heard at once.
        const int volQ8 = cfg.volume * 256 / 100;
        portENTER_CRITICAL(&voiceMux);
        for (auto &v : voices) {
            if (!v.data) {
                continue;
            }
            const size_t n = min((size_t)BLOCK, v.frames - v.pos);
            const int g = (v.gainQ8 * volQ8) >> 8;
            if (v.ulaw) {
                const uint8_t *d = v.data + v.pos;
                for (size_t i = 0; i < n; ++i) {
                    mix[i] += (ULAW[d[i]] * g) >> 8;
                }
            } else {
                const int16_t *d = (const int16_t *)v.data + v.pos;
                for (size_t i = 0; i < n; ++i) {
                    mix[i] += (d[i] * g) >> 8;
                }
            }
            v.pos += n;
            if (v.pos >= v.frames) {
                v.data = nullptr;
            }
            active = true;
        }
        portEXIT_CRITICAL(&voiceMux);
        for (int i = 0; i < BLOCK; ++i) {
            const int16_t s = (int16_t)constrain(mix[i], -32768, 32767);
            const int16_t half = (int16_t)((prev + s) / 2);
            out[4 * i] = out[4 * i + 1] = half;
            out[4 * i + 2] = out[4 * i + 3] = s;
            prev = s;
        }
        if (active) {
            quietSince = millis();
            if (!ampOn) {
                digitalWrite(PIN_AMP_SD, HIGH);
                ampOn = true;
            }
        } else if (ampOn && millis() - quietSince > 2000) {
            digitalWrite(PIN_AMP_SD, LOW);
            ampOn = false;
        }
        i2s.write((uint8_t *)out, sizeof(out));
    }
}

// A short beep for the Test button, made rather than stored.
void makeTestBeep() {
    const size_t n = RATE * 3 / 10;
    int16_t *d = (int16_t *)ps_malloc(n * 2);
    if (!d) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        const float env = min(1.0f, min(i / 200.0f, (n - i) / 2000.0f));
        d[i] = (int16_t)(12000 * env * sinf(2 * PI * 880 * i / RATE));
    }
    clips["test/beep"] = {(uint8_t *)d, (const uint8_t *)d, n, false};
}

String clipFile(const String &path) { return "/s/" + path + ".pcm"; }

// Read every downloaded sound into PSRAM, replacing what was there.
void loadClips() {
    stopVoices("*");
    xSemaphoreTake(clipLock, portMAX_DELAY);
    for (auto &[k, c] : clips) {
        if (k != "test/beep") {
            free(c.mem);
        }
    }
    Clip beep = clips["test/beep"];
    clips.clear();
    clips["test/beep"] = beep;
    File idx = FFat.open("/index.txt");
    while (idx && idx.available()) {
        const String line = idx.readStringUntil('\n');
        const String path = line.substring(0, line.indexOf(' '));
        File f = FFat.open(clipFile(path));
        if (!f) {
            continue;
        }
        const size_t bytes = f.size();
        uint8_t *d = (uint8_t *)ps_malloc(bytes);
        if (d && f.read(d, bytes) == bytes) {
            if (bytes >= 4 && !memcmp(d, "ULW1", 4)) {
                clips[path] = {d, d + 4, bytes - 4, true};
            } else {
                clips[path] = {d, d, bytes / 2, false};
            }
        } else {
            free(d);
            Serial.printf("No room for %s\n", path.c_str());
        }
        f.close();
    }
    idx.close();
    xSemaphoreGive(clipLock);
    Serial.printf("%u sounds loaded, %u bytes of PSRAM free\n", (unsigned)clips.size() - 1, ESP.getFreePsram());
}

// ------------------------------------------------------------------ status light

// Kept dim: the LED is very bright at full power.
uint32_t ledNow = 0xffffffff;

void setLed(uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t c = (r << 16) | (g << 8) | b;
    if (c != ledNow) {   // a write takes a moment, so only on a change
        ledNow = c;
        rgbLedWrite(PIN_LED, r, g, b);
    }
}

// ------------------------------------------------------------------ sync

String fppUrl(const String &path) { return "http://" + cfg.fpp + "/api/plugin-apis/arcade/" + path; }
String syncStatus = "Not synced yet";
uint32_t lastSyncCheck = 0;
bool fppAsked = false, fppAnswered = false;   // the last time we asked for its sound list

// What we have: "path version" per line.
std::map<String, String> readIndex() {
    std::map<String, String> m;
    File idx = FFat.open("/index.txt");
    while (idx && idx.available()) {
        const String line = idx.readStringUntil('\n');
        const int sp = line.indexOf(' ');
        if (sp > 0) {
            m[line.substring(0, sp)] = line.substring(sp + 1);
        }
    }
    return m;
}

// Firmware 1.0 kept 16-bit sounds, which no longer all fit: drop them once so
// they're fetched again as mu-law.
void dropOldSounds() {
    if (FFat.exists("/ulaw")) {
        return;
    }
    for (auto &[path, version] : readIndex()) {
        FFat.remove(clipFile(path));
    }
    FFat.remove("/index.txt");
    File mark = FFat.open("/ulaw", FILE_WRITE);
    mark.print("Sounds are fetched as mu-law\n");
    mark.close();
}

// Fetch FPP's list and download whatever is new or changed; drop what's gone.
// Returns true if anything changed.
bool syncSounds() {
    if (WiFi.status() != WL_CONNECTED || cfg.fpp.isEmpty()) {
        return false;
    }
    HTTPClient http;
    http.setTimeout(5000);
    http.begin(fppUrl("sounds"));
    fppAsked = true;
    fppAnswered = http.GET() == 200;
    if (!fppAnswered) {
        syncStatus = "Couldn't reach FPP at " + cfg.fpp;
        http.end();
        return false;
    }
    const String body = http.getString();
    http.end();

    // {"files":[{"path":"breakout/X","version":"123-456"}, ...]}
    std::map<String, String> remote;
    for (int at = body.indexOf("\"path\":\""); at >= 0; at = body.indexOf("\"path\":\"", at + 1)) {
        const int ps = at + 8, pe = body.indexOf('"', ps);
        const int vs = body.indexOf("\"version\":\"", pe) + 11, ve = body.indexOf('"', vs);
        if (pe > ps && ve > vs) {
            remote[body.substring(ps, pe)] = body.substring(vs, ve);
        }
    }
    std::map<String, String> local = readIndex();
    bool changed = false;
    int fetched = 0, failed = 0;
    // Every file is looked for once after starting; after that, only the
    // index is trusted. Looking for 300 files each minute held up the loop -
    // and sounds - for a moment, and reading the flash pauses the radio too.
    static bool filesChecked = false;
    for (auto &[path, version] : remote) {
        if (local.count(path) && local[path] == version && (filesChecked || FFat.exists(clipFile(path)))) {
            continue;
        }
        // Purple, flickering a file at a time: the loop waits for this.
        const bool odd = (fetched + failed) & 1;
        setLed(odd ? 4 : 16, 0, odd ? 6 : 24);
        FFat.mkdir("/s");
        FFat.mkdir("/s/" + path.substring(0, path.indexOf('/')));
        HTTPClient get;
        get.setTimeout(10000);
        get.begin(fppUrl("sound?f=" + path + "&rate=" + String(RATE) + "&fmt=ulaw"));
        bool ok = false;
        if (get.GET() == 200) {
            File f = FFat.open(clipFile(path), FILE_WRITE);
            ok = f && get.writeToStream(&f) > 0;
            f.close();
        }
        get.end();
        if (ok) {
            local[path] = version;
            ++fetched;
            changed = true;
        } else {
            ++failed;
        }
    }
    for (auto it = local.begin(); it != local.end();) {
        if (!remote.count(it->first)) {
            FFat.remove(clipFile(it->first));
            it = local.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        File idx = FFat.open("/index.txt", FILE_WRITE);
        for (auto &[path, version] : local) {
            idx.print(path + " " + version + "\n");
        }
        idx.close();
        loadClips();
        storageUsed = FFat.usedBytes();
    }
    filesChecked = true;
    syncStatus = String(remote.size()) + " sounds from FPP" + (fetched ? ", " + String(fetched) + " just fetched" : "") +
                 (failed ? ", " + String(failed) + " failed" : "");
    return changed;
}

// ------------------------------------------------------------------ network

WiFiUDP udp;
WebServer web(80);
DNSServer dns;
bool apMode = false;
bool syncWanted = true;
uint32_t lastHello = 0;

// The last four hex digits of the MAC address, which differ from board to
// board (the first ones are the maker's).
String boardId() {
    const uint64_t mac = ESP.getEfuseMac();   // first byte lowest
    char id[5];
    snprintf(id, sizeof(id), "%02x%02x", (unsigned)(mac >> 32) & 0xff, (unsigned)(mac >> 40) & 0xff);
    return id;
}

// Its name on the network, from its name: "Left Controller" is at
// http://left-controller.local, and the router lists it so. Letters and digits
// are kept, anything else becomes one hyphen. page.h does the same.
String hostName() {
    String o;
    for (size_t i = 0; i < cfg.name.length() && o.length() < 32; ++i) {
        const char c = cfg.name[i];
        if (isalnum((unsigned char)c)) {
            o += (char)tolower((unsigned char)c);
        } else if (o.length() && !o.endsWith("-")) {
            o += '-';
        }
    }
    while (o.endsWith("-")) {
        o.remove(o.length() - 1);
    }
    return o.length() ? o : "arcade-speaker-" + boardId();
}

// The name it's actually answering to: if another device has the same one,
// the network gives this one "-2" on the end.
String hostInUse() {
    char h[65];
    return mdns_hostname_get(h) == ESP_OK ? String(h) : hostName();
}

void startAccessPoint() {
    const String ssid = "ArcadeSpeaker-" + boardId();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid.c_str());
    dns.start(53, "*", WiFi.softAPIP());
    apMode = true;
    Serial.printf("Set-up network %s at %s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

// FPP stamps each PLAY with its own clock in ms. The gap between that and
// ours is fixed but for how long the packet took, so the smallest gap seen
// lately is a packet that wasn't held up, and the rest are late by how much
// bigger theirs is. Lately is this half-minute and the last, so the two clocks
// drifting apart is followed.
int32_t gapLeast[2] = {INT32_MAX, INT32_MAX};
uint32_t gapSince = 0;
uint32_t playsTimed = 0, lateOver50 = 0;
int32_t lateWorst = 0;

int32_t lateness(uint32_t fppMs) {
    const uint32_t now = millis();
    const int32_t gap = (int32_t)(now - fppMs);
    if (now - gapSince > 30000) {
        gapLeast[1] = gapLeast[0];
        gapLeast[0] = INT32_MAX;
        gapSince = now;
    }
    int32_t least = min(gapLeast[0], gapLeast[1]);
    if (least != INT32_MAX && (int64_t)gap - least > 10000) {
        gapLeast[0] = gapLeast[1] = least = INT32_MAX;   // a jump: FPP's player restarted, so start again
    }
    gapLeast[0] = min(gapLeast[0], gap);
    least = min(least, gap);
    const int32_t late = (int32_t)((int64_t)gap - least);
    ++playsTimed;
    lateOver50 += late > 50;
    lateWorst = max(lateWorst, late);
    return late;
}

void handleEvent(char *msg) {
    ++eventsReceived;
    lastEventMs = millis();
    lastEvent = msg;
    if (!strncmp(msg, "PLAY ", 5)) {
        // PLAY <seq> <path> <gain> [<ms>]: older plugins don't send the time.
        char *seq = strtok(msg + 5, " ");
        char *path = strtok(nullptr, " ");
        char *gain = strtok(nullptr, " ");
        char *ms = strtok(nullptr, " ");
        if (seq && path) {
            playClip(path, gain ? atof(gain) : 1.0f, ms ? lateness(strtoul(ms, nullptr, 10)) : 0);
            lastPlayMs = millis();
        }
    } else if (!strncmp(msg, "STOP ", 5)) {
        stopVoices(msg + 5);
    } else if (!strcmp(msg, "SYNC")) {
        syncWanted = true;
    }
}

void sendHello() {
    if (WiFi.status() != WL_CONNECTED || cfg.fpp.isEmpty()) {
        return;
    }
    IPAddress ip;
    if (!WiFi.hostByName(cfg.fpp.c_str(), ip)) {
        return;
    }
    udp.beginPacket(ip, FPP_PORT);
    udp.printf("FAS-HELLO 1 %s|%s|%s|%d", cfg.model.c_str(), cfg.name.c_str(), FIRMWARE, cfg.volume);
    udp.endPacket();
}

// ------------------------------------------------------------------ status

// What the speaker is doing, for the status light and the web page.
enum class State { Fault, SetUp, Joining, NoFpp, Waiting, Ready };
State state();   // declared here: the Arduino IDE's own declaration would come before State

// What's wrong with the speaker itself, or "" if nothing.
String fault() {
    if (!storageOk) {
        return "No FAT partition: rebuild with the 3MB APP / 9.9MB FATFS partition scheme.";
    }
    if (!audioOk) {
        return "The amp output (I2S) didn't start.";
    }
    if (fppAnswered && clips.size() <= 1) {
        return "No sounds loaded. FPP has none to give, or they don't fit.";
    }
    return "";
}

State state() {
    if (fault().length()) {
        return State::Fault;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return apMode ? State::SetUp : State::Joining;
    }
    if (cfg.fpp.isEmpty() || (fppAsked && !fppAnswered)) {
        return State::NoFpp;
    }
    return fppAsked ? State::Ready : State::Waiting;
}

// White blink: a sound was just asked for. Red blinking: something's wrong
// with the speaker itself; red: FPP doesn't answer. Blue blinking: the set-up
// network is on. Amber: joining the Wi-Fi or waiting to hear from FPP.
// Green: ready.
void updateLed() {
    const uint32_t now = millis();
    const bool blinkOn = now % 1000 < 500;
    if (now - lastPlayMs < 80 && lastPlayMs) {
        setLed(24, 24, 24);
        return;
    }
    switch (state()) {
        case State::Fault: setLed(blinkOn ? 24 : 0, 0, 0); break;
        case State::SetUp: setLed(0, 0, blinkOn ? 24 : 0); break;
        case State::NoFpp: setLed(24, 0, 0); break;
        case State::Joining:
        case State::Waiting: setLed(24, 10, 0); break;
        case State::Ready: setLed(0, 12, 0); break;
    }
}

// ------------------------------------------------------------------ web page

String json(const String &s) {
    String o = "\"";
    for (size_t i = 0; i < s.length(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if ((uint8_t)c < 0x20) {
            char u[7];
            snprintf(u, sizeof(u), "\\u%04x", c);
            o += u;
        } else {
            o += c;
        }
    }
    return o + "\"";
}

// Everything the page shows, refreshed every two seconds.
void handleStatus() {
    static const char *const NAMES[] = {"fault", "setup", "joining", "nofpp", "waiting", "ready"};
    const bool connected = WiFi.status() == WL_CONNECTED;
    String j;
    j.reserve(700);
    j += "{\"state\":\"" + String(NAMES[(int)state()]) + "\"";
    j += ",\"fault\":" + json(fault());
    j += ",\"name\":" + json(cfg.name);
    j += ",\"model\":" + json(cfg.model);
    j += ",\"fpp\":" + json(cfg.fpp);
    j += ",\"ssid\":" + json(cfg.ssid);
    j += ",\"connected\":" + String(connected ? "true" : "false");
    j += ",\"ip\":" + json(connected ? WiFi.localIP().toString() : "");
    j += ",\"rssi\":" + String(connected ? WiFi.RSSI() : 0);
    wifi_ps_type_t ps = WIFI_PS_MIN_MODEM;
    esp_wifi_get_ps(&ps);
    j += ",\"powerSave\":" + String(ps == WIFI_PS_NONE ? "false" : "true");
    j += ",\"timed\":" + String(playsTimed);
    j += ",\"late50\":" + String(lateOver50);
    j += ",\"lateSkipped\":" + String(lateSkipped);
    j += ",\"lateWorst\":" + String(lateWorst);
    j += ",\"ap\":" + json(apMode ? "ArcadeSpeaker-" + boardId() : "");
    j += ",\"fppAsked\":" + String(fppAsked ? "true" : "false");
    j += ",\"fppOk\":" + String(fppAnswered ? "true" : "false");
    j += ",\"sync\":" + json(syncStatus);
    j += ",\"sounds\":" + String(clips.size() - 1);
    j += ",\"events\":" + String(eventsReceived);
    j += ",\"last\":" + json(lastEvent);
    j += ",\"lastAgo\":" + String((millis() - lastEventMs) / 1000);
    j += ",\"used\":" + String(storageUsed);
    j += ",\"total\":" + String(storageOk ? FFat.totalBytes() : 0);
    j += ",\"volume\":" + String(cfg.volume);
    j += ",\"firmware\":" + json(FIRMWARE);
    j += ",\"host\":" + json(hostInUse());
    j += ",\"uptime\":" + String(millis() / 1000) + "}";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", j);
}

// FPP's model names, fetched here so the page needn't reach FPP itself.
void handleModels() {
    HTTPClient http;
    http.setTimeout(3000);
    http.begin("http://" + cfg.fpp + "/api/models?simple=true");
    if (WiFi.status() == WL_CONNECTED && !cfg.fpp.isEmpty() && http.GET() == 200) {
        web.send(200, "application/json", http.getString());
    } else {
        web.send(200, "application/json", "[]");
    }
    http.end();
}

// The Wi-Fi networks in range, strongest first, each once.
void handleScan() {
    const int n = WiFi.scanNetworks();
    std::map<String, int> best;
    for (int i = 0; i < n; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() && (!best.count(ssid) || WiFi.RSSI(i) > best[ssid])) {
            best[ssid] = WiFi.RSSI(i);
        }
    }
    WiFi.scanDelete();
    std::vector<std::pair<String, int>> list(best.begin(), best.end());
    std::sort(list.begin(), list.end(), [](auto &a, auto &b) { return a.second > b.second; });
    String j = "[";
    for (auto &[ssid, rssi] : list) {
        j += (j.length() > 1 ? "," : "") + String("{\"ssid\":") + json(ssid) + ",\"rssi\":" + String(rssi) + "}";
    }
    web.send(200, "application/json", j + "]");
}

void handleSave() {
    cfg.ssid = web.arg("ssid");
    if (web.arg("pass").length()) {
        cfg.pass = web.arg("pass");
    }
    cfg.fpp = web.arg("fpp");
    cfg.model = web.arg("model");
    cfg.name = web.arg("name");
    saveSettings();
    web.send(200, "text/plain", "ok");
    delay(500);
    ESP.restart();
}

// ------------------------------------------------------------------ main

void setup() {
    Serial.begin(115200);
    pinMode(PIN_AMP_SD, OUTPUT);
    digitalWrite(PIN_AMP_SD, LOW);
    setLed(24, 10, 0);   // amber while it starts up
    loadSettings();
    clipLock = xSemaphoreCreateMutex();
    if (!FFat.begin(true)) {
        Serial.println("No FAT partition: build with the 3MB APP / 9.9MB FATFS partition scheme");
        storageOk = false;
    }
    makeUlawTable();
    makeTestBeep();
    dropOldSounds();
    loadClips();
    storageUsed = storageOk ? FFat.usedBytes() : 0;
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 5, nullptr, 1);

    WiFi.setHostname(hostName().c_str());
    if (cfg.ssid.isEmpty()) {
        startAccessPoint();
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.enableIPv6();   // so its .local name answers IPv6 lookups too, and nobody waits for one
        WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
        // Modem sleep holds incoming packets for up to a beacon interval; keep
        // the radio awake so a sound plays the moment its event arrives.
        WiFi.setSleep(false);
        WiFi.setAutoReconnect(true);
        // And at the driver, on every (re)connection, so nothing turns it back on.
        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { esp_wifi_set_ps(WIFI_PS_NONE); },
                     ARDUINO_EVENT_WIFI_STA_CONNECTED);
        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
            delay(200);
        }
        if (WiFi.status() != WL_CONNECTED) {
            startAccessPoint();   // keep trying the network, and offer set-up meanwhile
        }
    }
    if (MDNS.begin(hostName().c_str())) {   // http://<name>.local
        MDNS.addService("http", "tcp", 80);
    }
    udp.begin(LISTEN_PORT);
    web.on("/", [] { web.send(200, "text/html", PAGE); });
    web.on("/status", handleStatus);
    web.on("/favicon.ico", [] { web.send(204); });   // the page has its own icon
    web.on("/models", handleModels);
    web.on("/scan", handleScan);
    web.on("/save", HTTP_POST, handleSave);
    web.on("/volume", [] {
        // Heard straight away; saved once the slider is let go (save=1).
        cfg.volume = constrain(web.arg("v").toInt(), 0, 100);
        if (web.hasArg("save")) {
            saveSettings();
        }
        web.send(200, "text/plain", "ok");
    });
    web.on("/test", [] {
        playClip("test/beep", 1.0f);
        web.send(200, "text/plain", "ok");
    });
    web.on("/sync", [] {
        syncSounds();
        web.send(200, "text/plain", syncStatus);
    });
    web.onNotFound([] {
        // The set-up network sends every address here, so phones show the page.
        web.sendHeader("Location", "http://" + (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "/");
        web.send(302, "text/plain", "");
    });
    web.begin();
}

void loop() {
    web.handleClient();
    if (apMode) {
        dns.processNextRequest();
    }
    char buf[256];
    while (int n = udp.parsePacket()) {
        n = udp.read(buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            handleEvent(buf);
        }
    }
    const uint32_t now = millis();
    if (now - lastHello > 3000) {
        lastHello = now;
        sendHello();
    }
    if (WiFi.status() == WL_CONNECTED && (syncWanted || now - lastSyncCheck > 60000)) {
        syncWanted = false;
        lastSyncCheck = now;
        syncSounds();
    }
    updateLed();
    delay(1);
}
