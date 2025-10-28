SRCDIR ?= /opt/fpp/src
include $(SRCDIR)/makefiles/common/setup.mk
include $(SRCDIR)/makefiles/platform/*.mk

all: libfpp-arcade.$(SHLIB_EXT)
debug: all

CFLAGS+=-I.
OBJECTS_fpp_arcade_so += src/FPPArcade.o src/FPPTetris.o src/FPPPong.o src/FPPSnake.o src/FPPBreakout.o src/FPPFrogger.o
LIBS_fpp_arcade_so += -L$(SRCDIR) -lfpp -ljsoncpp -ldrogon -ltrantor
CXXFLAGS_src/FPPArcade.o += -I$(SRCDIR)

# SDL is only used for game-controller input on macOS, where there is no
# /dev/input/jsN. Linux/Pi use the raw Linux joystick API and don't link SDL.
ifeq '$(ARCH)' 'OSX'
LIBS_fpp_arcade_so += -lSDL2
endif


%.o: %.cpp Makefile
	$(CCACHE) $(CC) $(CFLAGS) $(CXXFLAGS) $(CXXFLAGS_$@) -c $< -o $@

libfpp-arcade.$(SHLIB_EXT): $(OBJECTS_fpp_arcade_so) $(SRCDIR)/libfpp.$(SHLIB_EXT)
	$(CCACHE) $(CC) -shared $(CFLAGS_$@) $(OBJECTS_fpp_arcade_so) $(LIBS_fpp_arcade_so) $(LDFLAGS) -o $@

clean:
	rm -f libfpp-arcade.so $(OBJECTS_fpp_arcade_so)

