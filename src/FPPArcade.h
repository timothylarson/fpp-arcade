#ifndef __FPPARCADE__
#define __FPPARCADE__

#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <list>
#include <set>
#include <cstdint>

#include "overlays/PixelOverlayEffects.h"
#include "FPPArcadePause.h"

// One pixel of the 3x5 letter for c (A-Z, 0-9 and : ! . , - '): true if lit.
// Anything else is blank.
bool arcadeGlyph(char c, int x, int y);
// The same letter drawn scl times the size: pixel (px, py) of its 3scl x 5scl
// box. At twice the size or more some letters have extra detail.
bool arcadeGlyphScaled(char c, int px, int py, int scl);
// How many LEDs wide that letter is drawn: 3scl, or a little more for a wide one.
int arcadeGlyphWidth(char c, int scl);

#include "ShowTheme.h"

// Best scores, kept by model and game in media/plugindata/fpp-arcade/best.json.
// Records score; true if it beats the best so far (any score above 0 beats
// none). previousBest, if given, gets the best before this one.
bool arcadeRecordBest(const std::string &model, const std::string &game, int score, int *previousBest = nullptr);

class FPPArcadeGame;
// The game's title on its model, with its GameSelected jingle (Select).
void arcadeShowTitle(FPPArcadeGame *game);
// A button for a model's games, front first: Select moves to the next game
// and shows its title, Start starts the front one, anything else goes to it.
// Also the cheat codes, and EXIT on the pause menu.
void arcadeButton(std::list<FPPArcadeGame *> &games, const std::string &button);
// A joystick axis for a model's games, to the front one (see FPPArcadeGame::axis).
void arcadeAxis(std::list<FPPArcadeGame *> &games, const std::string &axis, int value);
// The game last played on a model (started from its title), or "".
void arcadeNotePlayed(const std::string &model, const std::string &game);
std::string arcadeLastPlayed(const std::string &model);
// The axes FPP Arcade Axis takes: Up -> Down, Left -> Right, Down -> Up, Right -> Left.
extern std::vector<std::string> AXIS;
// A game from its entry in the plugin's config, by its "game" name, or null.
namespace Json { class Value; }
FPPArcadeGame *arcadeCreateGame(Json::Value &config);

// Whether a finished game gets its end show (ArcadeShow.h): mode is the End
// Show setting (0 never, 1 after a good game, 2 after every game), good that
// it reached the game's Good Game mark, newBest from arcadeRecordBest().
inline bool arcadeWantShow(int mode, bool good, bool newBest) {
    return mode == 2 || (mode == 1 && (good || newBest));
}

class FPPArcadeGame {
public:
    FPPArcadeGame(Json::Value &config);
    virtual ~FPPArcadeGame() {}
    
    virtual const std::string &getName() = 0;
    
    virtual void button(const std::string &button) {}
    virtual void axis(const std::string &axis, int value);
    // Watches for the cheat code (see FPPArcade.cpp) and passes it to the
    // running game. True when this press finished the code, so the caller
    // doesn't pass it on as well.
    bool cheatInput(const std::string &button);

    
    virtual bool isRunning();
    virtual void stop();
    // True once, after EXIT was picked on the running game's pause menu.
    bool takeExitRequest();
    // The button that picked EXIT: its release is ignored, or with the game
    // gone it would start a new one.
    std::string releaseToIgnore;
    // Give back anything outside this library that points at the game, such
    // as a timer. Called from the plugin's shutdown(), before FPP unloads it.
    virtual void release() {}

    // The Sequence brightness setting: how bright the sequence under the game
    // is while it runs, 0-100. 100 (left alone) unless its overlay is
    // Transparent, the only way the sequence shows behind a game.
    int sequenceBrightness() {
        if (findOption("overlay", "Overwrite") != "Transparent") {
            return 100;
        }
        return std::clamp(findIntOption("sequenceBrightness", 100), 0, 100);
    }

    // Its Overlay setting: Overwrite or Transparent.
    std::string overlayMode() { return findOption("overlay", "Overwrite"); }

    int getIdx() const { return idx; };
    void setIdx(int i) { idx = i; }
    const std::string &getModelName() const { return modelName; }
protected:
    std::string findOption(const std::string &s, const std::string &def = "");
    // findOption() as an int, tolerating a blank or non-numeric value in the
    // config rather than throwing std::invalid_argument out of a button press.
    int findIntOption(const std::string &s, int def);
    // The end show settings every game with one has: End Show (0 never, 1
    // after a good game, the default, 2 after every game) and Good Game, the
    // level or score that makes a game a good one (def is the game's own).
    int endShowOption() { return std::clamp(findIntOption("End Show", 1), 0, 2); }
    int goodGameOption(int def) { return std::max(0, findIntOption("Good Game", def)); }
    // The two universal sizing options. 0 means "fill the panel"; see
    // FPPArcadeGameEffect::setPlayfield().
    int playfieldWidthOption()  { return findIntOption("Playfield Width", 0); }
    int playfieldHeightOption() { return findIntOption("Playfield Height", 0); }
    
    std::string modelName;    
    Json::Value config;
    int lastValues[2];
    int idx;
private:
    std::vector<std::string> cheatHist;   // the last few presses, for the cheat codes
    std::chrono::steady_clock::time_point cheatAt;
    std::set<std::string> cheatHeld;   // directions held, so an axis repeat isn't a second press
};


class FPPArcadeGameEffect : public RunningEffect {
public:
    FPPArcadeGameEffect(PixelOverlayModel *m);
    virtual ~FPPArcadeGameEffect();
    
    virtual void button(const std::string &button);

    void outputString(const std::string &s, int x, int y, int r = 255, int g = 255, int b = 255, int scl = -1);
    void outputLetter(int x, int y, char letter, int r = 255, int g = 255, int b = 255, int scl = -1);
    void drawUnderline(int x, int y, int length, int r = 255, int g = 255, int b = 255, int scl = -1);
    int centerTextX(const std::string &s, int scl = -1);
    // Largest letter scale, no bigger than `want`, at which every line fits in
    // widthPx panel pixels (0 = the model's width). Text used to be sized from
    // the matrix height, or a paddle, alone - so on a narrow region such as a
    // 60-wide half of a 120x50 matrix the pause menu drew "RESTART" 84 pixels
    // wide and ran off the edge.
    int fitTextScale(const std::vector<std::string> &lines, int want, int widthPx = 0) const;
    // Lines of 3x5 text centred on the panel over a black box, so a busy
    // playfield doesn't show through the letters. Draws as many lines as fit.
    void textBox(const std::vector<std::string> &lines, int r, int g, int b);
    // Sound effects, by name from sounds/<game>/<name>.wav (see ArcadeSound.h),
    // sent to this model's speakers. A name with no file is silent, so call it
    // for every event worth hearing.
    void sfx(const std::string &game, const std::string &name, float gain = 1.0f);
    // The end show's music for the theme (ShowTheme.h): EndShowHalloween or
    // EndShowEveryday if the game has it, else EndShow.
    void sfxEndShow(const std::string &game, float gain = 1.0f);
    void sfxStop(const std::string &game);
    void outputPixel(int x, int y, int r, int g, int b, int scl = -1);
    double consumeElapsedMs(double fallbackMs, double maxClampMs = 250.0);
    void resetFrameTimer();

    // For games that leave the copy to the lights to FPP's output loop
    // (setOverlayBufferDirty) and draw a frame in more than one pass - the
    // picture, then the score over it. That loop copies whenever it runs, so
    // it could take a frame the game was still drawing: the picture without
    // the score, which showed as the score flickering. beginFrame() withdraws
    // a finished frame FPP hasn't taken yet before drawing over it (the one
    // already on the lights stays up), and endFrame() offers the next once
    // it's complete.
    void beginFrame();
    void endFrame();
    
    // Pause menu methods
    virtual void pause();
    virtual void resume();
    virtual void restart();
    void activatePauseSelection();
    bool isPaused() const { return paused; }
    // EXIT was picked on the pause menu (see activatePauseSelection); true once.
    bool takeExit() { return exitWanted.exchange(false); }

    // For demos (ArcadeDemo.h): true while the game is on with nobody playing
    // it - Orb Pop's ambient mode, started by itself over the sequence, before
    // anyone steps up - so a demo may take the model over. While any other
    // game runs, the model is in use, buttons or not (Orb Pop is played by
    // standing in front of the Kinect). Asked under the model's effect lock.
    virtual bool waitingForPlayers() const { return false; }

    // A cheat code was entered. Button presses arrive on another thread, so
    // this only raises a flag: a game with levels takes it with takeCheat()
    // in its own update() and jumps to its last level (for an endless game,
    // the level where it is at its hardest) or the next one. Games without
    // levels ignore it.
    static constexpr int CHEAT_LAST_LEVEL = 1, CHEAT_NEXT_LEVEL = 2;
    void requestCheat(int which) { cheatPending.store(which); }
    
    // Size the logical playfield and centre it on the model. A game should
    // bound itself against pfW/pfH rather than model->getWidth()/getHeight(),
    // so that what it collides with and what outputPixel() draws can never
    // disagree - that mismatch is what let Breakout's ball travel through dead
    // panel beside its brick field.
    //
    // logicalW/logicalH are in game cells, not panel pixels; 0 (or anything
    // larger than the panel can hold) means "fill the panel", so a misconfigured
    // value degrades to full screen instead of drawing off-model.
    void setPlayfield(int logicalW, int logicalH, int scl);
    int pfW = 0;
    int pfH = 0;

    // Draw in absolute panel pixels for the lifetime of the object: zeroes the
    // transform AND widens the centring reference, so centerTextX() measures the
    // panel instead of the playfield. Zeroing the transform alone left
    // full-screen text jammed against x=0 whenever the playfield was narrower
    // than the text.
    struct PanelTransform {
        explicit PanelTransform(FPPArcadeGameEffect *eff);
        ~PanelTransform();
    private:
        FPPArcadeGameEffect *e;
        int s, ox, oy, w, h;
    };

    int scale;
    int offsetX;
    int offsetY;
protected:
    int takeCheat() { return cheatPending.exchange(0); }   // 0, or a CHEAT_ value
    void drawPauseMenu();
    virtual void handlePauseInput(const std::string &button);

    bool paused = false;
    PauseMenu pauseMenu;
    std::atomic<int> cheatPending{0};
    std::atomic<bool> exitWanted{false};

private:
    std::chrono::steady_clock::time_point lastFrameTime;
    bool firstFrame = true;
};

#endif
