# The FPP Arcade host (see README.md): make, then build/arcade-host.
# make SANITIZE=1 builds build-asan/arcade-host with the address and
# undefined-behaviour checkers, for chasing crashes.
#
# Paths stay relative: make can't cope with the spaces in this project's.

SRC := $(filter-out ../../src/FPPArcade.cpp,$(wildcard ../../src/*.cpp))
BUILD := build$(if $(SANITIZE),-asan)
OBJS := $(patsubst ../../src/%.cpp,$(BUILD)/%.o,$(SRC)) $(BUILD)/host.o $(BUILD)/HostJson.o

CXX := clang++
# SDL3 from Homebrew (brew install sdl3).
SDL := $(shell brew --prefix sdl3 2>/dev/null || echo /opt/homebrew)
CXXFLAGS := -std=gnu++2b -O1 -g -DPLATFORM_OSX -Istub -I../../src -I$(SDL)/include -MMD -MP \
            -Wall -Wno-unused-variable -Wno-unused-function -Wno-unused-private-field -Wno-sign-compare
LDFLAGS :=
LDLIBS := -L$(SDL)/lib -lSDL3
ifdef SANITIZE
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

$(BUILD)/arcade-host: $(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(BUILD)/%.o: ../../src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf build build-asan

.PHONY: clean
-include $(OBJS:.o=.d)
