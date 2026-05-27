ENV ?= esp32cam
PIO ?= pio

.PHONY: all build flash monitor

all: build flash monitor

build:
	$(PIO) run -e $(ENV)

flash:
	$(PIO) run -e $(ENV) -t upload

monitor:
	$(PIO) device monitor -e $(ENV)
