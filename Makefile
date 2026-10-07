PYTHON ?= python3
CC ?= cc
BUILD := build

.PHONY: test test-tooling test-firmware test-image image clean

test: test-tooling test-firmware test-image

test-tooling:
	$(PYTHON) -m unittest discover -s tests/tooling -t .

# Host-side harnesses for the wrapper's portable logic.
test-firmware: $(BUILD)/test_relatch $(BUILD)/test_seq $(BUILD)/test_rate $(BUILD)/test_oct \
               $(BUILD)/test_vhold $(BUILD)/test_disp $(BUILD)/test_arp
	$(BUILD)/test_relatch
	$(BUILD)/test_seq
	$(BUILD)/test_rate
	$(BUILD)/test_oct
	$(BUILD)/test_vhold
	$(BUILD)/test_disp
	$(BUILD)/test_arp

$(BUILD)/test_arp: tests/firmware/test_arp.c firmware/arp.c firmware/arp.h firmware/platform.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware -o $@ tests/firmware/test_arp.c firmware/arp.c

$(BUILD)/test_vhold: tests/firmware/test_vhold.c firmware/vhold.c firmware/vhold.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware -o $@ tests/firmware/test_vhold.c firmware/vhold.c

$(BUILD)/test_disp: tests/firmware/test_disp.c firmware/disp.c firmware/disp.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware -o $@ tests/firmware/test_disp.c firmware/disp.c

$(BUILD)/test_oct: tests/firmware/test_oct.c firmware/oct.c firmware/oct.h firmware/platform.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware -o $@ tests/firmware/test_oct.c firmware/oct.c

$(BUILD)/test_rate: tests/firmware/test_rate.c firmware/rate.c firmware/rate.h firmware/platform.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware -o $@ tests/firmware/test_rate.c firmware/rate.c

$(BUILD)/test_relatch: tests/firmware/test_relatch.c firmware/relatch.c firmware/relatch.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware -o $@ tests/firmware/test_relatch.c firmware/relatch.c

$(BUILD)/test_seq: tests/firmware/test_seq.c firmware/seq.c firmware/seq.h firmware/platform.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware -o $@ tests/firmware/test_seq.c firmware/seq.c

# Cross-build the wrapper and the image, then check every structural invariant.
test-image:
	$(PYTHON) -m unittest discover -s tests/firmware -t . -p 'test_*.py'

# The installable images. 'image' patches the MIDI-parser table for note values under MIDI
# sync; 'image-internal' leaves that table exactly as V5 had it (note values on the
# internal clock only).
image: $(BUILD)/wrapper.bin
	$(PYTHON) -m tools build --base fixtures/V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx \
		--wrapper $(BUILD)/wrapper.bin --map $(BUILD)/wrapper.map \
		--hooks firmware/hooks.json -o $(BUILD)/prophet10_v5_relatch_seq.syx

image-internal: $(BUILD)/wrapper.bin
	$(PYTHON) -m tools build --base fixtures/V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx \
		--wrapper $(BUILD)/wrapper.bin --map $(BUILD)/wrapper.map \
		--hooks firmware/hooks_internal.json -o $(BUILD)/prophet10_v5_relatch_seq_internal.syx

$(BUILD)/wrapper.bin: firmware/*.c firmware/*.h tools/fw.py tools/fwlink.py
	@mkdir -p $(BUILD)
	$(PYTHON) -m tools fwbuild firmware $(BUILD)

clean:
	rm -rf $(BUILD)
