TARGET_NAME := epub

MIPS_MUSL ?= /home/Sajnaps/toolchains/mipsel-linux-muslsf-cross/bin/mipsel-linux-muslsf-
MIPS_ELF  ?= /opt/frog-toolchain/mipsel-mti-elf_stable/bin/mipsel-mti-elf-
QEMU      ?= /home/Sajnaps/toolchains/qemu/usr/bin/qemu-mipsel
SYSROOT   ?= /home/Sajnaps/toolchains/mipsel-linux-muslsf-cross/mipsel-linux-muslsf
MULTICORE ?= /home/Sajnaps/gb300/sf2000_multicore

PLATFORM ?= $(platform)
CFLAGS_COMMON := -std=gnu11 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_TIME
SRC := src/util.c src/xml.c src/epub.c src/css.c src/fonts.c src/fonts_data.c \
       src/text.c src/image.c src/layout.c src/gfx.c src/app.c src/lang.c src/fonts_ui_data.c src/core.c
THIRD := third_party/miniz.c third_party/stb_image_impl.c
RUNNER := runner/test_runner.c

ifeq ($(PLATFORM),sf2000)
  CC := $(MIPS_ELF)gcc
  AR := $(MIPS_ELF)ar
  RANLIB := $(MIPS_ELF)ranlib
  BUILD := build_sf2000
  TARGET := $(TARGET_NAME)_libretro_sf2000.a
  CFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -O2 -G0 \
            -mno-abicalls -fno-pic -ffunction-sections \
            -fno-asynchronous-unwind-tables -fno-unwind-tables \
            -fomit-frame-pointer -fno-strict-aliasing \
            -DSF2000 -DGB300 $(CFLAGS_COMMON) \
            -Iinclude -Isrc -Ithird_party \
            -I/home/Sajnaps/gb300/sf2000_multicore/include
  OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRC) $(THIRD)))
else ifeq ($(PLATFORM),mips)
  CC := $(MIPS_MUSL)gcc
  BUILD := build_mips
  TARGET := runner/test_runner_mips
  CFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -O2 -g -static \
            -fno-strict-aliasing $(CFLAGS_COMMON) -Iinclude -Isrc -Ithird_party
  OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRC) $(THIRD) $(RUNNER)))
else
  CC ?= gcc
  BUILD := build_host
  TARGET := runner/test_runner
  CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
            -Wno-sign-compare -Wno-missing-field-initializers -fno-strict-aliasing \
            $(CFLAGS_COMMON) -Iinclude -Isrc -Ithird_party
  OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRC) $(THIRD) $(RUNNER)))
endif

.PHONY: all clean test test-mips device fpu-check sdcard

all: $(TARGET)

ifeq ($(PLATFORM),sf2000)
$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@
	@echo "Built $@"
else
$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OBJS) -o $@
	@echo "Built $@"
endif

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: runner/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	@rm -rf screenshots/final
	@mkdir -p screenshots/final
	@rm -rf build_host/test_state_*
	EPUB_READER_SAVE=build_host/test_state_lib ./runner/test_runner library 40 screenshots/final/lib library
	EPUB_READER_SAVE=build_host/test_state_shelf ./runner/test_runner library 30 screenshots/final/shelf shelf2
	EPUB_READER_SAVE=build_host/test_state_read ./runner/test_runner library 80 screenshots/final/read read
	EPUB_READER_SAVE=build_host/test_state_images ./runner/test_runner library 55 screenshots/final/images images
	EPUB_READER_SAVE=build_host/test_state_resume ./runner/test_runner library 55 screenshots/final/resume_pre images
	EPUB_READER_SAVE=build_host/test_state_resume ./runner/test_runner library 25 screenshots/final/resume resume
	EPUB_READER_SAVE=build_host/test_state_night ./runner/test_runner library 40 screenshots/final/night night
	EPUB_READER_SAVE=build_host/test_state_toc ./runner/test_runner library 40 screenshots/final/toc toc
	EPUB_READER_SAVE=build_host/test_state_anchor ./runner/test_runner library 42 screenshots/final/anchor anchor
	EPUB_READER_SAVE=build_host/test_state_bm ./runner/test_runner library 42 screenshots/final/bookmarks bookmarks
	EPUB_READER_SAVE=build_host/test_state_bmjump ./runner/test_runner library 45 screenshots/final/bmjump bmjump
	EPUB_READER_SAVE=build_host/test_state_info ./runner/test_runner library 45 screenshots/final/info info
	EPUB_READER_SAVE=build_host/test_state_help ./runner/test_runner library 30 screenshots/final/help help
	EPUB_READER_SAVE=build_host/test_state_lang ./runner/test_runner library 84 screenshots/final/lang lang
	EPUB_READER_SAVE=build_host/test_state_lang ./runner/test_runner library 55 screenshots/final/langread langread
	EPUB_READER_SAVE=build_host/test_state_marquee ./runner/test_runner library 104 screenshots/final/marquee marquee
	EPUB_READER_SAVE=build_host/test_state_table ./runner/test_runner library 55 screenshots/final/table table
	EPUB_READER_SAVE=build_host/test_state_scroll ./runner/test_runner library 300 screenshots/final/scroll scroll
	EPUB_READER_SAVE=build_host/test_state_reflow ./runner/test_runner library 60 screenshots/final/reflow reflow
	python3 tools/ppm_to_png.py screenshots/final
	@rm -f screenshots/final/*.ppm


test-mips: PLATFORM=mips
test-mips:
	$(MAKE) PLATFORM=mips all
	@rm -rf screenshots/mips build_mips/test_state_mips build_mips/test_state_mips2 build_mips/test_state_mips3 build_mips/test_state_lang build_mips/test_state_marquee
	@mkdir -p screenshots/mips
	EPUB_READER_SAVE=build_mips/test_state_mips $(QEMU) -L $(SYSROOT) ./runner/test_runner_mips library 20 screenshots/mips/lib library
	EPUB_READER_SAVE=build_mips/test_state_mips2 $(QEMU) -L $(SYSROOT) ./runner/test_runner_mips library 55 screenshots/mips/images images
	EPUB_READER_SAVE=build_mips/test_state_mips3 $(QEMU) -L $(SYSROOT) ./runner/test_runner_mips library 300 screenshots/mips/scroll scroll
	EPUB_READER_SAVE=build_mips/test_state_lang $(QEMU) -L $(SYSROOT) ./runner/test_runner_mips library 84 screenshots/mips/lang lang
	EPUB_READER_SAVE=build_mips/test_state_lang $(QEMU) -L $(SYSROOT) ./runner/test_runner_mips library 55 screenshots/mips/langread langread
	EPUB_READER_SAVE=build_mips/test_state_marquee $(QEMU) -L $(SYSROOT) ./runner/test_runner_mips library 104 screenshots/mips/marquee marquee
	python3 tools/ppm_to_png.py screenshots/mips
	@rm -f screenshots/mips/*.ppm


edge-test: all
	@rm -rf screenshots/edge /tmp/eg_edge
	@mkdir -p screenshots/edge
	python3 tools/make_edge_cases.py /tmp/eg_edge
	EPUB_READER_SAVE=build_host/test_state_edge_empty ./runner/test_runner /tmp/eg_edge/empty 5 screenshots/edge/empty library
	EPUB_READER_SAVE=build_host/test_state_edge_corrupt ./runner/test_runner /tmp/eg_edge/corrupt 5 screenshots/edge/corrupt library
	EPUB_READER_SAVE=build_host/test_state_edge_nocover ./runner/test_runner /tmp/eg_edge/nocover 25 screenshots/edge/nocover nocover
	python3 tools/ppm_to_png.py screenshots/edge
	@rm -f screenshots/edge/*.ppm

fpu-check:
	@if [ -f $(TARGET_NAME)_libretro_sf2000.a ]; then \
	  n=$$($(MIPS_ELF)objdump -d $(TARGET_NAME)_libretro_sf2000.a | grep -E '\b(mtc1|mfc1|ctc1|cfc1|lwc1|swc1|ldc1|sdc1|cvt\.|add\.s|sub\.s|mul\.s|div\.s)\b' | wc -l); \
	  echo "FPU instructions in archive: $$n (expected 0)"; test $$n -eq 0; \
	else echo "build archive first"; exit 1; fi

device:
	$(MAKE) clean
	$(MAKE) PLATFORM=sf2000 -j4
	@mkdir -p dist
	@rm -rf build_core_sf2000
	$(MAKE) -C $(MULTICORE) CORE=$(CURDIR) CONSOLE=epub FROGGY_TYPE=SF2000 BUILD_DIR=$(CURDIR)/build_core_sf2000
	cp -f build_core_sf2000/core.elf dist/epub_sf2000_core.elf
	cp -f build_core_sf2000/core_87000000 dist/epub.sf2k
	@rm -rf build_core_gb300
	$(MAKE) -C $(MULTICORE) CORE=$(CURDIR) CONSOLE=epub FROGGY_TYPE=GB300V2 BUILD_DIR=$(CURDIR)/build_core_gb300
	cp -f build_core_gb300/core.elf dist/epub_gb300_core.elf
	cp -f build_core_gb300/core_87000000 dist/epub.gb3k
	@ls -lh dist/epub.sf2k dist/epub.gb3k
	@$(MAKE) fpu-final

fpu-final:
	@for f in dist/epub_sf2000_core.elf dist/epub_gb300_core.elf; do \
	  if [ -f $$f ]; then \
	    n=$$($(MIPS_ELF)objdump -d $$f | grep -cE '\b(mtc1|mfc1|ctc1|cfc1|lwc1|swc1|ldc1|sdc1|cvt\.|add\.s|sub\.s|mul\.s|div\.s|mov\.s)\b' || true); \
	    echo "hardware FPU instructions in $$f: $$n"; \
	  fi; \
	done

clean:
	rm -rf build_host build_mips build_sf2000 build_core_sf2000 build_core_gb300 \
	       epub_libretro_sf2000.a runner/test_runner runner/test_runner_mips
