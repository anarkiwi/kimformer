# Host tools: cc, python3. The 6502 toolchain and the Inform 6 compiler run
# in containers; override MOS_CC / INFORM to use host installs.
BUILD ?= build
MOS_IMAGE ?= ghcr.io/anarkiwi/docker-mos-llvm-sdk:v23.1.0
DOCKER_RUN := docker run --rm -u $(shell id -u):$(shell id -g) \
    -v $(CURDIR):$(CURDIR) -w $(CURDIR)
MOS_CC ?= $(DOCKER_RUN) $(MOS_IMAGE) mos-common-clang
CLANG_FORMAT ?= $(DOCKER_RUN) --entrypoint clang-format $(MOS_IMAGE)
HOST_CC ?= cc

CORE := src/zmem.c src/ztext.c src/zobj.c src/zexec.c src/launcher.c
HDRS := $(wildcard src/*.h)

.PHONY: all host kim demo test format clean
all: host

format:
	$(CLANG_FORMAT) -i src/*.c src/*.h src/*/*.c src/*/*.h

clean:
	rm -rf $(BUILD)
include games/games.mk
