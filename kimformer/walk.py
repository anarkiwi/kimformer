"""Replay a walk file through the host interpreter and list the seeds that win.

``python -m kimformer.walk tests/stories/zork1.walk --seeds 1-200`` prints the
seeds for which the walk ends on the story's maximum score.
"""

import argparse
import re
import subprocess
from pathlib import Path

from tqdm import tqdm

from kimformer import fixtures

SCORE = re.compile(r"Your score is (-?\d+) \(total of (\d+) points\)")


def commands(path):
    """Commands of a walk file: one per line, blank lines and '#' comments skipped."""
    lines = (line.strip() for line in Path(path).read_text("ascii").splitlines())
    return [line for line in lines if line and not line.startswith("#")]


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
        input="".join(c + "\n" for c in cmds).encode("latin-1"),
        capture_output=True,
        check=False,
    )
    return result.stdout.decode("latin-1")


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
    args = parser.parse_args(argv)
    lo, hi = map(int, args.seeds.split("-"))
    story = args.story or fixtures.fetch("zork1.z3")
    cmds = commands(args.walk)
    wins = [
        seed
        for seed in tqdm(range(lo, hi + 1), desc="seeds")
        if won(play(args.zm, story, cmds, seed))
    ]
    print(" ".join(map(str, wins)))
    return 0 if wins else 1


if __name__ == "__main__":
    raise SystemExit(main())
