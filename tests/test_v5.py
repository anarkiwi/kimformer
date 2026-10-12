"""Host build with version 4, 5 and 8 stories: compliance suites, the probe
story, the screen model and a complete PunyInform game."""

import os
import re
import shlex
import subprocess

import pytest

from kimformer import fixtures, walk
from tests.conftest import REPO, flat, text
from tests.test_host import ZM_ERR_STACK, czech_body

LOH_WALK = REPO / "tests" / "stories" / "library_of_horror.walk"
DFROTZ = os.environ.get("DFROTZ")


@pytest.mark.parametrize("version", [4, 5, 8])
def test_czech(runner, czech, version):
    result = runner.run("-w", 255, runner.add(czech[f"z{version}"]))
    assert result.returncode == 0
    out = text(result).splitlines()
    assert (
        "Passed: %d, Failed: 0, Print tests: 19" % (367 if version == 4 else 406) in out
    )
    expected = czech[f"out{version}"].read_text(encoding="latin-1").splitlines()
    assert czech_body(out) == czech_body(expected)
    assert "    standard 1.1" in out
    assert "    Screen size: 255x255; in 1x1 units: 255x255" in out[:40] or version == 4


def test_praxix(runner):
    out = text(runner.run(runner.add(fixtures.praxix()), commands=["all"]))
    assert "\nAll tests passed.\n" in out
    assert "Undo is not available on this interpreter." in out


def test_etude(runner):
    story = runner.add(fixtures.etude())
    stdin = "6\n7\n1.8\naZ.9\nHello\n\n12\n more\n14\nx"
    out = text(runner.run("-n", story, stdin=stdin))
    assert "This aspect of your interpreter appears to behave according to spec." in out
    assert "> \ncode=97: ASCII character 'a'\nAccentedInput> \ncode=90: " in out
    assert "code=104: ASCII character 'h'" in out
    assert 'You just typed "given more".' in out
    assert out.rstrip().endswith("This is a final line of text. Goodbye.")


def probe(runner, story, *commands, stdin=None, args=()):
    """Probe output for commands (or raw stdin)."""
    if stdin is None:
        stdin = walk.stdin(commands)
    result = runner.run(*args, runner.add(story), stdin=stdin)
    assert result.returncode == 0
    return text(result)


def test_probe5_tables(runner, probe5):
    out = probe(runner, probe5, "tables", "props", "throw", "uni", "undo", "aux")
    assert ">tables\n2 9 none aabcdf abcddf 00 70 148 64 165 148 165 1 -1 8\n" in out
    assert ">props\n20 5 1\n" in out
    assert ">throw\n7\n" in out
    assert ">uni\n3 0 A? 1 0 1\n" in out
    assert ">undo\n-1 0\n" in out
    assert ">aux\naux 0\n" in out


def test_probe5_tokenise(runner, probe5):
    out = probe(runner, probe5, "tok", "piracy")
    assert (
        ">tok\n3 ?/5/2 ?/4/8 ?/5/13\n3 u0/5/2 ?/4/8 u1/5/13\n3 u0/5/2 -/0/1 u1/5/13\n"
        in out
    )
    assert ">piracy\ngenuine\n" in out


def test_probe5_input(runner, probe5):
    keys = "keys\nab\x1b[A\x1bOB\x1b[D\x1b[C\x7f\x08\r.pre\nde\x08f\nfoo\x1b[B"
    out = probe(runner, probe5, stdin=keys + "\x1b[A\x1bxbeep\nquit\n", args=("-n",))
    assert ">97 98 129 130 131 132 8 8 13 46\n" in out
    assert ">abc \x08[13 5 abcdf]\n" in out
    assert ">term 130\n>term 129\n>\x07ok\n" in out


def test_probe5_windows(runner, probe5):
    plain = probe(runner, probe5, "win", "quit")
    assert ">win\nrow 2 col 7\nlower 24\nunbuffered text\nerased\n>" in plain
    vt = probe(runner, probe5, "win", "keys", "\\x", "\\.", "quit", args=("-v", "-n"))
    assert vt.startswith("\x1b[2;24r\x1b[24;1H")
    win = vt[vt.index(">") :]
    assert win.startswith(
        ">\x1b[4;24r\x1b[24;1H\x1b[1;1H\x1b[2;5HUP\x1b[K\x1b[0;7mR\x1b[0;7;1mB"
        "\x1b[0m\x1b[1;1Habc\x1b[2;1Hfgh\x1b[24;1Hrow 2 col 7\nlower 24\n"
        "unbuffered text\n\x1b[3;80H\x1b[1J\x1b[24;1H\x1b[4;1H\x1b[J\x1b[24;1H"
        "\x1b[4;24r\x1b[2J\x1b[24;1H\x1b[1;24r\x1b[24;1H\x1b[1;24r\x1b[2J\x1b[24;1H"
        "erased\n>"
    )
    assert ">\x1b[24;1H \x1b[24;1H120 \x1b[24;5H \x1b[24;5H46\n>" in vt
    assert vt.endswith(">\x1b[1;24r\x1b[24;1H\x1b[r\x1b[24;1H")


def test_probe5_save_restore(runner, probe5):
    commands = ["save", "", "restore", "", "restore", "missing.sav", "quit"]
    out = probe(runner, probe5, *commands)
    assert ">save\nFile [probe5.SAV]: \nsave 1\n" in out
    assert ">restore\nFile [probe5.SAV]: \nsave 2\n" in out
    assert "missing.sav\nrestore 0\n" in out
    assert (runner.dir / "probe5.SAV").read_bytes()[:4] == b"KZ3\x02"


def test_v5_restore_v3_save(runner, probe, probe5):
    probe5_name = runner.add(probe5)
    runner.run(runner.add(probe), commands=["save", "p3.sav"])
    result = runner.run(probe5_name, commands=["restore", "p3.sav", "quit"])
    assert "p3.sav\nrestore 0\n" in text(result)


@pytest.mark.parametrize("args", [(), ("-p", 3)])
def test_demo5_walkthrough(runner, demo5, args):
    script = (REPO / "games" / "demo.walkthrough").read_text(encoding="ascii")
    expect = (REPO / "games" / "demo.expect").read_text(encoding="ascii")
    result = runner.run("-s", 5, *args, runner.add(demo5), stdin=script)
    assert result.returncode == 0
    assert walk.unmatched(expect.splitlines(), text(result).splitlines()) is None


def loh_play(runner, loh, *args):
    """The Library Of Horror walk: output without the input echo."""
    result = runner.run(
        "-n", *args, runner.add(loh), stdin=walk.stdin(walk.commands(LOH_WALK))
    )
    assert result.returncode == 0
    return text(result)


MILESTONES = [
    "[The score has just gone up by 10 points.]",
    "You have so far scored 10 out of a possible 100, in 10 turns, earning you "
    "the rank of Well-meaning.",
    "Frank rolls up in front of you, blocking your path.",
    '"Oh, here it is, sir! The book that the master asked for!',
    "[The score has just gone up by 20 points.]",
    "From some other room, you hear the sound of the cardboard box falling",
    "Do you mean the small book or the red book?",
    "[The score has just gone up by 50 points.]",
    "You managed to escape the burning building just in time!",
    "*** You have won ***",
    "In that game you scored 100 out of a possible 100, in 39 turns, earning you "
    "the rank of Hero.",
    "10 talking to the manager 20 obstructing the robot 50 burning the evil book "
    "20 leaving the library 100 total (out of 100)",
]


def test_loh_full(runner, loh):
    out = " ".join(loh_play(runner, loh).split())
    pos = 0
    for milestone in MILESTONES:
        pos = out.index(milestone, pos) + len(milestone)
    assert "You have died" not in out


@pytest.mark.parametrize(
    "args", [("-p", 3), ("-w", 40), ("-r", "04-13", "-r", "20-5F")]
)
def test_loh_memory(runner, loh, args):
    full = loh_play(runner, loh)
    assert walk.squash(loh_play(runner, loh, *args)) == walk.squash(full)


def test_loh_vt100(runner, loh):
    result = runner.run(
        "-v", "-n", runner.add(loh), stdin=walk.stdin(walk.commands(LOH_WALK))
    )
    out = result.stdout.decode("latin-1")
    assert out.count("Score: 100") >= 1
    assert "\x1b[1;1H Gloomy Street\x1b[1;52H Score: 0      Moves: 0" in out
    assert "Joseph Campbell" in out


def test_loh_save_restore(runner, loh):
    cmds = walk.commands(LOH_WALK)
    cut = cmds.index("put box on frank")
    story = runner.add(loh)
    first = cmds[:cut] + ["save", "", "quit", "y"]
    runner.run("-n", story, stdin=walk.stdin(first))
    rest = ["restore", ""] + cmds[cut:]
    out = " ".join(text(runner.run("-n", story, stdin=walk.stdin(rest))).split())
    assert "Ok." in out
    pos = out.index("[The score has just gone up by 20 points.]")
    out.index("In that game you scored 100 out of a possible 100", pos)


@pytest.mark.skipif(not DFROTZ, reason="DFROTZ (a dfrotz command) not set")
@pytest.mark.parametrize("name", ["loh", "demo5"])
def test_against_dfrotz(runner, loh, demo5, name):
    story = {"loh": loh, "demo5": demo5}[name]
    if name == "loh":
        cmds = walk.commands(LOH_WALK)
    else:
        cmds = walk.commands(REPO / "games" / "demo.walkthrough")
        cmds = [c for c in cmds if c not in ("save", "restore")]
    ours = text(runner.run("-n", runner.add(story), stdin=walk.stdin(cmds)))
    ref_in = "".join((walk.keys(c) or c) + "\n" for c in cmds)
    ref = subprocess.run(
        [*shlex.split(DFROTZ), "-m", "-p", "-q", "-w", "80", str(story)],
        input=ref_in.encode("latin-1"),
        capture_output=True,
        check=True,
    ).stdout.decode("latin-1")
    assert walk.differences(ours, ref) == []


def test_launcher_versions(runner, czech, loh):
    runner.add(czech["z4"], "C4.Z4")
    runner.add(czech["z8"], "c8.z8")
    runner.add(loh, "LOH.Z5")
    (runner.dir / "x.z6").write_bytes(b"\x06")
    (runner.dir / "y.z7").write_bytes(b"\x07")
    result = runner.run(stdin="3\nlook\nquit\ny\nQ\n")
    out = text(result)
    assert out.startswith("1. C4.Z4\n2. LOH.Z5\n3. c8.z8\nStory (Q quits): ")
    assert "Passed: 406, Failed: 0" in out


def test_v5_stack_errors(runner, probe5):
    assert ZM_ERR_STACK == 5
    assert re.search(r"^PROBE5", text(runner.run(runner.add(probe5))), re.M)
    assert flat(runner.run(runner.add(probe5), commands=["quit"])).startswith("PROBE5")
