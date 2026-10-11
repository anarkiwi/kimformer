# Development

## Requirements

`cc`, `make`, Python 3.11+ and Docker (for the llvm-mos SDK image). KIMDOS is
the `extern/kimdos` submodule; install it and the Python tooling with

```
git submodule update --init
pip install -e extern/kimdos -e ".[dev]"
```

## Targets

| Target | What |
| --- | --- |
| `make host` | `build/zm`, the interpreter with the host platform |
| `make cov` | `build/cov/zm`, the same built with `--coverage` |
| `make fixtures` | download and verify Zork I, CZECH and Inform 6; build CZECH for v3 |
| `make test` | pytest (`-n auto`) against `build/cov/zm`; `PYTEST_ARGS` passes options |
| `make coverage` | gcovr summary of the C sources; fails under 85% |
| `make kim` | the KIM-1 image: `build/kimformer.{bin,elf,map,sym,ptp,prg}`; `VT100=0` for a plain TTY |
| `make mos-check` | links the core with `src/stub/` for the 6502 (`MOS_CFLAGS`), prints sizes, fails above `MOS_TEXT_MAX` |
| `make bench` | interpreter cycles per Zork I move on the simulated KIM-1 (after `make kim`) |
| `make format` | clang-format (LLVM style) over `src/` |

## Host interpreter

```
build/zm [-w width] [-p pages] [-r lo-hi]... [-s seed] [-d dir] [-t] [-v] [story]
```

| Option | |
| --- | --- |
| `-w` | wrap width (1-255, default 80) |
| `-p` | cache frames beyond dynamic memory (forces eviction when small; at least 3) |
| `-r` | free RAM page range in hex, repeatable (default `20-FE`, like a PAL-II) |
| `-s` | fixed seed for `plat_entropy` |
| `-d` | directory for stories and save files (default `.`) |
| `-v` | VT100 status line, as on the KIM-1 |
| `-t` | trace: one stderr line per instruction, `PPPPP OP operands...` (PC, opcode byte, operand values, hex) |

With a story it loads and runs it; without one it runs the launcher over the
`.z3` files in the directory. Input characters are echoed. The exit status is
0 after `@quit` or end of input, otherwise the `zm.h` error code (64 for usage,
66 for a bad directory).

## Tests

`tests/stories/probe.inf` is compiled at test time; each of its commands
exercises one area (parsing, streams, properties, save/restore, every fatal
error). Run a single test first, e.g.
`python -m pytest -n0 tests/test_host.py::test_czech`, then the full suite.

`tests/test_zork_full.py` plays `tests/stories/zork1.walk`, a command list for
all 350 points of Zork I, with a fixed `-s` seed: every treasure must reach the
trophy case, the score must climb to 350 and the game must end in the stone
barrow. Combat and the thief depend on the RNG, so the walk wins only for some
seeds. After changing the walk or the RNG, list the seeds that still win with
`python -m kimformer.walk tests/stories/zork1.walk --seeds 1-200` (after
`make host`) and set `SEED` in the test to one of them.

`tests/test_kim.py` and `tests/test_board.py` need `make kim` and run the
image on the simulated KIM-1; the Zork I tests are marked `zork`
(`-m 'not zork'` skips them locally; CI runs them).

`tests/test_board.py` runs `kimformer.board` against `tests/fake_kim.py`, a
KIM-1 on a pty: the monitor commands kimtape uses, and `G` running the
simulated PAL-II on what was loaded. The plain TTY case needs
`make kim VT100=0 BUILD=build/plain`.

## KIM-1 simulator

`kimformer.sim.Machine` is a PAL-II (RAM $0000-$13FF and $2000-$FEFF, writes
elsewhere ignored, RAM initially random) from the KIMDOS py65 simulator,
running `build/kimformer.bin` from $2000 with an sd2iec model as device 8
holding the given files under their upper-case names. `run()` and
`send(line)` run until input is awaited and return the output with the
VT100 sequences removed; status lines collect in `status`, story seeks in
`seeks`, and `cycles` counts CPU cycles (microseconds at 1 MHz). `timer=`
fixes the RIOT timer, the entropy source, so runs of different length play
alike; `char_cycles=0` leaves out the TTY's character time; `mirrors=`
((start, end, base), ...) makes address ranges images of the RAM at base, as
on a board with an undecoded address line.

```
python -m kimformer.sim [--dir DIR] [--ram 0000-03FF,2000-AFFF] [--char-cycles N] [--cycles] [story...]
```

plays interactively (stdin to the TTY, stdout from it, a cycle meter on
stderr, drawn only while the transcript is at the start of a line). `--dir` puts every file of DIR on the card and writes saved files
back; `--cycles` reports the cycles and story seeks of each input line.

```
python -m kimformer.prof [--top N] [--image BIN] [--sym SYM] [--ram R] [--lines] story command...
python -m kimformer.prof --bench [--image BIN] [--ram R] [story [command...]]
```

loads the story, then prints the CPU cycles spent per function (by the
nearest symbol in `build/kimformer.sym`) while it runs the commands; with
`--lines`, per source line, from an image built with `-g`
(`make kim BUILD=build/g MOS_CFLAGS="-Os -flto -mlto-zp=183 -g"`) through
`llvm-addr2line` from the SDK image (`ADDR2LINE` overrides the command).
`--bench` plays the commands (default: Zork I's opening, the cached fixture)
with a fixed entropy source and prints per command the interpreter's cycles
(without the time inside `st_seek`, `st_next` and the console calls), those
I/O cycles and the story seeks.
