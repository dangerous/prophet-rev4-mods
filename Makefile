PYTHON ?= python3

.PHONY: test test-tooling test-firmware image clean

test: test-tooling test-firmware

test-tooling:
	$(PYTHON) -m unittest discover -s tests/tooling -t .

test-firmware:
	@echo "(no firmware tests yet)"

image:
	@echo "(no image build yet)"

clean:
	rm -rf build
