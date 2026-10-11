"""Host build of the interpreter: compliance, Zork I, paging, errors."""

import re

import pytest

from kimformer import walk
from tests.conftest import REPO, flat, text

ZM_ERR_OPEN, ZM_ERR_VERSION, ZM_ERR_MEMORY, ZM_ERR_OPCODE = 1, 2, 3, 4
ZM_ERR_STACK, ZM_ERR_ADDRESS, ZM_ERR_OBJECT = 5, 6, 7

OPENING = [
    ("open mailbox", "Opening the small mailbox reveals a leaflet."),
    ("read leaflet", "WELCOME TO ZORK!"),
    ("drop leaflet", "Dropped."),
    ("north", "North of House"),
    ("east", "Behind House"),
    ("open window", "you open the window far enough to allow entry."),
    ("enter", "Kitchen"),
    ("west", "Living Room"),
    ("take lamp", "Taken."),
    ("move rug", "revealing the dusty cover of a closed trap door."),
    ("open trap door", "reveal a rickety staircase descending into darkness."),
    ("turn on lamp", "The brass lantern is now on."),
    ("down", "The trap door crashes shut, and you hear someone barring it."),
    ("south", "East of Chasm"),
    ("score", "Your score is 35 (total of 350 points), in 14 moves."),
]


def czech_body(lines):
    """CZECH output without the interpreter-specific header report."""
    lines = [line.rstrip() for line in lines]
    start = lines.index("Header (No tests)")
    return lines[:start] + lines[start + 5 :]


def test_czech(runner, czech):
    story = runner.add(czech["z3"])
    result = runner.run("-w", 255, story)
    assert result.returncode == 0
    out = text(result).splitlines()
    assert "Passed: 349, Failed: 0, Print tests: 19" in out
    expected = czech["out3"].read_text(encoding="latin-1").splitlines()
    assert czech_body(out) == czech_body(expected)
    assert "    standard 1.1" in out
    assert "    Flags on: NO status," in out


def test_czech_trace(runner, czech):
    story = runner.add(czech["z3"])
    result = runner.run("-t", "-w", 255, story)
    assert result.returncode == 0
    trace = result.stderr.decode().splitlines()
    assert len(trace) > 10000
    assert all(
        re.fullmatch(r"[0-9A-F]{5} [0-9A-F]{2}( [0-9A-F]{4})*", t) for t in trace
    )
    assert trace[-1].split()[1] == "BA"


def test_zork_opening(runner, zork):
    story = runner.add(zork)
    result = runner.run(
        "-s", 1, story, commands=[c for c, _ in OPENING] + ["quit", "y"]
    )
    assert result.returncode == 0
    out = flat(result)
    assert out.startswith("ZORK I: The Great Underground Empire")
    assert "Release 119 / Serial number 880429" in out
    pos = 0
    for command, reply in OPENING:
        pos = out.index(">" + command, pos)
        pos = out.index(reply, pos)
    assert out.endswith("Do you wish to leave the game? (Y is affirmative): >y")


def walkthrough(runner, story, *args):
    """Zork opening with a save and restore in the middle."""
    commands = [c for c, _ in OPENING[:9]]
    commands += ["save", "", "drop lamp", "north", "restore", "", "inventory"]
    commands += [c for c, _ in OPENING[9:]] + ["quit", "y"]
    result = runner.run("-s", 7, *args, story, commands=commands)
    assert result.returncode == 0
    return result


def test_zork_save_restore(runner, zork):
    story = runner.add(zork)
    out = flat(walkthrough(runner, story))
    assert "File [zork1.SAV]: Ok." in out
    assert "carrying: A brass lantern" in out
    assert out.count("Ok.") == 2
    assert "The brass lantern is now on." in out
    save = runner.dir / "zork1.SAV"
    assert save.read_bytes()[:4] == b"KZ3\x01"
    fresh = flat(runner.run(story, commands=["restore", "", "look", "inventory"]))
    assert "Ok. >look Living Room" in fresh
    assert "A brass lantern" in fresh


@pytest.mark.parametrize("frames", [3, 4, 16])
def test_zork_eviction(runner, zork, frames):
    story = runner.add(zork)
    full = walkthrough(runner, story)
    full_save = (runner.dir / "zork1.SAV").read_bytes()
    small = walkthrough(runner, story, "-p", frames)
    assert text(small) == text(full)
    assert (runner.dir / "zork1.SAV").read_bytes() == full_save


def test_zork_split_ram(runner, zork):
    story = runner.add(zork)
    full = walkthrough(runner, story)
    split = walkthrough(runner, story, "-r", "04-13", "-r", "20-7F", "-r", "90-91")
    assert text(split) == text(full)


def test_zork_restart_verify(runner, zork):
    story = runner.add(zork)
    commands = ["open mailbox", "$verify", "restart", "y", "look", "quit", "y"]
    out = flat(runner.run(story, commands=commands))
    assert "Verifying disk... The disk is correct." in out
    assert "Restarting." in out
    tail = out[out.index("Restarting.") :]
    assert "ZORK I: The Great Underground Empire" in tail
    assert "There is a small mailbox here." in tail


def test_zork_wrap(runner, zork):
    story = runner.add(zork)
    commands = ["north", "east", "open window", "enter", "quit", "y"]
    wide = runner.run("-s", 1, "-w", 255, story, commands=commands)
    narrow = runner.run("-s", 1, "-w", 30, story, commands=commands)
    assert flat(narrow) == flat(wide)
    assert max(map(len, text(narrow).splitlines())) <= 30
    assert max(map(len, text(wide).splitlines())) > 30
    assert all(len(line) <= 80 for line in text(runner.run(story)).splitlines())


def test_zork_input_editing(runner, zork):
    story = runner.add(zork)
    out = flat(runner.run(story, stdin="OPEN MAILBOXX\x08\nnorrr\x7f\x7fth\n"))
    assert "Opening the small mailbox reveals a leaflet." in out
    assert "North of House" in out


def test_zork_transcript_bit(runner, zork):
    story = runner.add(zork)
    out = flat(runner.run(story, commands=["script", "unscript"]))
    assert "Here begins a transcript of interaction with" in out
    assert "Here ends a transcript of interaction with" in out


def test_eof_exits(runner, zork):
    story = runner.add(zork)
    assert runner.run(story, commands=["look"]).returncode == 0


def play(runner, story, *commands):
    """Run the probe story with a fixed seed."""
    return runner.run("-s", 3, runner.add(story), commands=commands)


def test_probe_parse(runner, probe):
    out = text(play(runner, probe, "parse zzz, ball.  red", "parse"))
    assert "6 parse/5/1 ?/3/7 ?/1/10 ball/4/12 ?/1/16 red/3/19" in out
    assert "1 parse/5/1" in out


def test_probe_parse_limit(runner, probe):
    out = text(play(runner, probe, "parse a b c d e f g h"))
    assert "6 parse/5/1 ?/1/7 ?/1/9 ?/1/11 ?/1/13 ?/1/15" in out


def test_probe_streams(runner, probe):
    out = text(play(runner, probe, "streams"))
    assert "4 3 abcf 13\nshown 10\n" in out
    assert "hidden" not in out


def test_probe_props(runner, probe):
    assert "7 3 1 4 4 0 <number 0> 9 5 4 red ball 1 1" in text(
        play(runner, probe, "props")
    )


def test_probe_text(runner, probe):
    out = text(play(runner, probe, "esc", "noops", "verify", "call0", "what"))
    assert "<{|}>?\n>noops\nok\n>verify\ngood\n>call0\n0\n>what\n?\n" in out


def test_probe_random(runner, probe):
    out = text(play(runner, probe, "random", "random"))
    first, second = re.findall(r">random\n(\d+) (\d+) (\d+) (\d+)", out)
    assert first == second
    assert first[0] == first[2] and first[3] == "0"
    assert all(1 <= int(v) <= 100 for v in first[:3])


def test_probe_seed(runner, probe):
    story = runner.add(probe)
    rolls = [text(runner.run("-s", s, story, commands=["roll"])) for s in (2, 2, 4, 6)]
    assert rolls[0] == rolls[1]
    assert len(set(rolls[1:])) == 3


def test_probe_save_restore(runner, probe):
    commands = ["count", "save", "", "count", "count", "restore", "", "count"]
    commands += ["restore", "missing.sav", "save", "nodir/x.sav"]
    out = text(play(runner, probe, *commands, "restart", "count", "quit"))
    counts = re.findall(r">count\n(\d+)", out)
    assert counts == ["1", "2", "3", "2", "1"]
    assert "File [probe.SAV]: \nsaved\n" in out
    assert "missing.sav\nrestore failed" in out
    assert "nodir/x.sav\nsave failed" in out


def test_probe_restore_mismatch(runner, probe, zork):
    story = runner.add(zork)
    runner.run(story, commands=["save", "zork.sav"])
    out = text(play(runner, probe, "restore", "zork.sav", "count"))
    assert "restore failed\n>count\n1" in out


@pytest.mark.parametrize("cut", [10, 100])
def test_probe_restore_truncated(runner, probe, cut):
    play(runner, probe, "save", "")
    save = runner.dir / "probe.SAV"
    save.write_bytes(save.read_bytes()[:-cut])
    result = play(runner, probe, "restore", "")
    assert result.returncode == ZM_ERR_OPEN
    assert "Error 1 at $" in text(result)


@pytest.mark.parametrize(
    "command,code",
    [
        ("div0", ZM_ERR_OPCODE),
        ("mod0", ZM_ERR_OPCODE),
        ("illegal", ZM_ERR_OPCODE),
        ("obj0", ZM_ERR_OBJECT),
        ("attr", ZM_ERR_OBJECT),
        ("putprop", ZM_ERR_OBJECT),
        ("nextprop", ZM_ERR_OBJECT),
        ("static", ZM_ERR_ADDRESS),
        ("far", ZM_ERR_ADDRESS),
        ("pull", ZM_ERR_STACK),
        ("local", ZM_ERR_STACK),
        ("deep", ZM_ERR_STACK),
        ("nest", ZM_ERR_STACK),
        ("quit", 0),
        ("ret", 0),
    ],
)
def test_probe_errors(runner, probe, command, code):
    result = play(runner, probe, command, "count")
    assert result.returncode == code
    out = text(result)
    assert ">count" not in out
    assert bool(re.search(r"\nError %d at \$[0-9A-F]{5}\n$" % code, out)) == bool(code)


def test_load_errors(runner, czech, zork):
    (runner.dir / "empty.z3").write_bytes(b"")
    z5 = runner.add(czech["z5"])
    story = runner.add(zork)
    cases = [
        (("missing.z3",), ZM_ERR_OPEN),
        (("empty.z3",), ZM_ERR_OPEN),
        ((z5,), ZM_ERR_VERSION),
        (("-p", 2, story), ZM_ERR_MEMORY),
        (("-r", "20-40", story), ZM_ERR_MEMORY),
    ]
    for args, code in cases:
        result = runner.run(*args)
        assert result.returncode == code
        assert text(result).endswith("Error %d\n" % code)


@pytest.mark.parametrize(
    "args,code",
    [
        (("-w", 0), 64),
        (("-w", 256), 64),
        (("-r", "x"), 64),
        (("-q",), 64),
        (("-d", "/nonexistent"), 66),
    ],
)
def test_usage(runner, args, code):
    assert runner.run(*args).returncode == code


def test_launcher(runner, probe, zork, czech):
    runner.add(probe, "probe.z3")
    runner.add(zork, "ZORK1.Z3")
    runner.add(czech["z5"], "bad.z3")
    (runner.dir / "notes.txt").write_text("x")
    (runner.dir / "toolongname_story.z3").write_text("x")
    stdin = "".join(
        line + "\n"
        for line in [
            "0",
            "9",
            "x",
            "3",
            "count",
            "quit",
            "2",
            "1",
            "look",
            "quit",
            "y",
            "Q",
        ]
    )
    result = runner.run(stdin=stdin)
    assert result.returncode == 0
    out = text(result)
    listing = "1. ZORK1.Z3\n2. bad.z3\n3. probe.z3\nStory (Q quits): "
    assert out.count(listing) == 7
    assert "notes.txt" not in out and "toolongname" not in out
    assert "PROBE\n>count\n1\n>quit\n" in out
    assert "\nError 2\n" in out
    assert "West of House" in out


def test_launcher_empty(runner):
    result = runner.run(commands=["1"])
    assert result.returncode == 0
    assert text(result) == "No stories\n"


VT_OPEN, VT_CLOSE = "\x1b[2;24r\x1b[24;1H", "\x1b[r\x1b[24;1H"
STATUS = re.compile("\x1b7\x1b\\[1;1H\x1b\\[7m(.*?)\x1b\\[0m\x1b8")


def statuses(result):
    """The status lines drawn, after checking the VT100 set-up and reset."""
    out = result.stdout.decode("latin-1")
    assert out.startswith(VT_OPEN) and out.endswith(VT_CLOSE)
    return STATUS.findall(out)


def test_status_time(runner, clock):
    result = runner.run("-v", "-w", 40, runner.add(clock), commands=["a", "b", "c"])
    assert result.returncode == 0
    assert statuses(result) == [
        "Time: 12:05 AM".rjust(40),
        "Clock Tower" + "Time: 1:30 PM".rjust(29),
        "Clock Tower" + "Time: 12:00 PM".rjust(29),
        "Clock Tower" + "Time: 11:59 PM".rjust(29),
    ]


def test_status_score(runner, zork, czech):
    story = runner.add(zork)
    result = runner.run("-v", "-s", 1, story, commands=["open mailbox", "north"])
    lines = statuses(result)
    assert lines == [
        "West of House".ljust(62) + "Score: 0  Moves: 0",
        "West of House".ljust(62) + "Score: 0  Moves: 1",
        "North of House".ljust(62) + "Score: 0  Moves: 2",
    ]
    assert b"\x1b" not in runner.run("-s", 1, story, commands=["look"]).stdout
    out = runner.run("-v", "-w", 255, runner.add(czech["z3"])).stdout
    assert b"    Flags on: NO status," not in out and b"\n    Flags on:" in out


@pytest.mark.parametrize("args", [(), ("-p", 3)])
def test_demo_walkthrough(runner, demo, args):
    script = (REPO / "games" / "demo.walkthrough").read_text(encoding="ascii")
    expect = (REPO / "games" / "demo.expect").read_text(encoding="ascii")
    result = runner.run("-s", 5, *args, runner.add(demo), stdin=script)
    assert result.returncode == 0
    assert walk.unmatched(expect.splitlines(), text(result).splitlines()) is None
