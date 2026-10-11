# Development

## Requirements

`cc`, `make`, Python 3.11+ and Docker (for the llvm-mos SDK image). Install the
Python tooling with `pip install -e ".[dev]"`.

## Targets

| Target | What |
| --- | --- |
| `make host` | `build/zm`, the interpreter with the host platform |
| `make cov` | `build/cov/zm`, the same built with `--coverage` |
| `make fixtures` | download and verify Zork I, CZECH and Inform 6; build CZECH for v3 |
| `make test` | pytest (`-n auto`) against `build/cov/zm`; `PYTEST_ARGS` passes options |
| `make coverage` | gcovr summary of the C sources; fails under 85% |
| `make mos-check` | links the core with `src/stub/` for the 6502 (`-Os -flto`), prints sizes, fails above `MOS_TEXT_MAX` |
| `make format` | clang-format (LLVM style) over `src/` |

## Host interpreter

```
build/zm [-w width] [-p pages] [-r lo-hi]... [-s seed] [-d dir] [-t] [story]
```

| Option | |
| --- | --- |
| `-w` | wrap width (1-255, default 80) |
| `-p` | cache frames beyond dynamic memory (forces eviction when small; at least 3) |
| `-r` | free RAM page range in hex, repeatable (default `20-FE`, like a PAL-II) |
| `-s` | fixed seed for `plat_entropy` |
| `-d` | directory for stories and save files (default `.`) |
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
