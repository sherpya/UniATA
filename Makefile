# Linux -> Windows XP SP3 x86 miniport. Original NMAKE files remain in driver/.
CROSS ?= i686-w64-mingw32-
CXX = $(CROSS)g++
CC = $(CROSS)gcc
RC = $(CROSS)windres
MC = $(CROSS)windmc
PYTHON ?= python3
DEBUG ?= 0
OUT ?= build/$(if $(filter 1,$(DEBUG)),debug,release)
DDK ?= /usr/i686-w64-mingw32/include/ddk

CPPFLAGS = -DUSE_REACTOS_DDK -DUNIATA_MINGW_XP -D_WIN32_WINNT=0x0501 \
           -DNTDDI_VERSION=0x05010000 -D_X86_ -D__USE_NTOSKRNL__ \
           -D__USE_MINGW_ANSI_STDIO=0 \
           -I$(DDK) -Idriver -I$(OUT) -include cross/compat.h
CXXFLAGS = -std=gnu++98 -nostdinc++ -O2 -march=i586 -mno-sse -mno-sse2 -mno-mmx \
           -ffreestanding -fno-builtin -fno-exceptions -fno-rtti \
           -fno-stack-protector -fno-strict-aliasing -fno-omit-frame-pointer \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -Wno-multichar -Wframe-larger-than=2048 -MMD -MP -fstack-usage
ifeq ($(DEBUG),1)
CPPFLAGS += -D_DEBUG -DDBG=1
CXXFLAGS += -g
else
CPPFLAGS += -DNDEBUG -DDBG=0
endif

SOURCES = bm_devs atacmd_map id_probe id_ata id_dma id_init id_queue id_sata id_badblock stdafx
OBJECTS = $(addprefix $(OUT)/,$(addsuffix .o,$(SOURCES))) $(OUT)/xp_compat.o

.PHONY: all check test clean FORCE
all: $(OUT)/uniata.sys

$(OUT):
	mkdir -p $@

# Rebuild if options change even when OUT is explicitly reused.
$(OUT)/config.stamp: FORCE | $(OUT)
	@printf '%s\n' '$(CXX)' '$(CPPFLAGS)' '$(CXXFLAGS)' '$(DEBUG)' > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@
	@rm -f $@.tmp

FORCE:

$(OUT)/%.o: driver/%.cpp cross/compat.h Makefile $(OUT)/config.stamp | $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OUT)/xp_compat.o: cross/xp_compat.cpp cross/compat.h Makefile $(OUT)/config.stamp | $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OUT)/messages.stamp: driver/uataerr.mc | $(OUT)
	$(MC) -h $(OUT) -r $(OUT) $<
	touch $@

$(OUT)/version.o: cross/version.rc driver/uniata_ver.h $(OUT)/messages.stamp Makefile $(OUT)/config.stamp
	$(RC) -DDEBUG=$(DEBUG) -Idriver -I$(OUT) -O coff $< $@

$(OUT)/uniata.sys: $(OBJECTS) $(OUT)/version.o
	$(CXX) -nostdlib -Wl,--subsystem,native:5.01 -Wl,--entry,_DriverEntry@8 \
	  -Wl,--major-os-version,5 -Wl,--minor-os-version,1 \
	  -Wl,--image-base,0x10000 -Wl,--file-alignment,0x200 -Wl,--section-alignment,0x1000 \
	  -Wl,--exclude-all-symbols -Wl,--enable-reloc-section -Wl,--disable-auto-import \
	  -Wl,--no-insert-timestamp -Wl,-Map,$(OUT)/uniata.map -o $@ $(OBJECTS) $(OUT)/version.o \
	  -lntoskrnl -lhal -lscsiport -lgcc

clean:
	rm -rf $(OUT)

check: all
	$(PYTHON) cross/check_pe.py $(OUT)/uniata.sys --prefix $(CROSS) $(CHECK_ARGS)

test:
	$(PYTHON) cross/test_dma_chain.py
	$(PYTHON) cross/test_dma_payload.py

-include $(OBJECTS:.o=.d)
