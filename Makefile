#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

#---------------------------------------------------------------------------------
# ROM metadata
#---------------------------------------------------------------------------------
GAME_TITLE	:=	AoE2 DSi
GAME_SUBTITLE1	:=	Age of Empires II
GAME_SUBTITLE2	:=	daniel.ie

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
# MAXMOD_SOUNDBANK contains a directory of music and sound effect files
#---------------------------------------------------------------------------------
TARGET		:=	$(shell basename $(CURDIR))
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include
SPRITES		:=  sprites
AUDIO		:=	audio
NITRODATA	:=	nitrofiles

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv5te -mtune=arm946e-s -mthumb

CFLAGS	:=	-g -Wall -O2 -ffunction-sections -fdata-sections\
		$(ARCH)

CFLAGS	+=	$(INCLUDE) -DARM9
CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project (order is important)
#---------------------------------------------------------------------------------
LIBS	:= 	-lfat -lmm9 -lnds9


#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(LIBNDS)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export GAME_ICON :=	$(CURDIR)/build_icon.bmp

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
					$(foreach dir,$(SPRITES),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

export AUDIOFILES :=	$(foreach dir,$(AUDIO),$(wildcard $(CURDIR)/$(dir)/*))

ifneq ($(strip $(NITRODATA)),)
	export NITRO_FILES	:=	$(CURDIR)/$(NITRODATA)
endif

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*))) soundbank.bin
# All terrain + sprite data is preprocessed to data/*.bin files
# No grit-processed PNGs needed
SPRITE_FILES   :=

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES	:=	$(SPRITE_FILES:.png=.o) \
				$(addsuffix .o,$(BINFILES)) \
			$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean assets assets-game sprites

#---------------------------------------------------------------------------------
$(BUILD):
	@python3 -c "from PIL import Image; img=Image.open('icon.bmp').convert('RGB'); img.quantize(colors=16, dither=Image.Dither.FLOYDSTEINBERG).save('build_icon.bmp')"
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds build_icon.bmp

#---------------------------------------------------------------------------------
# Asset pipeline helper
# Usage:
# make assets SLP="/mnt/c/.../2.slp" OUT="sprites/villager.png" PALETTE="/mnt/c/.../pal_5.pal" CELL=32x32 COLS=4 FIT=1
#---------------------------------------------------------------------------------
assets:
	@if [ -z "$(SLP)" ]; then \
		echo "Usage: make assets SLP=<path.slp> OUT=<out.png> [PALETTE=<path.pal>] [CELL=32x32] [COLS=4] [FIT=1]"; \
		exit 1; \
	fi
	@python3 scripts/build_sprite_sheet.py \
		"$(SLP)" \
		"$(OUT)" \
		$(if $(PALETTE),--palette "$(PALETTE)",) \
		$(if $(CELL),--cell "$(CELL)",) \
		$(if $(COLS),--cols "$(COLS)",) \
		$(if $(FIT),--fit,)

#---------------------------------------------------------------------------------
# Extract game unit/building sprites from manifest
#---------------------------------------------------------------------------------
assets-game:
	@python3 scripts/build_assets_from_manifest.py assets/manifest_game.json

#---------------------------------------------------------------------------------
# Preprocess HD sprite PNGs into NDS-ready indexed binary data (data/*.bin)
# IMPORTANT: terrain tiles are indexed against the sprite palette, so terrain
# MUST be re-preprocessed whenever sprites change. Always use `make sprites`
# instead of running the scripts individually.
#---------------------------------------------------------------------------------
sprites:
	@echo "=== Preprocessing sprites (palette + indexed data) ==="
	@python3 scripts/preprocess_sprites.py
	@echo "=== Preprocessing terrain (re-indexing against sprite palette) ==="
	@python3 scripts/preprocess_terrain.py

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).nds	: 	$(OUTPUT).elf
	ndstool -c $@ -9 $< -7 $(DEVKITPRO)/calico/bin/ds7_maine.elf \
		-g "AE2D" "01" "AOE2DSI" \
		-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)"
	@echo built ... $(notdir $@)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------


#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
# soundbank generation from audio WAV files
#---------------------------------------------------------------------------------
soundbank.bin soundbank.h : $(AUDIOFILES)
	@mmutil $^ -d -osoundbank.bin -hsoundbank.h

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
