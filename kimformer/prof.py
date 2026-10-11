"""Cycle profile of the 6502 image by function, from its llvm-nm listing.

``python -m kimformer.prof [--top N] [--lines] story command...`` loads the
story on the simulated PAL-II, picks it from the menu, runs the commands and
prints the cycles spent in each function (callees not included), or with
``--lines`` in each source line (from the image's .elf, built with ``-g``).

``python -m kimformer.prof --bench [--ram R] [--walk FILE] [--seed N] [story
[command...]]`` prints, for each command (default: the Zork I opening in
``BENCH``, or the commands of a walk file; ``--seed`` seeds the story's RNG as
``zm -s`` does), the interpreter's cycles without the time spent in story I/O
and on the console, and the story seeks.
"""

import argparse
import bisect
import os
import re
import shlex
import subprocess
import sys
from collections import Counter
from pathlib import Path

from tqdm import tqdm

from kimdos.sim import INPUT
from kimformer import fixtures, walk
from kimformer.sim import IMAGE, PAL2_RAM, REPO, Machine, ram_ranges

SYMBOL = re.compile(r"([0-9a-f]+) ([A-Za-z]) (\S+)")
IO = ("st_seek", "st_next", "kd_putc", "kd_getc")
BENCH = (
    "open mailbox",
    "read leaflet",
    "drop leaflet",
    "north",
    "east",
    "open window",
    "enter",
    "west",
    "take lamp",
    "move rug",
    "open trap door",
    "turn on lamp",
    "down",
    "south",
    "score",
)
BENCH_TIMER = 0x5A
MOVE_CYCLES = 100_000_000
MOS_IMAGE = "ghcr.io/anarkiwi/docker-mos-llvm-sdk:v23.1.0"
ADDR2LINE = os.environ.get(
    "ADDR2LINE",
    f"docker run --rm -i -v {REPO}:{REPO} --entrypoint llvm-addr2line {MOS_IMAGE}",
)


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


class Profile:
    """Counts from now on the cycles of every instruction the machine executes
    by PC in ``pcs``, and in ``io`` the cycles spent inside calls to the
    functions at the addresses in ``io_entries``, callees included, until
    ``stop()``."""

    def __init__(self, machine, io_entries=()):
        self.pcs = Counter()
        self.io = 0
        mpu, mem = machine.kim.mpu, machine.kim.mem
        step, pcs, entries = mpu.step, self.pcs, frozenset(io_entries)
        call = None

        def counted():
            nonlocal call
            pc, c = mpu.pc, mpu.processorCycles
            if call is None:
                if pc in entries:
                    s = mpu.sp
                    lo, hi = 0x100 | (s + 1) & 0xFF, 0x100 | (s + 2) & 0xFF
                    ret = (mem[lo] | mem[hi] << 8) + 1
                    call = (ret & 0xFFFF, (s + 2) & 0xFF, c)
            elif pc == call[0] and mpu.sp == call[1]:
                self.io += c - call[2]
                call = None
            step()
            pcs[pc] += mpu.processorCycles - c

        self._restore = (mpu, step)
        mpu.step = counted

    def stop(self):
        """Stop counting."""
        mpu, step = self._restore
        mpu.step = step

    def by_function(self, funcs):
        """Cycles so far per function (see by_function)."""
        return by_function(self.pcs, funcs)


def by_function(pcs, funcs):
    """Cycles per function: each PC goes to the nearest code symbol at or
    below it, '?' below them all."""
    starts = [a for a, _ in funcs]
    out = Counter()
    for pc, n in pcs.items():
        i = bisect.bisect_right(starts, pc) - 1
        out[funcs[i][1] if i >= 0 else "?"] += n
    return out


def by_line(pcs, elf, addr2line=ADDR2LINE):
    """Cycles per 'function file:line' (innermost inlined frame), from the
    debug information of elf."""
    addrs = sorted(pcs)
    out = subprocess.run(
        [*shlex.split(addr2line), "-f", "-e", str(Path(elf).resolve())],
        input="".join("0x%x\n" % a for a in addrs),
        capture_output=True,
        text=True,
        check=True,
    ).stdout.splitlines()
    counts = Counter()
    for addr, func, line in zip(addrs, out[::2], out[1::2]):
        counts["%s %s" % (func, line.rsplit("/", 1)[-1])] += pcs[addr]
    return counts


def entropy_seed(seed, timer=BENCH_TIMER, keys=2):
    """The value of plat_kim's key-timing accumulator that makes
    plat_entropy return seed after keys more key presses with the RIOT timer
    fixed at timer (the menu choice is two: the digit and Enter)."""
    s = seed ^ timer << 8
    for _ in range(keys):
        s ^= timer
        s = (s >> 3 | s << 13) & 0xFFFF
    return s


def send(machine, command):
    """Type a command and run until input is awaited again."""
    out = machine.send(command, max_cycles=MOVE_CYCLES)
    if machine.reason != INPUT:
        raise RuntimeError("%r: stopped (%s): %s" % (command, machine.reason, out))
    return out


def bench(machine, sym, commands, out=None):
    """Run the commands, printing each one's interpreter cycles (story I/O and
    console excluded), I/O cycles and story seeks; returns the interpreter
    cycles per command."""
    out = out or sys.stdout
    names = symbols(sym)
    prof = Profile(machine, [names[n] for n in IO if n in names])
    rows = []
    for command in tqdm(commands, unit="move", file=sys.stderr, leave=False):
        cycles, io, seeks = machine.cycles, prof.io, machine.seeks
        send(machine, command)
        io = prof.io - io
        rows.append((command, machine.cycles - cycles - io, io, machine.seeks - seeks))
    print("%-16s %10s %10s %5s" % ("command", "interp", "io", "seeks"), file=out)
    for row in rows:
        print("%-16s %10d %10d %5d" % row, file=out)
    interp = [r[1] for r in rows]
    print(
        "%-16s %10d %10d %5d"
        % ("total", sum(interp), sum(r[2] for r in rows), sum(r[3] for r in rows)),
        file=out,
    )
    print("%-16s %10d" % ("mean", sum(interp) // max(len(interp), 1)), file=out)
    prof.stop()
    return interp


def main(argv=None):
    """Command line entry point."""
    parser = argparse.ArgumentParser(prog="python -m kimformer.prof")
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--image", type=Path, default=IMAGE)
    parser.add_argument("--sym", type=Path, help="default: the image's .sym")
    parser.add_argument(
        "--bench", action="store_true", help="interpreter cycles per command"
    )
    parser.add_argument("--lines", action="store_true", help="cycles per line")
    parser.add_argument(
        "--ram", type=ram_ranges, default=PAL2_RAM, help="e.g. 0000-03FF,2000-AFFF"
    )
    parser.add_argument(
        "--no-jiffy", action="store_true", help="a drive without JiffyDOS"
    )
    parser.add_argument("--walk", type=Path, help="commands from a walk file")
    parser.add_argument("--seed", type=int, help="story RNG seed, as zm -s")
    parser.add_argument("story", type=Path, nargs="?")
    parser.add_argument("command", nargs="*")
    args = parser.parse_args(argv)
    sym = args.sym or args.image.with_suffix(".sym")
    story = args.story or fixtures.fetch("zork1.z3")
    machine = Machine(
        [story],
        ram=args.ram,
        image=args.image,
        timer=BENCH_TIMER,
        char_cycles=0,
        **({"jiffy": None} if args.no_jiffy else {}),
    )
    machine.run()
    if args.seed is not None:
        acc = entropy_seed(args.seed)
        machine.kim.write(symbols(sym)["seed"], bytes((acc & 0xFF, acc >> 8)))
    start = machine.cycles
    send(machine, "1")
    if args.bench:
        print("%-16s %10d" % ("load", machine.cycles - start))
        commands = walk.commands(args.walk) if args.walk else args.command
        bench(machine, sym, commands or BENCH)
        return 0
    prof = Profile(machine)
    for command in tqdm(args.command, unit="cmd", file=sys.stderr, leave=False):
        send(machine, command)
    prof.stop()
    if args.lines:
        counts = by_line(prof.pcs, args.image.with_suffix(".elf"))
    else:
        counts = prof.by_function(functions(sym))
    total = sum(counts.values())
    for name, n in counts.most_common(args.top):
        print("%10d %5.1f%% %s" % (n, 100 * n / total, name))
    print("%10d total" % total)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
