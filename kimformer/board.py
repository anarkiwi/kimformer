"""Smoke test kimformer on a real board over its TTY line.

``python -m kimformer.board --port /dev/ttyUSB0`` loads build/kimformer.ptp
with the monitor, verifies it, starts it at $2000, picks DEMO.Z3 from the menu
and plays games/demo.walkthrough, checking the output against games/demo.expect.
"""

import argparse
import re
import sys
import time
from pathlib import Path

from tqdm import tqdm

from kimdos.board import Board
from kimformer.sim import ORG, REPO, VT_STATUS, strip_vt100
from kimformer.walk import unmatched

IMAGE = REPO / "build" / "kimformer.ptp"
SCRIPT = REPO / "games" / "demo.walkthrough"
EXPECT = REPO / "games" / "demo.expect"
LOGS = REPO / "logs"
VT_OPEN = "\x1b[2;24r"
OTHER_PROMPTS = r"Story \(Q quits\): |File \[[^\]]*\]: "
PROMPTS = {
    vt100: re.compile(
        r"(?:\n>%s|%s)$" % (VT_STATUS.pattern if vt100 else "", OTHER_PROMPTS),
        re.S,
    )
    for vt100 in (False, True)
}


def normal(raw):
    """TTY output as text: NUL fill dropped, line breaks as '\\n'."""
    text = raw.decode("latin-1").replace("\x00", "")
    return text.replace("\r\n", "\n").replace("\r", "")


class Session(Board):
    """kimformer on a board: each command waits for the next input prompt (the
    game's '>', after its status line on a VT100 build, the menu or a file
    name); the output, without VT100 sequences, goes to log."""

    def __init__(self, dev, baud, log, trace=False):
        super().__init__(dev, baud, trace)
        self.log = log
        self.vt100 = False
        self.status = []

    def start(self, org):
        """Run the image at org; wait_prompt collects the menu."""
        self.mon.sync()
        self.port.take()
        self.mon.go(org)

    def wait_prompt(self, timeout):
        """Collect output up to the next input prompt; returns it as text."""
        deadline = time.monotonic() + timeout
        while True:
            text = normal(self.port.rx)
            self.vt100 = self.vt100 or VT_OPEN in text
            if PROMPTS[self.vt100].search(text):
                break
            if time.monotonic() > deadline:
                self.log.write(strip_vt100(text) + "\n[timeout]\n")
                raise TimeoutError("no prompt after %r" % text[-80:])
            self.port.poll(0.1)
        self.port.take()
        text = strip_vt100(text, self.status)
        self.log.write(text)
        self.log.flush()
        return text

    def command(self, line, timeout=60):
        """Type a line and Enter; returns the output up to the next prompt."""
        self.port.take()
        self.port.write(line.encode("latin-1") + b"\r")
        return self.wait_prompt(timeout)

    def pick(self, menu, story, timeout):
        """Choose story from the launcher's menu."""
        found = re.search(r"^(\d+)\. %s$" % re.escape(story.upper()), menu, re.M)
        if not found:
            raise LookupError("%s not in the menu:\n%s" % (story, menu))
        return self.command(found.group(1), timeout)


def smoke(session, lines, story, timeout, report):
    """Pick story and type lines; report takes each line and its latency.
    Returns the whole output."""
    out = session.wait_prompt(timeout)
    for line in [None] + lines:
        start = time.monotonic()
        if line is None:
            out += session.pick(out, story, timeout)
        else:
            out += session.command(line, timeout)
        report(story if line is None else line, time.monotonic() - start)
    return out


def main(argv=None):
    """Command line entry point; exit status 1 if an expected line is missing."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True, help="serial device")
    parser.add_argument("--baud", type=int, default=1200)
    parser.add_argument("--image", type=Path, default=IMAGE, help="paper tape")
    parser.add_argument(
        "--no-load", action="store_true", help="start the image already loaded"
    )
    parser.add_argument("--story", default="DEMO.Z3", help="story to pick")
    parser.add_argument("--script", type=Path, help="input lines (default: demo)")
    parser.add_argument("--expect", type=Path, help="lines expected in order")
    parser.add_argument("--timeout", type=float, default=120, help="seconds a step")
    parser.add_argument("--log", type=Path, help="transcript (default: logs/)")
    parser.add_argument("--trace", action="store_true", help="log every byte")
    args = parser.parse_args(argv)
    if args.script is None:
        args.script, args.expect = SCRIPT, args.expect or EXPECT
    lines = args.script.read_text("latin-1").splitlines()
    if args.log is None:
        LOGS.mkdir(exist_ok=True)
        args.log = LOGS / time.strftime("board-%Y%m%d-%H%M%S.log")
    with open(args.log, "w", encoding="latin-1") as log:
        session = Session(args.port, args.baud, log, args.trace)
        try:
            if args.no_load:
                session.start(ORG)
            else:
                session.install(args.image.read_bytes(), ORG)
            with tqdm(total=len(lines) + 1, unit="cmd", file=sys.stderr) as meter:

                def report(line, seconds):
                    meter.write("%7.2fs  %s" % (seconds, line))
                    meter.update()

                out = smoke(session, lines, args.story, args.timeout, report)
        finally:
            session.close()
    print("transcript: %s" % args.log)
    if args.expect:
        expect = args.expect.read_text("latin-1").splitlines()
        missing = unmatched(expect, out.splitlines())
        if missing is not None:
            print("missing: %s" % missing)
            return 1
        print("all %d expected lines found" % len(expect))
    return 0


if __name__ == "__main__":
    sys.exit(main())
