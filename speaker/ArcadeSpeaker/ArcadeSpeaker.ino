// ArcadeSpeaker: a speaker for an FPP Arcade controller.
//
// An ESP32-S3 with a MAX98357A I2S amp and a small speaker, inside the
// controller. It keeps a copy of the game sounds from FPP in its flash and
// plays them when FPP says so, mixing several at once - so a lost network
// packet costs at most one sound, and sounds play the moment the event lands.
//
//   - Says hello to FPP (UDP 5571) every 3 s: which model it plays for.
//   - Gets "PLAY <seq> <game>/<name> <gain>", "STOP <game>" and "SYNC" back.
//   - Fetches the sound files over HTTP from the arcade plugin, at 22.05 kHz,
//     when it starts, when told to, and when the list changes (checked every
//     minute), and keeps them in PSRAM to play.
//   - Serves a settings page on port 80. With no Wi-Fi set up, or none it can
//     reach, it makes its own network "ArcadeSpeaker-XXXX" with that page.
//
// Build for "ESP32S3 Dev Module" with Flash Size 16MB, PSRAM "OPI PSRAM" and
// Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)" - see README.md.

#include <DNSServer.h>
#include <ESP_I2S.h>
#include <FFat.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <map>
#include <vector>

#define FIRMWARE "1.0"

// Pins for an ESP32-S3-DevKitC-1; any free GPIOs will do.
constexpr int PIN_BCLK = 5;    // MAX98357A BCLK
constexpr int PIN_LRC = 6;     // MAX98357A LRC
constexpr int PIN_DIN = 7;     // MAX98357A DIN
constexpr int PIN_AMP_SD = 8;  // MAX98357A SD: high plays, low switches the amp off

constexpr int RATE = 22050;
constexpr uint16_t LISTEN_PORT = 5572;
constexpr uint16_t FPP_PORT = 5571;
constexpr int MAX_VOICES = 10;
constexpr int BLOCK = 256;   // frames per write, about 12 ms

// ------------------------------------------------------------------ settings

struct Settings {
    String ssid, pass, fpp, model, name;
    int volume = 70;   // 0..100
} cfg;
Preferences prefs;

void loadSettings() {
    prefs.begin("speaker", true);
    cfg.ssid = prefs.getString("ssid", "");
    cfg.pass = prefs.getString("pass", "");
    cfg.fpp = prefs.getString("fpp", "");
    cfg.model = prefs.getString("model", "Matrix-Left");
    cfg.name = prefs.getString("name", "Controller speaker");
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

struct Clip {
    int16_t *data = nullptr;
    size_t frames = 0;
};
std::map<String, Clip> clips;   // "breakout/BounceFromPaddle" -> samples in PSRAM
SemaphoreHandle_t clipLock;     // held while clips are swapped out

struct Voice {
    const int16_t *data = nullptr;
    size_t frames = 0, pos = 0;
    int gainQ8 = 256;     // gain in 1/256ths
    char game[24] = "";
};
Voice voices[MAX_VOICES];
portMUX_TYPE voiceMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t eventsReceived = 0;
volatile uint32_t lastEventMs = 0;
String lastEvent;

void stopVoices(const char *game) {
    portENTER_CRITICAL(&voiceMux);
    for (auto &v : voices) {
        if (v.data && (!strcmp(game, "*") || !strcmp(game, v.game))) {
            v.data = nullptr;
        }
    }
    portEXIT_CRITICAL(&voiceMux);
}

void playClip(const String &path, float gain) {
    xSemaphoreTake(clipLock, portMAX_DELAY);
    auto it = clips.find(path);
    if (it == clips.end() || !it->second.data) {
        xSemaphoreGive(clipLock);
        return;   // no such sound: silent, as on FPP
    }
    const Clip c = it->second;
    xSemaphoreGive(clipLock);
    const int q = constrain((int)(gain * cfg.volume / 100.0f * 256), 0, 1024);
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
    v.frames = c.frames;
    v.pos = 0;
    v.gainQ8 = q;
    strlcpy(v.game, game.c_str(), sizeof(v.game));
    portEXIT_CRITICAL(&voiceMux);
}

// Mix and write 12 ms at a time; the I2S write blocks, which keeps time. The
// amp is switched off after two quiet seconds - no hiss, and less battery.
void audioTask(void *) {
    static I2SClass i2s;
    i2s.setPins(PIN_BCLK, PIN_LRC, PIN_DIN);
    if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        Serial.println("I2S failed to start");
        vTaskDelete(nullptr);
    }
    static int16_t out[BLOCK * 2];
    int32_t mix[BLOCK];
    uint32_t quietSince = millis();
    bool ampOn = false;
    for (;;) {
        memset(mix, 0, sizeof(mix));
        bool active = false;
        portENTER_CRITICAL(&voiceMux);
        for (auto &v : voices) {
            if (!v.data) {
                continue;
            }
            const size_t n = min((size_t)BLOCK, v.frames - v.pos);
            for (size_t i = 0; i < n; ++i) {
                mix[i] += (v.data[v.pos + i] * v.gainQ8) >> 8;
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
            out[2 * i] = out[2 * i + 1] = s;
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
    clips["test/beep"] = {d, n};
}

String clipFile(const String &path) { return "/s/" + path + ".pcm"; }

// Read every downloaded sound into PSRAM, replacing what was there.
void loadClips() {
    stopVoices("*");
    xSemaphoreTake(clipLock, portMAX_DELAY);
    for (auto &[k, c] : clips) {
        if (k != "test/beep") {
            free(c.data);
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
        int16_t *d = (int16_t *)ps_malloc(bytes);
        if (d && f.read((uint8_t *)d, bytes) == bytes) {
            clips[path] = {d, bytes / 2};
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

// ------------------------------------------------------------------ sync

String fppUrl(const String &path) { return "http://" + cfg.fpp + "/api/plugin-apis/arcade/" + path; }
String syncStatus = "Not synced yet";
uint32_t lastSyncCheck = 0;

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

// Fetch FPP's list and download whatever is new or changed; drop what's gone.
// Returns true if anything changed.
bool syncSounds() {
    if (WiFi.status() != WL_CONNECTED || cfg.fpp.isEmpty()) {
        return false;
    }
    HTTPClient http;
    http.setTimeout(5000);
    http.begin(fppUrl("sounds"));
    if (http.GET() != 200) {
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
    for (auto &[path, version] : remote) {
        if (local.count(path) && local[path] == version && FFat.exists(clipFile(path))) {
            continue;
        }
        FFat.mkdir("/s");
        FFat.mkdir("/s/" + path.substring(0, path.indexOf('/')));
        HTTPClient get;
        get.setTimeout(10000);
        get.begin(fppUrl("sound?f=" + path + "&rate=" + String(RATE)));
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
    }
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

void startAccessPoint() {
    const String ssid = "ArcadeSpeaker-" + String((uint32_t)ESP.getEfuseMac() & 0xffff, HEX);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid.c_str());
    dns.start(53, "*", WiFi.softAPIP());
    apMode = true;
    Serial.printf("Set-up network %s at %s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

void handleEvent(char *msg) {
    ++eventsReceived;
    lastEventMs = millis();
    lastEvent = msg;
    if (!strncmp(msg, "PLAY ", 5)) {
        // PLAY <seq> <path> <gain>
        char *seq = strtok(msg + 5, " ");
        char *path = strtok(nullptr, " ");
        char *gain = strtok(nullptr, " ");
        if (seq && path) {
            playClip(path, gain ? atof(gain) : 1.0f);
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

// ------------------------------------------------------------------ web page

String html(const String &s) {
    String o = s;
    o.replace("&", "&amp;");
    o.replace("<", "&lt;");
    o.replace("\"", "&quot;");
    return o;
}

void handleRoot() {
    String p;
    p.reserve(6000);
    p += F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
           "<title>Arcade Speaker</title><style>"
           "body{font:16px system-ui,sans-serif;margin:0;padding:16px;background:#14161b;color:#e6e8ee;max-width:640px}"
           "h1{font-size:20px}h2{font-size:16px;margin-top:24px}label{display:block;margin:10px 0 4px}"
           "input,select{font:inherit;padding:6px;width:100%;box-sizing:border-box;background:#22262f;color:inherit;border:1px solid #3a404c;border-radius:6px}"
           "button{font:inherit;padding:8px 14px;margin:8px 8px 0 0;background:#2d6cdf;color:#fff;border:0;border-radius:6px}"
           "table{border-collapse:collapse}td{padding:3px 12px 3px 0;vertical-align:top}.hint{opacity:.7;font-size:14px}"
           "</style></head><body><h1>Arcade Speaker</h1><table>");
    auto row = [&](const char *k, const String &v) { p += "<tr><td>" + String(k) + "</td><td>" + html(v) + "</td></tr>"; };
    row("Name", cfg.name);
    row("Plays for", cfg.model);
    row("Wi-Fi", WiFi.status() == WL_CONNECTED
                     ? cfg.ssid + " (" + WiFi.localIP().toString() + ", " + String(WiFi.RSSI()) + " dBm)"
                     : (apMode ? "Not connected - set it up below" : "Connecting to " + cfg.ssid));
    row("FPP", cfg.fpp.isEmpty() ? "Not set" : cfg.fpp);
    row("Sounds", syncStatus + " (" + String(clips.size() - 1) + " loaded)");
    row("Last event", eventsReceived ? lastEvent + " (" + String((millis() - lastEventMs) / 1000) + " s ago)" : "None yet");
    row("Storage", String(FFat.usedBytes() / 1024) + " of " + String(FFat.totalBytes() / 1024) + " KB");
    row("Firmware", FIRMWARE);
    p += F("</table><h2>Volume</h2><input type=range min=0 max=100 value='");
    p += String(cfg.volume);
    p += F("' oninput=\"fetch('/volume?v='+this.value)\">"
           "<button onclick=\"fetch('/test')\">Test sound</button><button onclick=\"fetch('/sync').then(()=>location.reload())\">Fetch sounds now</button>"
           "<h2>Settings</h2><form method=post action=/save>"
           "<label>Wi-Fi network</label><input name=ssid value='");
    p += html(cfg.ssid);
    p += F("'><label>Wi-Fi password</label><input name=pass type=password placeholder='(unchanged)'>"
           "<label>FPP player (IP address or name)</label><input name=fpp value='");
    p += html(cfg.fpp);
    p += F("'><label>Model it plays for</label><input name=model list=models value='");
    p += html(cfg.model);
    p += F("'><datalist id=models></datalist><div class=hint>The FPP overlay model this controller plays, as on the Arcade Games page"
           " (Matrix-Left, Matrix-Right, ...). Only that model's sounds play here.</div>"
           "<label>Name</label><input name=name value='");
    p += html(cfg.name);
    p += F("'><button>Save and restart</button></form>"
           "<script>fetch('http://");
    p += html(cfg.fpp);
    p += F("/api/models?simple=true').then(r=>r.json()).then(m=>{const d=document.getElementById('models');"
           "m.forEach(n=>{const o=document.createElement('option');o.value=n;d.appendChild(o)})}).catch(()=>{})</script>"
           "</body></html>");
    web.send(200, "text/html", p);
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
    web.send(200, "text/html", "<meta http-equiv=refresh content='8;url=/'>Saved. Restarting...");
    delay(500);
    ESP.restart();
}

// ------------------------------------------------------------------ main

void setup() {
    Serial.begin(115200);
    pinMode(PIN_AMP_SD, OUTPUT);
    digitalWrite(PIN_AMP_SD, LOW);
    loadSettings();
    clipLock = xSemaphoreCreateMutex();
    if (!FFat.begin(true)) {
        Serial.println("No FAT partition: build with the 3MB APP / 9.9MB FATFS partition scheme");
    }
    makeTestBeep();
    loadClips();
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 5, nullptr, 1);

    WiFi.setHostname(("arcade-speaker-" + String((uint32_t)ESP.getEfuseMac() & 0xffff, HEX)).c_str());
    if (cfg.ssid.isEmpty()) {
        startAccessPoint();
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
        // Modem sleep holds incoming packets for up to a beacon interval; keep
        // the radio awake so a sound plays the moment its event arrives.
        WiFi.setSleep(false);
        WiFi.setAutoReconnect(true);
        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
            delay(200);
        }
        if (WiFi.status() != WL_CONNECTED) {
            startAccessPoint();   // keep trying the network, and offer set-up meanwhile
        }
    }
    udp.begin(LISTEN_PORT);
    web.on("/", handleRoot);
    web.on("/save", HTTP_POST, handleSave);
    web.on("/volume", [] {
        cfg.volume = constrain(web.arg("v").toInt(), 0, 100);
        saveSettings();
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
    delay(1);
}
