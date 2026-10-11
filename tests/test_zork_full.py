"""A complete winning Zork I playthrough on the host build."""

import re

import pytest

from kimformer import walk
from tests.conftest import REPO, ZM, text

WALK = REPO / "tests" / "stories" / "zork1.walk"
SEED = 16


def transcript(runner, zork, *args):
    """Text output of the walk played with SEED."""
    result = runner.run(
        "-s", SEED, *args, runner.add(zork), commands=walk.commands(WALK)
    )
    assert result.returncode == 0
    return text(result)


def exchanges(out):
    """(command, reply) pairs of a transcript."""
    return [
        tuple(chunk.split("\n", 1))
        for chunk in re.split(r"^>", out, flags=re.M)[1:]
        if "\n" in chunk
    ]


def test_zork_full(runner, zork):
    out = transcript(runner, zork)
    pairs = exchanges(out)
    deposits = [reply for cmd, reply in pairs if re.fullmatch(r"put .* in case", cmd)]
    assert len(deposits) == 20
    assert all(reply.startswith("Done.") for reply in deposits)
    scores = [int(s) for s in re.findall(r"Your score is (\d+) \(total of 350", out)]
    first = scores.index(350)
    assert first >= 9
    assert scores[: first + 1] == sorted(set(scores[: first + 1]))
    assert set(scores[first:]) == {350}
    assert "You have died" not in out
    flat_out = " ".join(out.split())
    whisper = flat_out.index("Look to your treasures for the final secret.")
    barrow = flat_out.index("Inside the Barrow As you enter the barrow,", whisper)
    flat_out.index("You have mastered the first part of the ZORK trilogy.", barrow)
    assert re.search(
        r"Your score is 350 \(total of 350 points\), in \d+ moves\. This gives "
        r"you the rank of Master Adventurer\. Would you like to restart .* "
        r"\(Type RESTART, RESTORE, or QUIT\): >quit$",
        flat_out[barrow:],
    )


@pytest.mark.parametrize(
    "args", [("-p", 3), ("-r", "04-13", "-r", "20-7F", "-r", "90-91"), ("-w", 40)]
)
def test_zork_full_memory(runner, zork, args):
    full = transcript(runner, zork)
    other = transcript(runner, zork, *args)
    assert " ".join(other.split()) == " ".join(full.split())


def test_walk_stats(capsys, zork):
    tool = ["--zm", str(ZM), "--story", str(zork), "--stats", str(SEED)]
    assert walk.main([str(WALK), *tool, "--args", "-p 8"]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert lines[0].startswith("frames ") and " pinned " in lines[0]
    assert int(lines[1].split()[1]) > 350
    rates = {line.split()[0]: line.split() for line in lines[2:]}
    assert set(rates) == {"loads", "seeks", "sloads", "swb"}
    assert float(rates["loads"][1].split("/")[0]) > 1
    assert walk.summary([(1,) * 8]).splitlines()[1] == "moves 1"


def test_walk_seeds(capsys, tmp_path, zork):
    tool = ["--zm", str(ZM), "--story", str(zork)]
    assert walk.main([str(WALK), *tool, "--seeds", f"{SEED - 2}-{SEED + 2}"]) == 0
    wins = [int(s) for s in capsys.readouterr().out.split()]
    assert SEED in wins
    assert len(wins) < 5
    quitter = tmp_path / "quit.walk"
    quitter.write_text("# gives up\n\nquit\n", encoding="ascii")
    assert walk.commands(quitter) == ["quit"]
    assert walk.main([str(quitter), *tool, "--seeds", "1-3"]) == 1
    assert capsys.readouterr().out == "\n"
