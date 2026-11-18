# Makefile for the 3DSDirStat homebrew (devkitPro / devkitARM).
# Produces a .3dsx + .smdh via the modern 3ds_rules toolchain.

#---------------------------------------------------------------------------------
# Locate devkitPro. Override DEVKITPRO in your environment to point at your install.
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
DEVKITPRO := $(shell ls -d /opt/devkitpro /usr/local/devkitpro ~/devkitPro 2>/dev/null | head -n1)
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO not found. Set it: export DEVKITPRO=/opt/devkitPro")
endif

#---------------------------------------------------------------------------------
# Pull in the devkitPro rules (defines %.elf, %.3dsx, %.smdh and the compiler PATH).
#---------------------------------------------------------------------------------
include $(DEVKITPRO)/devkitARM/3ds_rules

# Force `make` with no target to build the app (the included rules may define
# an earlier normal target that would otherwise become the default goal).
.DEFAULT_GOAL := all

TARGET		:=	$(notdir $(CURDIR))
OUTPUT		:=	$(TARGET)

SRC		:=	$(wildcard src/*.c)
OFILES		:=	$(SRC:.c=.o)

# base_rules emits each dependency file at $(DEPSDIR)/$*.d, where $* keeps the
# src/ prefix (e.g. src/colors.d). Point DEPSDIR at the project root so the .d
# files land next to the .o files in src/, which already exists.
DEPSDIR		:=	$(CURDIR)

#---------------------------------------------------------------------------------
# The 3ds_rules %.elf rule has no prerequisites of its own, so force the objects
# to be built before linking.
#---------------------------------------------------------------------------------
$(OUTPUT).elf: $(OFILES)

#---------------------------------------------------------------------------------
# Toolchain / ABI. The libctru archives carry Tag_ABI_VFP_args: VFP registers, so
# everything must link with hard-float + VFP. -march=armv6k is required so the crt
# objects (3dsx_crt0.o, crti.o, ...) are on the object search path.
#---------------------------------------------------------------------------------
LD			:=	arm-none-eabi-gcc
LDFLAGS		:=	-specs 3dsx.specs -march=armv6k -marm -mfloat-abi=hard -mfpu=vfp
LIBPATHS	:=	-L$(CTRULIB)/lib
LIBS		:=	-lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# Build settings.
#---------------------------------------------------------------------------------
PRODUCTION ?= release
DEBUG      ?= 0

ifeq ($(PRODUCTION),release)
    CFLAGS += -O2 -DPRODUCTION
else
    CFLAGS += -g -DDEBUG
endif

# Scripted UI test build: replays sdmc:/logs/script.txt and dumps the real LCD
# framebuffers to sdmc:/logs/*.ppm (see src/debug.h).
ifeq ($(SCRIPT),1)
    CFLAGS += -DSCRIPT
endif

# Debug-gdb build: add -g (keeping -O2) so gdb can resolve guest function names
# and source lines when catching the crash. Timing-faithful to the production build.
ifeq ($(GDB),1)
    CFLAGS += -g
endif

CFLAGS		+=	-march=armv6k -marm -mfloat-abi=hard -mfpu=vfp \
				-Wall -Wextra -std=c11 -I$(CURDIR)/src -I$(CTRULIB)/include

CXXFLAGS	:=	$(CFLAGS)
LDLIBS		:=	$(LIBS)

#---------------------------------------------------------------------------------
# .smdh metadata.
#---------------------------------------------------------------------------------
APP_TITLE		:=	3DSDirStat
APP_DESCRIPTION	:=	WinDirStat-like disk usage treemap for the 3DS
APP_AUTHOR		:=	3DSDirStat
APP_ICON		:=	$(CTRULIB)/default_icon.png

#---------------------------------------------------------------------------------
# Targets.
#---------------------------------------------------------------------------------
all: $(OUTPUT).3dsx $(OUTPUT).smdh
	@echo "built $(OUTPUT).3dsx + $(OUTPUT).smdh"

clean:
	@rm -f src/*.d $(OFILES) $(OUTPUT).elf $(OUTPUT).lst $(OUTPUT).3dsx $(OUTPUT).smdh

.PHONY: all clean
