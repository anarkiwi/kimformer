# kimformer

A version 3 Z-machine for the KIM-1, PAL-II and replicas: plays Zork I and
other `.z3` stories from an SD card on an sd2iec attached through the
[KIMDOS](https://github.com/anarkiwi/kimdos) adapter.

## Host build

```
pip install -e ".[dev]"
make host        # build/zm
build/zm zork1.z3
make test coverage mos-check
```

See [docs/development.md](docs/development.md) for options and tracing.

## Documentation

- [docs/design.md](docs/design.md) — architecture, memory map, interfaces
- [docs/demo.md](docs/demo.md) — demo adventure: building, map, walkthrough
- [docs/development.md](docs/development.md) — building, testing, tracing
