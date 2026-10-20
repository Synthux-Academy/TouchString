# Project Name
TARGET = TouchString

# Enabling DEBUG disables USB MIDI
# DEBUG = 1

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES = TouchString.cpp $(wildcard touch/*.cpp) $(wildcard string/*.cpp) $(wildcard ui/*.cpp)
C_INCLUDES = -Ilib/ -Icommon/

# Library Locations
LIBDAISY_DIR = lib/libDaisy/
DAISYSP_DIR = lib/DaisySP/

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

CPP_STANDARD = -std=gnu++17

# Bare-metal firmware has no OS loader enforcing W^X on ELF program
# headers, so the RWX LOAD segment warning (binutils >= 2.39) is cosmetic.
LDFLAGS += -Wl,--no-warn-rwx-segments

libs:
	cd $(LIBDAISY_DIR) && $(MAKE)
	cd $(DAISYSP_DIR) && $(MAKE)

clean-libs:
	cd $(LIBDAISY_DIR) && $(MAKE) clean
	cd $(DAISYSP_DIR) && $(MAKE) clean