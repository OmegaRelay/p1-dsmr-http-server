ARGS ?=

SHELL := /bin/bash # needed for source command

export PYTHONVENV = ${PWD}/.venv/bin/activate

ifeq (esp, $(filter esp,${MAKECMDGOALS}))
	BOARD = esp32_devkitc_wroom/esp32/procpu
else 
	BOARD = nrf54l15pdk/nrf54l15/cpuapp
endif 

ARGS +=-b $(BOARD)

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
	${WEST} build app ${ARGS}

esp:
	@printf ""