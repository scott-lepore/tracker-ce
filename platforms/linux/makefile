# Linux host platform for the Tracker core (headless stage).
#
# Variables:
#   TRACKER_DIR=../../core   core sources (libTracker.a built with PLATFORM=LINUX)
#   M32=1                    32-bit build (default, matches ARM struct layout)
#   DEBUG=1                  -Og, asserts, LOG output

TRACKER_DIR ?= ../../core
M32         ?= 1
-include ../../version.mk
BUILD_ID_STR ?= $(BUILD_TAG)
TARGET      := tracker-linux

.DEFAULT_GOAL := build
BUILD_DIR   := build
OBJ_DIR     := $(BUILD_DIR)/obj
RM          := rm -rf

CPP_SRCS    := $(wildcard source/*.cpp) $(wildcard source/eve/*.cpp)
OBJS        := $(patsubst source/%.cpp,$(OBJ_DIR)/%.o,$(CPP_SRCS))
DEPS        := $(OBJS:.o=.d)

INCLUDES    := -I./source -I$(TRACKER_DIR)/include -I$(TRACKER_DIR)/config
DEFINES     := -DPLATFORM_LINUX -DBUILD_ID=\"$(BUILD_ID_STR)\"

ARCHFLAGS   :=
ifeq ($(M32), 1)
ARCHFLAGS   += -m32 -msse2 -mfpmath=sse -malign-double
endif

CXXFLAGS    := -MMD -MP -g -Wall -Wextra -std=gnu++20 -fno-rtti -fno-exceptions \
               -fshort-enums -funsigned-char -pthread -Wno-psabi $(ARCHFLAGS)
ifdef DEBUG
CXXFLAGS    += -Og -DDEBUG
else
CXXFLAGS    += -O2 -DNDEBUG
endif

LDFLAGS     := $(ARCHFLAGS) -pthread
LIBS        := -L$(TRACKER_DIR)/build-LINUX-ORIGINAL -lTracker -lm

# ALSA: headers from libasound2-dev (any arch); for the 32-bit build link the
# runtime .so directly so libasound2-dev:i386 is not required.
ifneq ($(wildcard /usr/include/alsa/asoundlib.h),)
DEFINES     += -DHAVE_ALSA
ifeq ($(M32), 1)
ALSA_LIB    := $(firstword $(wildcard /usr/lib/i386-linux-gnu/libasound.so /usr/lib/i386-linux-gnu/libasound.so.2))
else
ALSA_LIB    := -lasound
endif
LIBS        += $(ALSA_LIB)
endif

# X11 window for the emulated display (same trick: link the runtime .so.6)
ifneq ($(wildcard /usr/include/X11/Xlib.h),)
DEFINES     += -DHAVE_X11
ifeq ($(M32), 1)
X11_LIB     := $(firstword $(wildcard /usr/lib/i386-linux-gnu/libX11.so /usr/lib/i386-linux-gnu/libX11.so.6))
else
X11_LIB     := -lX11
endif
LIBS        += $(X11_LIB)
endif

$(OBJ_DIR)/%.o: source/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(DEFINES) $(INCLUDES) $(CXXFLAGS) -c -o "$@" "$<"

# Rebuild the file that embeds BUILD_ID whenever the build tag changes.
BUILD_ID_STAMP := $(OBJ_DIR)/build_id.$(BUILD_ID_STR)
$(BUILD_ID_STAMP):
	@mkdir -p $(dir $@)
	@rm -f $(OBJ_DIR)/build_id.*
	@touch "$@"
$(OBJ_DIR)/PlatformImpl.o: $(BUILD_ID_STAMP)

$(BUILD_DIR)/$(TARGET): $(OBJS) $(TRACKER_DIR)/build-LINUX-ORIGINAL/libTracker.a
	$(CXX) $(LDFLAGS) -o "$@" $(OBJS) $(LIBS)

build: $(BUILD_DIR)/$(TARGET)

clean:
	-$(RM) $(BUILD_DIR)

-include $(DEPS)
.PHONY: build clean
