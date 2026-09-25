PATH := /c/devkitPro/devkitARM/bin:/c/devkitPro/tools/bin:$(PATH)
export PATH
DEVKITPRO := /c/devkitPro
DEVKITARM := /c/devkitPro/devkitARM
include $(DEVKITARM)/gba_rules

TARGET   := speck
SRC      := source/main.c
CFLAGS   := -mthumb -mthumb-interwork -O2 -Wall -fno-strict-aliasing \
            -I$(DEVKITPRO)/libtonc/include
# make DEBUG=1: every level selectable on the title (for testing). Rebuild without it
# before shipping: the flag is baked into both speck.gba and web/speck.gba.
ifdef DEBUG
CFLAGS   += -DSPECK_UNLOCK_ALL
endif
# make DEBUG=1 START=8: title opens on level 8.
ifdef START
CFLAGS   += -DSPECK_UNLOCK_ALL -DSPECK_START=$(START)
endif
LDFLAGS  := -specs=gba.specs -mthumb -mthumb-interwork \
            -L$(DEVKITPRO)/libtonc/lib -ltonc

all: web/$(TARGET).gba

LEVELS := levels/order.txt $(wildcard levels/*.txt)

# Level maps: levels/*.txt -> source/levels.h (lint first, so bad maps never build).
source/levels.h: $(LEVELS) tools/gen_levels.py tools/lint_levels.py tools/levels_io.py
	python tools/lint_levels.py
	python tools/gen_levels.py

$(TARGET).elf: $(SRC) source/levels.h
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

$(TARGET).gba: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	gbafix $@ -tSPECK

# The EmulatorJS page loads web/speck.gba; keep it in sync with every build.
web/$(TARGET).gba: $(TARGET).gba
	cp $< $@

# Contact sheet of every level after its starting sand settles.
preview:
	python tools/preview_levels.py levels_preview.png

clean:
	rm -f $(TARGET).elf $(TARGET).gba web/$(TARGET).gba

.PHONY: all clean preview
