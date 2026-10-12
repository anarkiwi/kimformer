"""Replay a walk file through the host interpreter and list the seeds that win.

``python -m kimformer.walk tests/stories/zork1.walk --seeds 1-200`` prints the
seeds for which the walk ends on the story's maximum score.

``python -m kimformer.walk tests/stories/zork1.walk --stats SEED [--args ARGS]``
plays it once with that seed and prints the paging counters per move
(``zm -S``): page loads, story seeks, dirty pages and the loads and
write-backs of a pool that would evict dirty pages.
"""

import argparse
import difflib
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

from tqdm import tqdm

from kimformer import fixtures

SCORE = re.compile(r"Your score is (-?\d+) \(total of (\d+) points\)")


def commands(path):
    """Commands of a walk file: one per line, blank lines and '#' comments
    skipped. A line starting with a backslash holds keys for read_char."""
    lines = (line.strip() for line in Path(path).read_text("ascii").splitlines())
    return [line for line in lines if line and not line.startswith("#")]


def keys(command):
    """The keys of a read_char command (\\keys), else None."""
    return command[1:] if command.startswith("\\") else None


def stdin(cmds):
    """Terminal input for cmds: each line command followed by Enter, the keys
    of a read_char command alone."""
    return "".join(c if (c := keys(cmd)) is not None else cmd + "\n" for cmd in cmds)


def squash(text):
    """text without whitespace."""
    return re.sub(r"\s+", "", text)


def differences(out, ref):
    """Where out is not ref with text left out: (out, ref) snippets of every
    change other than an insertion into out, whitespace ignored. Checks a
    plain TTY transcript against one that also shows the upper window."""
    out, ref = squash(out), squash(ref)
    ops = difflib.SequenceMatcher(None, out, ref, autojunk=False).get_opcodes()
    return [
        (out[max(i1 - 20, 0) : i2 + 20], ref[max(j1 - 20, 0) : j2 + 20])
        for op, i1, i2, j1, j2 in ops
        if op in ("replace", "delete")
    ]


def unmatched(expect, lines):
    """The first of expect not found in order in lines, one per line; None if
    all are (as games/expect.awk checks)."""
    lines = iter(lines)
    for want in expect:
        if not any(want in line for line in lines):
            return want
    return None


def play(zm, story, cmds, seed, *args):
    """Interpreter output for cmds played on story with a fixed seed."""
    story = Path(story)
    result = subprocess.run(
        [str(zm), "-d", str(story.parent), "-s", str(seed), *map(str, args)]
        + [story.name],
        input=stdin(cmds).encode("latin-1"),
        capture_output=True,
        check=False,
    )
    return result.stdout.decode("latin-1")


COUNTERS = ("frames", "pinned", "loads", "dirty", "max_dirty", "sloads", "swb")


def stats(zm, story, cmds, seed, *args):
    """Cumulative paging counters (COUNTERS, then seeks) at every read of a
    walk played with a fixed seed."""
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "stats"
        play(zm, story, cmds, seed, "-S", path, *args)
        lines = path.read_text("ascii").splitlines()
    return [tuple(map(int, line.split())) for line in lines]


def summary(rows):
    """Text summary of stats rows: per-move means and maxima from the first
    read on."""
    names = COUNTERS + ("seeks",)
    first, last = dict(zip(names, rows[0])), dict(zip(names, rows[-1]))
    moves = max(len(rows) - 1, 1)
    out = [
        "frames %(frames)d pinned %(pinned)d max_dirty %(max_dirty)d" % last,
        "moves %d" % moves,
    ]
    for name in ("loads", "seeks", "sloads", "swb"):
        steps = [
            dict(zip(names, b))[name] - dict(zip(names, a))[name]
            for a, b in zip(rows, rows[1:])
        ] or [0]
        out.append(
            "%-6s %8.3f/move max %d total %d"
            % (
                name,
                (last[name] - first[name]) / moves,
                max(steps),
                last[name] - first[name],
            )
        )
    return "\n".join(out)


def won(out):
    """True if the last score the story reports is its maximum."""
    scores = SCORE.findall(out)
    return bool(scores) and scores[-1][0] == scores[-1][1]


def main(argv=None):
    """List the winning seeds of a walk; exit status 1 if there are none."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("walk")
    parser.add_argument("--story", help="story file (default: Zork I fixture)")
    parser.add_argument("--zm", default=fixtures.REPO / "build" / "zm")
    parser.add_argument("--seeds", default="1-200", help="inclusive range lo-hi")
    parser.add_argument("--stats", type=int, metavar="SEED", help="paging counters")
    parser.add_argument("--args", default="", help="more zm options, e.g. '-p 8'")
    args = parser.parse_args(argv)
    lo, hi = map(int, args.seeds.split("-"))
    story = args.story or fixtures.fetch("zork1.z3")
    cmds = commands(args.walk)
    if args.stats is not None:
        rows = stats(args.zm, story, cmds, args.stats, *shlex.split(args.args))
        print(summary(rows))
        return 0
    wins = [
        seed
        for seed in tqdm(range(lo, hi + 1), desc="seeds")
        if won(play(args.zm, story, cmds, seed))
    ]
    print(" ".join(map(str, wins)))
    return 0 if wins else 1


if __name__ == "__main__":
    raise SystemExit(main())
