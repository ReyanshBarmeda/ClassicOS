#===============================================================================
# Classic OS — Native PlayStation Classic (SCPH-1000R) Build System
#
# Modes:
#   1. ARM Cross-Compilation (DEFAULT — for PlayStation Classic console):
#        make              (or: make arm, make package)
#      Requires arm-linux-gnueabihf-g++. Fails immediately if missing; NEVER
#      falls back silently to the host x86_64 compiler.
#
#   2. Host Desktop Testing (Explicit opt-in ONLY — for local Linux x86_64 UI/DB test):
#        make host         (or: make TARGET_ARCH=host)
#      Builds into build-host/classic_os so host binaries never mix with ARM builds.
#===============================================================================

TARGET       := classic_os
SRC_DIR      := src
INC_DIR      := include

# Automatically select host architecture when 'make host' is invoked directly
ifneq ($(filter host,$(MAKECMDGOALS)),)
    TARGET_ARCH := host
endif
TARGET_ARCH  ?= armhf

SOURCES      := $(SRC_DIR)/main.cpp \
                $(SRC_DIR)/app.cpp \
                $(SRC_DIR)/config.cpp \
                $(SRC_DIR)/renderer.cpp \
                $(SRC_DIR)/texture_cache.cpp \
                $(SRC_DIR)/input.cpp \
                $(SRC_DIR)/library.cpp \
                $(SRC_DIR)/launcher.cpp \
                $(SRC_DIR)/ui_dashboard.cpp

ifeq ($(TARGET_ARCH),armhf)
    BUILD_DIR      ?= build
    CROSS_PREFIX   ?= arm-linux-gnueabihf-
    CROSS_CXX_PATH := $(shell command -v $(CROSS_PREFIX)g++ 2>/dev/null)
    # Enforce cross-compiler presence on all ARM build goals (except 'make clean')
    ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
        ifeq ($(CROSS_CXX_PATH),)
            $(error ARM cross-compiler '$(CROSS_PREFIX)g++' not found in PATH. Refusing to fall back to host compiler. Install g++-arm-linux-gnueabihf in your Linux VM for PlayStation Classic ARM builds, or run 'make host' (TARGET_ARCH=host) for local desktop testing)
        endif
    endif
    CXX            := $(CROSS_PREFIX)g++
    STRIP          := $(CROSS_PREFIX)strip
    READELF        := $(CROSS_PREFIX)readelf
    ARCH_FLAGS     := -march=armv8-a+crc -mtune=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard
    LINK_RPATH     := -Wl,-rpath,/usr/sony/lib:/media/project_eris/lib -static-libstdc++ -static-libgcc -Wl,--allow-shlib-undefined
    SYS_LIBDIR     ?= /usr/lib/arm-linux-gnueabihf
else ifeq ($(TARGET_ARCH),host)
    BUILD_DIR      ?= build-host
    CXX            ?= g++
    STRIP          ?= strip
    READELF        ?= readelf
    ARCH_FLAGS     ?=
    LINK_RPATH     ?=
    SYS_LIBDIR     ?= /usr/lib/x86_64-linux-gnu
else
    $(error Unsupported TARGET_ARCH '$(TARGET_ARCH)'. Use TARGET_ARCH=armhf (default) or TARGET_ARCH=host)
endif

OBJECTS      := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

# Resolve SDL2 / libpng16 / sqlite3 link flags (supports both -l<name> dev symlinks and direct SONAMEs)
ifneq ($(wildcard $(SYS_LIBDIR)/libSDL2-2.0.so.0),)
    LDLIBS ?= $(SYS_LIBDIR)/libSDL2-2.0.so.0 $(SYS_LIBDIR)/libpng16.so.16 $(SYS_LIBDIR)/libsqlite3.so.0 -lpthread -ldl -lm
endif
LDLIBS       ?= -lSDL2 -lpng16 -lsqlite3 -lpthread -ldl -lm

CXXFLAGS     := -std=c++17 -O2 -Wall -Wextra -fPIE -ffunction-sections -fdata-sections $(ARCH_FLAGS) -I$(INC_DIR) $(EXTRA_CXXFLAGS)
LDFLAGS      := -pie -Wl,--gc-sections $(LINK_RPATH) $(EXTRA_LDFLAGS)

.PHONY: all arm host clean package verify-abi

all: $(BUILD_DIR)/$(TARGET)

arm: $(BUILD_DIR)/$(TARGET)

host: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

package:
ifneq ($(TARGET_ARCH),armhf)
	$(error Refusing to package a non-ARM binary (TARGET_ARCH=$(TARGET_ARCH)). Run 'make package' with TARGET_ARCH=armhf)
endif
	$(MAKE) TARGET_ARCH=armhf BUILD_DIR=build $(BUILD_DIR)/$(TARGET)
	@if ! $(READELF) -h $(BUILD_DIR)/$(TARGET) | grep -q "Machine:.*ARM"; then \
		echo "[ERROR] $(BUILD_DIR)/$(TARGET) is not a 32-bit ARM ELF binary! Aborting package step."; \
		exit 1; \
	fi
	$(STRIP) $(BUILD_DIR)/$(TARGET)
	mkdir -p package/opt/classic_os/assets
	cp -f $(BUILD_DIR)/$(TARGET) package/opt/classic_os/$(TARGET)
	cp -rf assets/* package/opt/classic_os/assets/ 2>/dev/null || true
	chmod +x package/opt/classic_os/$(TARGET)
	chmod +x package/opt/classic_os/launch.sh
	chmod +x package/opt/classic_os/bootctl.sh
	chmod +x package/etc/project_eris/FUNC/0095_classic_os_boot.sh
	chmod +x package/etc/project_eris/SUP/launchers/classic_os/launch.sh

verify-abi: $(BUILD_DIR)/$(TARGET)
	$(READELF) -h -d -V -A $(BUILD_DIR)/$(TARGET)

clean:
	rm -rf build build-host build-arm
