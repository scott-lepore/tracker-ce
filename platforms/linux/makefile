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

CPP_SRCS    := $(wildcard source/*.cpp)
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

$(OBJ_DIR)/%.o: source/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(DEFINES) $(INCLUDES) $(CXXFLAGS) -c -o "$@" "$<"

$(BUILD_DIR)/$(TARGET): $(OBJS) $(TRACKER_DIR)/build-LINUX-ORIGINAL/libTracker.a
	$(CXX) $(LDFLAGS) -o "$@" $(OBJS) $(LIBS)

build: $(BUILD_DIR)/$(TARGET)

clean:
	-$(RM) $(BUILD_DIR)

-include $(DEPS)
.PHONY: build clean
