SRCDIR ?= /opt/fpp/src
include $(SRCDIR)/makefiles/common/setup.mk
include $(SRCDIR)/makefiles/platform/*.mk

all: libfpp-arcade.$(SHLIB_EXT)
debug: all

CFLAGS+=-I.
OBJECTS_fpp_arcade_so += src/FPPArcade.o src/FPPTetris.o src/FPPPong.o src/FPPSnake.o src/FPPBreakout.o src/FPPBreakoutVersus.o src/FPPFrogger.o src/FPPOrbPop.o src/FPPAsteroidOrbit.o src/FPPMazeChase.o src/FPPBarrelClimb.o src/FPPStarDefenders.o src/FPPShapeFit.o src/FPPSimpleGames.o src/FPPStickBridge.o src/FPPSkyJump.o src/FPPReindeerRun.o src/FPPRoadRace.o src/FPPSnowballFight.o src/ArcadeSound.o
LIBS_fpp_arcade_so += -L$(SRCDIR) -lfpp -ljsoncpp -ldrogon -ltrantor
CXXFLAGS_src/FPPArcade.o += -I$(SRCDIR)

# SDL is only used for game-controller input on macOS, where there is no
# /dev/input/jsN. Linux/Pi use the raw Linux joystick API and don't link SDL.
ifeq '$(ARCH)' 'OSX'
LIBS_fpp_arcade_so += -lSDL2
else
# Sound effects go straight to ALSA (a USB sound card); see src/ArcadeSound.h.
LIBS_fpp_arcade_so += -lasound
endif


%.o: %.cpp Makefile
	$(CCACHE) $(CC) $(CFLAGS) $(CXXFLAGS) $(CXXFLAGS_$@) -c $< -o $@

# Linked beside the old library and renamed over it, so a running fppd keeps
# the copy it has loaded until it restarts. Linking straight onto it rewrote
# the file fppd was running from, and it died of a bus error.
libfpp-arcade.$(SHLIB_EXT): $(OBJECTS_fpp_arcade_so) $(SRCDIR)/libfpp.$(SHLIB_EXT)
	$(CCACHE) $(CC) -shared $(CFLAGS_$@) $(OBJECTS_fpp_arcade_so) $(LIBS_fpp_arcade_so) $(LDFLAGS) -o $@.new
	mv -f $@.new $@

clean:
	rm -f libfpp-arcade.so $(OBJECTS_fpp_arcade_so)

