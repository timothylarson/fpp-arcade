#!/bin/bash
# Renders a game's end show (src/<Game>Show.h) to a contact sheet, without FPP
# or the wall:
#
#     tools/shows/preview.sh SnakeShow [width height every_ms out.png start_ms]
#
# The header must define  void preview(ShowConfig &cfg, std::unique_ptr<ShowStory> &story)
# inside  namespace <Game>Show  (made up numbers are fine). Needs clang++ and
# python3 with Pillow.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../src"
NAME=$1; W=${2:-60}; H=${3:-25}; EVERY=${4:-1000}; OUT=${5:-$NAME.png}; FROM=${6:-0}
WORK=$(mktemp -d)
mkdir -p "$WORK/overlays"
cp "$SRC/ArcadeShow.h" "$SRC/ArcadeCanvas.h" "$SRC/$NAME.h" "$WORK/"
cp "$HERE/stub_model.h" "$WORK/overlays/PixelOverlayModel.h"
cp "$HERE/stub_arcade.h" "$WORK/FPPArcade.h"
# The real font, taken from FPPArcade.cpp.
{ echo '#include "FPPArcade.h"'; echo '#include <array>'; echo '#include <cstring>'; echo '#include <map>'
  awk '/^static const std::map<uint8_t, std::vector<uint8_t>> LETTERS/,/^bool arcadeGlyph\(char c, int x, int y\) \{/' "$SRC/FPPArcade.cpp"
  awk '/^bool arcadeGlyph\(char c, int x, int y\) \{/{f=1} f{print} f&&/^}$/{exit}' "$SRC/FPPArcade.cpp" | tail -n +2
} > "$WORK/glyph.cpp"
cat > "$WORK/main.cpp" <<CPP
#include "$NAME.h"
#include <cstdio>
#include <cstdlib>
int main() {
    ShowConfig cfg;
    std::unique_ptr<ShowStory> story;
    $NAME::preview(cfg, story);
    PixelOverlayModel m($W, $H);
    ArcadeCanvas::Canvas c;
    c.resize($W, $H);
    ArcadeShow show;
    show.start($W, $H, cfg, std::move(story));
    double next = $FROM;
    int n = 0;
    while (!show.over()) {
        show.step(50, c);
        if (show.elapsedMs() >= next) {
            c.writeTo(&m);
            char name[64];
            snprintf(name, sizeof name, "f%04d.rgb", n++);
            FILE *f = fopen(name, "wb");
            fwrite(m.px.data(), 1, m.px.size(), f);
            fclose(f);
            next += $EVERY;
        }
    }
    printf("%d frames\n", n);
}
CPP
(cd "$WORK" && clang++ -std=c++17 -O1 -Wall -I. main.cpp glyph.cpp -o show && ./show)
python3 "$HERE/sheet.py" "$WORK" $W $H $EVERY $FROM "$OUT"
rm -rf "$WORK"
echo "$OUT"
