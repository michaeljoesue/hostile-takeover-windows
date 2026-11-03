# Windows (MinGW-w64) build of Hostile Takeover.
#
# Cross-compile from Linux/WSL:   make REL=1
# Native on Windows with MSYS2:   make REL=1 CROSS=   (inside the MINGW32 shell)
#
# Needs SDL2 (mingw dev package). Point SDL2DIR at it.
#
# NOTE: the game is built as a 32-bit (i686) Windows program on purpose. The
# code base dates from 32-bit Palm/Windows CE days and in many places assumes
# that 'long' can hold a pointer. That is true on 32-bit Windows and on 64-bit
# Linux/macOS, but NOT on 64-bit Windows (where long is 32 bits). 32-bit
# programs run natively on Windows 11 (x64 and ARM64).

BINARY = HostileTakeover.exe
HTROOT := ../../..

ARCH ?= i686-w64-mingw32
CROSS ?= $(ARCH)-
CC  = $(CROSS)gcc-posix
CXX = $(CROSS)g++-posix
LD  = $(CROSS)g++-posix
WINDRES = $(CROSS)windres
ifeq ($(CROSS),)
CC = gcc
CXX = g++
LD = g++
WINDRES = windres
endif

SDL2DIR ?= ../../../../deps/SDL2-2.30.9/$(ARCH)

CPPFLAGS += -w -fpermissive -fno-delete-null-pointer-checks -fno-strict-aliasing -fwrapv -Wno-write-strings -fsigned-char \
-I../../../base -I../../../mpshared -I../../../inc \
-I$(SDL2DIR)/include -I$(SDL2DIR)/include/SDL2 \
-I../../../ -I../../ -I../ \
-DSDL \
-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0601

LDFLAGS = -static -static-libgcc -static-libstdc++ -mwindows \
-L$(SDL2DIR)/lib \
-lmingw32 -lSDL2main -lSDL2 \
-lws2_32 \
-lshlwapi -lshell32 -lole32 -lwinmm -limm32 -lversion -lsetupapi -loleaut32 -luuid -lgdi32 -luser32

ifeq ($(REL),1)
OUTDIR = release
CPPFLAGS += -O2
LDFLAGS += -s
else
OUTDIR = debug
CPPFLAGS += -g -O0 -DDEV_BUILD -DDEBUG_LOGGING -DDEBUG
LDFLAGS := $(filter-out -mwindows,$(LDFLAGS)) -mconsole
endif

GAME_FILES := $(basename $(notdir $(wildcard $(HTROOT)/game/*.cpp)))
GAME_FILES := $(filter-out SocTransport miscgraphics hashtablecode filepdbreader DebugHelpers, $(GAME_FILES))
BASE_FILES := $(basename $(notdir $(wildcard $(HTROOT)/base/*.cpp)))
MPSHARED_FILES := $(basename $(notdir $(wildcard $(HTROOT)/mpshared/*.cpp)))
INC_FILES := rip
YAJL_C_FILES := $(basename $(notdir $(wildcard $(HTROOT)/yajl/src/*.c)))
YAJL_CPP_FILES := $(basename $(notdir $(wildcard $(HTROOT)/yajl/wrapper/*.cpp)))
YAJL_CPP_FILES := $(filter-out jsonbuilder_test, $(YAJL_CPP_FILES))
SDL_FILES := $(basename $(notdir $(wildcard $(HTROOT)/game/sdl/*.cpp)))
WIN_FILES := $(basename $(notdir $(wildcard $(HTROOT)/game/sdl/win/*.cpp)))

OBJS := \
$(GAME_FILES:%=$(OUTDIR)/game/%.o) \
$(BASE_FILES:%=$(OUTDIR)/base/%.o) \
$(MPSHARED_FILES:%=$(OUTDIR)/mpshared/%.o) \
$(INC_FILES:%=$(OUTDIR)/inc/%.o) \
$(YAJL_CPP_FILES:%=$(OUTDIR)/yajl/%.o) \
$(YAJL_C_FILES:%=$(OUTDIR)/yajl/%.o) \
$(SDL_FILES:%=$(OUTDIR)/sdl/%.o) \
$(WIN_FILES:%=$(OUTDIR)/win/%.o) \
$(OUTDIR)/win/resource.o

CPPFLAGS += -MMD -MP

all: $(OUTDIR)/$(BINARY)

clean:
	rm -rf $(OUTDIR)

$(OUTDIR)/$(BINARY): $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/game/%.o: ../../%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -c $< -o $@
$(OUTDIR)/base/%.o: ../../../base/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -c $< -o $@
$(OUTDIR)/mpshared/%.o: ../../../mpshared/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -c $< -o $@
$(OUTDIR)/inc/%.o: ../../../inc/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -c $< -o $@
$(OUTDIR)/yajl/%.o: ../../../yajl/wrapper/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -c $< -o $@
$(OUTDIR)/yajl/%.o: ../../../yajl/src/%.c
	@mkdir -p $(@D)
	$(CC) $(filter-out -fpermissive,$(CPPFLAGS)) -c $< -o $@
$(OUTDIR)/sdl/%.o: ../%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -c $< -o $@
$(OUTDIR)/win/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -c $< -o $@
$(OUTDIR)/win/resource.o: resource.rc ../../win/app.ico app.manifest
	@mkdir -p $(@D)
	$(WINDRES) resource.rc -O coff -o $@

-include $(OBJS:.o=.d)

.PHONY: all clean
