# kimformer

A version 3 Z-machine for the KIM-1, PAL-II and replicas: plays Zork I and
other `.z3` stories from an SD card on an sd2iec attached through the
[KIMDOS](https://github.com/anarkiwi/kimdos) adapter.

Needs a VT100/ANSI terminal (80x24) for the status line; `make kim VT100=0`
builds for a plain TTY.

## Build

```
git submodule update --init
pip install -e extern/kimdos -e ".[dev]"
make kim         # build/kimformer.bin, .ptp (paper tape), .prg (SD card)
make kimdos      # build/kimdos-0400.ptp, the KIMDOS shell at $0400 (needs cc65)
make host        # build/zm, the same interpreter on the host
make test coverage mos-check
```

Releases carry `kimformer.ptp`, `kimformer.prg`, `kimdos-0400.ptp` and the
demo story ready to use.

## Use

Copy the `.z3` stories to the SD card. Then load the interpreter at $2000
one of two ways and pick a story from the menu.

From paper tape (no SD card copy needed):

```
kimtape load --port /dev/ttyUSB0 kimformer.ptp --go 2000
```

From the SD card, bootstrapping the KIMDOS shell by paper tape: copy
`kimformer.prg` to the card as `KIMFORMER.PRG`, then

```
kimtape load --port /dev/ttyUSB0 kimdos-0400.ptp --go 0400
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
