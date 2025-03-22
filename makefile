ARGS ?=

SHELL := /bin/bash # needed for source command

export PYTHONVENV = ${PWD}/.venv/bin/activate

ifeq (esp, $(filter esp,${MAKECMDGOALS}))
	BOARD = esp32_devkitc_wroom/esp32/procpu
else ifeq (nrf, $(filter nrf,${MAKECMDGOALS}))
	BOARD = nrf54l15pdk/nrf54l15/cpuapp
endif 

ARGS +=-b $(BOARD)

APPDIR ?= app

SET_ENV = source ${PYTHONVENV}
WEST := ${SET_ENV} && west

bootstrap:
	rm -rf .west
	cd app && ${WEST} init -l
	${WEST} update

update:
	${WEST} update

clean:
	@rm -rf build

.PHONY: build
build:
	${WEST} build ${APPDIR} ${ARGS}

esp:
	@printf ""
nrf:
	@printf ""