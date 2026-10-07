PYTHON ?= python3
CC ?= cc
BUILD := build

.PHONY: test test-tooling test-firmware test-image image image-native clean

test: test-tooling test-firmware test-image

test-tooling:
	$(PYTHON) -m unittest discover -s tests/tooling -t .

# Host-side harnesses for the engine's portable logic.
test-firmware: $(BUILD)/test_rate $(BUILD)/test_oct $(BUILD)/test_vhold $(BUILD)/test_disp \
               $(BUILD)/test_arp $(BUILD)/test_arpui
	$(BUILD)/test_rate
	$(BUILD)/test_oct
	$(BUILD)/test_vhold
	$(BUILD)/test_disp
	$(BUILD)/test_arp
	$(BUILD)/test_arpui

HOSTCC := $(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware

$(BUILD)/test_rate: tests/firmware/test_rate.c firmware/rate.c firmware/rate.h
	@mkdir -p $(BUILD)
	$(HOSTCC) -o $@ tests/firmware/test_rate.c firmware/rate.c

$(BUILD)/test_oct: tests/firmware/test_oct.c firmware/oct.c firmware/oct.h firmware/platform.h
	@mkdir -p $(BUILD)
	$(HOSTCC) -o $@ tests/firmware/test_oct.c firmware/oct.c

$(BUILD)/test_vhold: tests/firmware/test_vhold.c firmware/vhold.c firmware/vhold.h
	@mkdir -p $(BUILD)
	$(HOSTCC) -o $@ tests/firmware/test_vhold.c firmware/vhold.c

$(BUILD)/test_disp: tests/firmware/test_disp.c firmware/disp.c firmware/disp.h
	@mkdir -p $(BUILD)
	$(HOSTCC) -o $@ tests/firmware/test_disp.c firmware/disp.c

$(BUILD)/test_arp: tests/firmware/test_arp.c firmware/arp.c firmware/arp.h firmware/platform.h
	@mkdir -p $(BUILD)
	$(HOSTCC) -o $@ tests/firmware/test_arp.c firmware/arp.c

$(BUILD)/test_arpui: tests/firmware/test_arpui.c firmware/arpui.c firmware/arpui.h firmware/arp.c \
                     firmware/rate.c firmware/disp.c firmware/platform.h
	@mkdir -p $(BUILD)
	$(HOSTCC) -o $@ tests/firmware/test_arpui.c firmware/arpui.c firmware/arp.c firmware/rate.c firmware/disp.c

# Cross-build the engine and the image, then check every structural invariant.
test-image:
	$(PYTHON) -m unittest discover -s tests/firmware -t . -p 'test_*.py'

# The installable image: stock 2.1.0 + our engine in one 32 KB record at 0x20088000.
image: $(BUILD)/native.bin
	$(PYTHON) -m tools build --base fixtures/prophet5_main_2.1.0.syx \
		--wrapper $(BUILD)/native.bin --map $(BUILD)/native.map \
		--hooks firmware/hooks_native.json -o $(BUILD)/prophet10_native.syx

image-native: image

$(BUILD)/native.bin: firmware/*.c firmware/*.h tools/fw.py tools/fwlink.py
	@mkdir -p $(BUILD)
	$(PYTHON) -m tools fwbuild firmware $(BUILD)

clean:
	rm -rf $(BUILD)
