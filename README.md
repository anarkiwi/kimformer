# kimformer

A Z-machine for the KIM-1, PAL-II and replicas: plays Zork I and other
version 3, 4, 5 and 8 stories (`.z3`, `.z4`, `.z5`, `.z8`) from an SD card on
an sd2iec attached through the
[KIMDOS](https://github.com/anarkiwi/kimdos) adapter.

Needs a VT100/ANSI terminal (80x24) for the status line and the upper window
of v4+ games; `make kim VT100=0` builds for a plain TTY.

## Build

```
pip install -e ".[dev]"   # with kimdos from PyPI: its C library and simulator
make kim         # build/kimformer.bin, .ptp (paper tape), .prg (SD card)
make host        # build/zm, the same interpreter on the host
make test coverage mos-check
```

Releases carry `kimformer.ptp`, `kimformer.prg` and the demo story ready to
use; the KIMDOS shell comes from the
[KIMDOS releases](https://github.com/anarkiwi/kimdos/releases).

## Use

Copy the stories to the SD card. Then load the interpreter at $2000
one of two ways and pick a story from the menu.

From paper tape (no SD card copy needed):

```
kimtape load --port /dev/ttyUSB0 https://github.com/anarkiwi/kimformer/releases/latest/download/kimformer.ptp --go 2000
```

From the SD card, bootstrapping the KIMDOS shell by paper tape: copy
`kimformer.prg` (from the release) to the card as `KIMFORMER.PRG`, then

```
kimtape load --port /dev/ttyUSB0 https://github.com/anarkiwi/kimdos/releases/latest/download/kimdos-0400.ptp --go 0400
```

and in the shell

```
>L KIMFORMER
$2000-$xxxx
>Q
```

then `2000` space `G` in the monitor. The interpreter takes $0400-$13FF for
its cache, so reload the shell afterwards to use it again.

`python -m kimformer.sim --dir DIR` runs the same on a simulated PAL-II;
`python -m kimformer.board --port /dev/ttyUSB0` smoke-tests a board.

## Documentation

- [docs/usage.md](docs/usage.md) — hardware, SD card, loading, playing, memory, performance
- [docs/design.md](docs/design.md) — architecture, memory map, interfaces
- [docs/demo.md](docs/demo.md) — demo adventure: building, map, walkthrough
- [docs/development.md](docs/development.md) — building, testing, tracing, the simulator
- [docs/paging.md](docs/paging.md) — unified paging of dynamic memory: the measurements behind it
