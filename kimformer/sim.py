"""kimformer on a simulated PAL-II: the KIMDOS py65 KIM-1 with an sd2iec model.

``python -m kimformer.sim [--dir DIR] [story...]`` plays interactively.
"""

import argparse
import random
import re
import sys
from pathlib import Path

from tqdm import tqdm

from kimdos.iec import Drive
from kimdos.sim import CHAR_CYCLES, CYCLES, INPUT, Kim

REPO = Path(__file__).resolve().parent.parent
IMAGE = REPO / "build" / "kimformer.bin"
ORG = 0x2000
PAL2_RAM = ((0x0000, 0x13FF), (0x1700, 0x17FF), (0x2000, 0xFEFF))
MONITOR_ZP = range(0xEF, 0x100)
RIOT_TIMER = 0x1706
CHUNK = 1_000_000
VT_STATUS = re.compile("\x1b7\x1b\\[1;1H\x1b\\[7m(.*?)\x1b\\[0m\x1b8", re.S)
VT_REGION = re.compile("\x1b\\[[0-9;]*[rH]")


def drive_name(path):
    """The name an sd2iec shows for a FAT file: upper-case PETSCII."""
    return Path(path).name.upper().encode("latin-1")


class Machine:
    """A PAL-II running build/kimformer.bin with story files on device 8.

    stories: paths, or a mapping of drive names to contents.
    ram: inclusive (start, end) ranges that accept writes.
    timer: a fixed value for the RIOT timer, the entropy source, so that runs
    with different timing play the same; None for the modelled timer.
    char_cycles: cycles per TTY character (0 to time the CPU alone).
    Unused RAM starts with pseudo-random contents.
    """

    def __init__(
        self,
        stories=(),
        ram=PAL2_RAM,
        image=IMAGE,
        timer=None,
        char_cycles=CHAR_CYCLES,
        **drive,
    ):  # pylint: disable=too-many-arguments,too-many-positional-arguments
        if not isinstance(stories, dict):
            stories = {drive_name(p): Path(p).read_bytes() for p in stories}
        self.drive = Drive(dict(stories), **drive)
        self.kim = Kim(drives=self.drive, ram=ram, char_cycles=char_cycles)
        if timer is not None:
            self.kim.mem.subscribe_to_read([RIOT_TIMER], lambda _: timer)
        self.kim.write(0, random.Random(0).randbytes(0xFF00))
        self.kim.write(ORG, Path(image).read_bytes())
        self.kim.mpu.pc = ORG
        self.zp = self.kim.read(MONITOR_ZP.start, len(MONITOR_ZP))
        self.seen = 0
        self.reason = None
        self.status = []
        self.seeks = 0
        execute = self.drive.execute

        def counted(cmd):
            self.seeks += cmd[:1] == b"P"
            execute(cmd)

        self.drive.execute = counted

    @property
    def cycles(self):
        """CPU cycles so far: microseconds on a 1 MHz KIM-1."""
        return self.kim.cycles

    @property
    def zp_intact(self):
        """True if the monitor's zero page is as it was at start."""
        return self.kim.read(MONITOR_ZP.start, len(MONITOR_ZP)) == self.zp

    def run(self, max_cycles=2_000_000_000, progress=None):
        """Run until input is awaited, the program exits or max_cycles pass;
        returns the new output with '\\n' line breaks and without the VT100
        sequences, appending the status lines drawn to self.status."""
        end = self.cycles + max_cycles
        while self.cycles < end:
            before = self.cycles
            self.reason = self.kim.run(min(CHUNK, end - self.cycles))
            if progress is not None:
                progress(self, self.cycles - before)
            if self.reason != CYCLES:
                break
        return self.screen(self.output())

    def screen(self, text):
        """text without VT100 sequences; status lines go to self.status."""
        self.status += VT_STATUS.findall(text)
        return VT_REGION.sub("", VT_STATUS.sub("", text))

    def output(self):
        """Output since the last call, with line breaks as '\\n'; a CR at the
        end waits for the LF that may follow it."""
        end = len(self.kim.out) - self.kim.out.endswith(b"\r")
        out = self.kim.out[self.seen : end]
        self.seen = end
        return out.decode("latin-1").replace("\r\n", "\n")

    def send(self, line, **kw):
        """Type a line and Enter, then run until input is awaited again."""
        self.kim.type(line + "\r")
        return self.run(**kw)


def play(machine, lines, write, report=None):
    """Run the machine interactively: lines yields input, write takes output,
    report (if given) takes the cycles and story seeks of each step."""
    with tqdm(unit="cyc", unit_scale=True, leave=False, file=sys.stderr) as meter:

        def progress(m, n):
            write(m.output())
            meter.update(n)

        def step():
            cycles, seeks = machine.cycles, machine.seeks
            machine.run(progress=progress)
            if report:
                report(machine.cycles - cycles, machine.seeks - seeks)

        step()
        for line in lines:
            if machine.reason != INPUT:
                break
            machine.kim.type(line.rstrip("\r\n") + "\r")
            step()
    return machine.reason


def ram_ranges(text):
    """'0000-03FF,2000-A9FF' as ((0, 0x3FF), (0x2000, 0xA9FF)); I/O is added."""
    out = [(0x1700, 0x17FF)]
    for part in text.split(","):
        lo, hi = (int(v, 16) for v in part.split("-"))
        out.append((lo, hi))
    return tuple(out)


def main(argv=None):
    """Command line entry point."""
    parser = argparse.ArgumentParser(prog="python -m kimformer.sim")
    parser.add_argument("--dir", type=Path, help="SD card directory")
    parser.add_argument("--image", type=Path, default=IMAGE)
    parser.add_argument(
        "--ram", type=ram_ranges, default=PAL2_RAM, help="e.g. 0000-03FF,2000-A9FF"
    )
    parser.add_argument(
        "--char-cycles", type=int, default=CHAR_CYCLES, help="TTY cycles per char"
    )
    parser.add_argument(
        "--cycles", action="store_true", help="report cycles and seeks per input"
    )
    parser.add_argument("story", nargs="*", type=Path)
    args = parser.parse_args(argv)
    paths = [p for p in sorted(args.dir.iterdir()) if p.is_file()] if args.dir else []
    machine = Machine(
        paths + args.story,
        ram=args.ram,
        image=args.image,
        char_cycles=args.char_cycles,
    )
    before = dict(machine.drive.files)

    def write(text):
        sys.stdout.write(text)
        sys.stdout.flush()

    def report(cycles, seeks):
        print("\n[%d cycles, %d seeks]" % (cycles, seeks), file=sys.stderr)

    reason = play(machine, sys.stdin, write, report if args.cycles else None)
    if args.dir:
        for name, data in machine.drive.files.items():
            if before.get(name) != data:
                (args.dir / name.decode("latin-1")).write_bytes(data)
    print("\n[%s after %d cycles]" % (reason, machine.cycles), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
