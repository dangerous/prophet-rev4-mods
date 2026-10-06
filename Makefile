PYTHON ?= python3
CC ?= cc
BUILD := build

.PHONY: test test-tooling test-firmware test-image image clean

test: test-tooling test-firmware test-image

test-tooling:
	$(PYTHON) -m unittest discover -s tests/tooling -t .

# Host-side harness for the wrapper's portable logic.
test-firmware: $(BUILD)/test_relatch
	$(BUILD)/test_relatch

$(BUILD)/test_relatch: tests/firmware/test_relatch.c firmware/relatch.c firmware/relatch.h
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware -o $@ tests/firmware/test_relatch.c firmware/relatch.c

# Cross-build the wrapper and the image, then check every structural invariant.
test-image:
	$(PYTHON) -m unittest discover -s tests/firmware -t . -p 'test_*.py'

# The installable image.
image:
	@mkdir -p $(BUILD)
	$(PYTHON) -m tools fwbuild firmware $(BUILD)
	$(PYTHON) -m tools build --base fixtures/V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx \
		--wrapper $(BUILD)/wrapper.bin --map $(BUILD)/wrapper.map \
		--hooks firmware/hooks.json -o $(BUILD)/prophet10_v5_relatch.syx

clean:
	rm -rf $(BUILD)
