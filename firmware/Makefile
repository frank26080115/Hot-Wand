# STM32CubeIDE external build entry point. PlatformIO remains the single
# source of truth for the compiler, framework, libraries, and linker script.

PIO ?= $(USERPROFILE)/.platformio/penv/Scripts/pio.exe
PIO_ENV := stm32f042_cubeide

.PHONY: all clean rebuild

all:
	"$(PIO)" run -e $(PIO_ENV)

clean:
	"$(PIO)" run -e $(PIO_ENV) -t clean

rebuild: clean all
