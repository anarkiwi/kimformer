"""Cycle profile of the 6502 image by function, from its llvm-nm listing.

``python -m kimformer.prof [--top N] story command...`` loads the story on
the simulated PAL-II, picks it from the menu, runs the commands and prints the
cycles spent in each function (callees not included).
"""

import argparse
import bisect
import re
from collections import Counter
from pathlib import Path

from kimformer.sim import IMAGE, Machine

SYMBOL = re.compile(r"([0-9a-f]+) ([A-Za-z]) (\S+)")


def symbols(sym_path):
    """{name: address} of every symbol in an ``llvm-nm`` listing."""
    out = {}
    for line in Path(sym_path).read_text(encoding="ascii").splitlines():
        if m := SYMBOL.fullmatch(line):
            out[m[3]] = int(m[1], 16)
    return out


def functions(sym_path):
    """Sorted (address, name) of the code symbols, one name per address."""
    text = {}
    for line in Path(sym_path).read_text(encoding="ascii").splitlines():
        if (m := SYMBOL.fullmatch(line)) and m[2] in "tT":
            text.setdefault(int(m[1], 16), m[3])
    return sorted(text.items())


def profile(machine):
    """Count from now on the cycles of every instruction the machine executes:
    returns a Counter of cycles by PC that fills as it runs."""
    pcs = Counter()
    mpu = machine.kim.mpu
    step = mpu.step

    def counted():
        pc, c = mpu.pc, mpu.processorCycles
        step()
        pcs[pc] += mpu.processorCycles - c

    mpu.step = counted
    return pcs


def by_function(pcs, funcs):
    """Cycles per function: each PC goes to the nearest code symbol at or
    below it, '?' below them all."""
    starts = [a for a, _ in funcs]
    out = Counter()
    for pc, n in pcs.items():
        i = bisect.bisect_right(starts, pc) - 1
        out[funcs[i][1] if i >= 0 else "?"] += n
    return out


def main(argv=None):
    """Command line entry point."""
    parser = argparse.ArgumentParser(prog="python -m kimformer.prof")
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--image", type=Path, default=IMAGE)
    parser.add_argument("--sym", type=Path, help="default: the image's .sym")
    parser.add_argument("story", type=Path)
    parser.add_argument("command", nargs="*")
    args = parser.parse_args(argv)
    machine = Machine([args.story], image=args.image, char_cycles=0)
    machine.run()
    machine.send("1")
    pcs = profile(machine)
    for command in args.command:
        machine.send(command)
    counts = by_function(pcs, functions(args.sym or args.image.with_suffix(".sym")))
    total = sum(counts.values())
    for name, n in counts.most_common(args.top):
        print("%10d %5.1f%% %s" % (n, 100 * n / total, name))
    print("%10d total" % total)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
