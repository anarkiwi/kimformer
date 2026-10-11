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

# Host build, tests and coverage.
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2
HOST_SRC := $(CORE) src/host/plat_host.c src/host/main.c
HOST_HDRS := $(HDRS) src/host/host.h
COV_OBJ := $(patsubst src/%.c,$(BUILD)/cov/%.o,$(HOST_SRC))

.PHONY: cov coverage mos-check fixtures

host: $(BUILD)/zm

$(BUILD)/zm: $(HOST_SRC) $(HOST_HDRS)
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -DZM_TRACE -o $@ $(HOST_SRC)

cov: $(BUILD)/cov/zm

$(BUILD)/cov/%.o: src/%.c $(HOST_HDRS)
	@mkdir -p $(@D)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -O0 --coverage -DZM_TRACE -c -o $@ $<

$(BUILD)/cov/zm: $(COV_OBJ)
	$(HOST_CC) --coverage -o $@ $^

fixtures:
	python3 -m kimformer.fixtures

test: host cov
	find $(BUILD)/cov -name '*.gcda' -delete
	python3 -m pytest $(PYTEST_ARGS)

coverage:
	gcovr -r . --object-directory $(BUILD)/cov --filter 'src/' \
	    --print-summary --fail-under-line 85

# 6502 check: link the core with a stub platform and report its size.
MOS_CFLAGS ?= -Os -flto -Wall -Wextra -Werror
MOS_SIM_CC ?= $(DOCKER_RUN) $(MOS_IMAGE) mos-sim-clang
MOS_SIZE ?= $(DOCKER_RUN) --entrypoint llvm-size $(MOS_IMAGE)
MOS_NM ?= $(DOCKER_RUN) --entrypoint llvm-nm $(MOS_IMAGE)
MOS_TEXT_MAX ?= 12288

mos-check: $(CORE) $(HDRS) src/stub/plat_stub.c
	@mkdir -p $(BUILD)/mos
	$(MOS_SIM_CC) $(MOS_CFLAGS) -o $(BUILD)/mos/zm.sim $(CORE) src/stub/plat_stub.c
	$(MOS_NM) --size-sort -S $(BUILD)/mos/zm.sim.elf | tail -12
	$(MOS_SIZE) $(BUILD)/mos/zm.sim.elf | tee $(BUILD)/mos/size.txt
	@awk 'NR == 2 && $$1 > $(MOS_TEXT_MAX) { print "text " $$1 " exceeds $(MOS_TEXT_MAX)"; exit 1 }' $(BUILD)/mos/size.txt
include games/games.mk
