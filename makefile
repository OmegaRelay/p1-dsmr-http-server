ARGS ?=

SHELL := /bin/bash # needed for source command

export PYTHONVENV = ${PWD}/.venv/bin/activate

SET_ENV = source ${PYTHONVENV}
WEST := ${SET_ENV} && west

bootstrap:
	rm -rf .west
	cd app && ${WEST} init -l
	${WEST} update

update:
	${WEST} update

.PHONY: build
build:
	${WEST} build app ${ARGS}
