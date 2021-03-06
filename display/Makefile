# target config
TARGET = LPC11xx
FLASH = 32K
SRAM = 8K
DEBUG = FALSE

# paths and sources
ROOT = $(abspath .)
CODEBASEDIR = ./microbuilder-codebase
BIN = ./bin

VPATH = . $(CODEBASEDIR)
OBJS = src/main src/lcd src/utils src/font src/font_data src/draw \
		src/unicode src/graphs src/tables src/touch src/comm src/time \
		src/buffer
OBJS += lpc1xxx/LPC11xx_handlers lpc1xxx/LPC1xxx_startup
OBJS += core/cpu/cpu \
		core/systick/systick \
		core/gpio/gpio

# toolchain config
TARGET_TOOLCHAIN = arm-none-eabi-
AS = $(TARGET_TOOLCHAIN)as
CC = $(TARGET_TOOLCHAIN)gcc
LD = $(TARGET_TOOLCHAIN)gcc
SIZE = $(TARGET_TOOLCHAIN)size
OBJCOPY = $(TARGET_TOOLCHAIN)objcopy
OBJDUMP = $(TARGET_TOOLCHAIN)objdump
GENFONT = ../utils/genfont.py

FONTDATA = data/*.glyph

# flags
INCLUDE_PATHS = -I$(ROOT) -I$(ROOT)/inc -I$(CODEBASEDIR)/core -I$(CODEBASEDIR) -I../

# linker stuff
LD_SCRIPT = $(CODEBASEDIR)/lpc1xxx/linkscript.ld
LD_TEMP = $(BIN)/memory.ld
CPU_TYPE = cortex-m0

BASEFLAGS = -Wall -Wextra -Werror -D__LITTLE_ENDIAN
OPTFLAGS = -Os

ifeq (TRUE,$(DEBUG))
    COMMONFLAGS = -c -g -O0 $(INCLUDE_PATHS) $(BASEFLAGS) -mthumb \
        -ffunction-sections -fdata-sections -fmessage-length=0 \
        -mcpu=$(CPU_TYPE)
    CFLAGS = $(COMMONFLAGS) -DTARGET=$(TARGET) -fno-builtin
    ASFLAGS = $(COMMONFLAGS) -D__ASSEMBLY__ -x assembler-with-cpp
else
    COMMONFLAGS = -c -g $(OPTFLAGS) $(INCLUDE_PATHS) $(BASEFLAGS) \
        -mthumb -ffunction-sections -fdata-sections -fmessage-length=0 \
        -mcpu=$(CPU_TYPE)
    CFLAGS = $(COMMONFLAGS) -DTARGET=$(TARGET) -fno-builtin
    ASFLAGS = $(COMMONFLAGS) -D__ASSEMBLY__ -x assembler-with-cpp
endif

CFLAGS += -std=c11
LDFLAGS = -nostartfiles -mcpu=$(CPU_TYPE) -mthumb -Wl,--gc-sections
LDLIBS = -lm
OCFLAGS = --strip-unneeded

# autogenerated
OBJ_FILES = $(addprefix bin/,$(addsuffix .o,$(OBJS)))
SRC_FILES = $(addsuffix .c,$(OBJS))
FIRMWAREBASE = bin/firmware
FAKEDEPS = inc/lpc111x.h inc/projectconfig.h inc/sysdefs.h
# make all files depend on project config
COMMONDEPS = src/config.h

default: bin/firmware.hex

inc/lpc111x.h:
	ln -s ../$(CODEBASEDIR)/lpc111x.h $@

inc/sysdefs.h:
	ln -s ../$(CODEBASEDIR)/sysdefs.h $@

inc/projectconfig.h:
	ln -s ../src/config.h $@

src/generated_font_data.inc: $(GENFONT)
	$(GENFONT) --no-flip \
		-r 0x20 0x7e \
		-r 0xa1 0xff \
		-x 0xad \
		-c 0xfffd \
		-c 0x221e 0x2026 0x202f 0x2714 0x2718 0x2013 \
		-r 0x2588 0x258f \
		--section ".text#" \
		-- \
		"DejaVu Sans" 12 dejavu_sans_12px > $@
	$(GENFONT) --no-flip --bfseries \
		-r 0x20 0x7e \
		-r 0xa1 0xff \
		-x 0xad \
		-c 0xfffd \
	        -c 0x2588 \
		-c 0x2026 0x2013 \
		--section ".text#" \
		-- \
		"DejaVu Sans" 12 dejavu_sans_12px_bf >> $@
	$(GENFONT) --no-flip --bfseries \
		-r "'0'" "'9'" \
		-c "':'" \
	  --space-width 6 \
		-c 0xfffd \
		-l "data/glyphs/Cantarell/20px/0x0000003a.glyph" \
		-- \
		"Cantarell" 20 cantarell_20px_bf >> $@
	$(GENFONT) --no-flip \
		-r "'A'" "'Z'" \
		-r "'a'" "'z'" \
		-r "'0'" "'9'" \
		-c "'.'" "':'" "'°'" \
		-c "'ä'" "'ö'" "'ü'" \
	  -c "'%'" \
		-c 0xfffd \
		-c 0x2026 \
		-c 0x202f \
		-c 0x2013 \
		-l "data/glyphs/DejaVu Sans/9px/0x00000033.glyph" \
		-l "data/glyphs/DejaVu Sans/9px/0x000000b0.glyph" \
		--\
		"DejaVu Sans" 9 dejavu_sans_9px >> $@

$(FIRMWAREBASE).elf: $(OBJ_FILES) $(FAKEDEPS) $(COMMONDEPS)
	-@echo "MEMORY" > $(LD_TEMP)
	-@echo "{" >> $(LD_TEMP)
	-@echo "  flash(rx): ORIGIN = 0x00000000, LENGTH = $(FLASH)" >> $(LD_TEMP)
	-@echo "  sram(rwx): ORIGIN = 0x10000000, LENGTH = $(SRAM)" >> $(LD_TEMP)
	-@echo "}" >> $(LD_TEMP)
	-@echo "INCLUDE $(LD_SCRIPT)" >> $(LD_TEMP)
	$(LD) $(LDFLAGS) -T $(LD_TEMP) -o $@ $(OBJ_FILES) $(LDLIBS)
	$(SIZE) $@

$(FIRMWAREBASE).hex: $(FIRMWAREBASE).elf
	$(OBJCOPY) $(OCFLAGS) -O ihex $< $@

bin/src/font_data.o: src/font_data.c src/font.h src/font_data.h src/generated_font_data.inc
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

bin/src/touch.c: src/touch.c src/touch.h src/fp11_4.h src/config.h $(COMMONDEPS) $(FAKEDEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

bin/src/comm.c: src/comm.c src/comm.h ../common/comm.h src/comm_internal.h src/comm_*.inc.c $(COMMONDEPS) $(FAKEDEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

bin/%.o: %.c %.h $(FAKEDEPS) $(COMMONDEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

bin/%.o: %.c $(FAKEDEPS) $(COMMONDEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

bin/%.o: %.s $(FAKEDEPS) $(COMMONDEPS)
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

clean:
	rm -f $(FAKEDEPS) src/generated_font_data.inc
	rm -rf bin
	mkdir bin

.PHONY: clean
