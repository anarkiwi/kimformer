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
make host        # build/zm, the same interpreter on the host
make test coverage mos-check
```

## Use

Copy `.z3` stories (and `build/kimformer.prg` as `KIMFORMER.PRG`) to the SD
card, then load the interpreter at $2000 and start it there:

```
kimtape load --port /dev/ttyUSB0 build/kimformer.ptp --go 2000
```

or `L KIMFORMER` from the KIMDOS shell, then `2000 G`. Pick a story from the
menu. `python -m kimformer.sim --dir DIR` runs the same on a simulated PAL-II.

## Documentation

- [docs/usage.md](docs/usage.md) — hardware, SD card, loading, playing, memory, performance
- [docs/design.md](docs/design.md) — architecture, memory map, interfaces
- [docs/demo.md](docs/demo.md) — demo adventure: building, map, walkthrough
- [docs/development.md](docs/development.md) — building, testing, tracing, the simulator
