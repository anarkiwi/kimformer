"""The 6502 build on a simulated PAL-II with an sd2iec model on the bus."""

import io

import pytest

from kimdos.build import packaged
from kimdos.sim import INPUT, MONITOR
from kimformer import prof, sim, walk
from kimformer.sim import IMAGE, Machine
from tests.conftest import REPO
from tests.test_host import OPENING

VT_OPEN, VT_CLOSE = "\x1b[2;24r\x1b[24;1H", "\x1b[r\x1b[24;1H"
MENU = "1. DEMO.Z3\n2. ZORK1.Z3\nStory (Q quits): "
ZORK_SAVE = (
    OPENING[:9]
    + [("save", "File [ZORK1.SAV]: "), ("", "Ok.")]
    + [("drop lamp", "Dropped."), ("east", "Kitchen")]
    + [("restore", "File [ZORK1.SAV]: "), ("", "Ok.")]
    + [("inventory", "A brass lantern")]
    + OPENING[9:-1]
    + [("score", "Your score is 35 (total of 350 points)")]
)
ZORK_QUIT = [("quit", "(Y is affirmative)"), ("y", MENU)]
SMALL_FRAMES = 24


def status(left, right):
    """A status line as drawn at 80 columns."""
    return left.ljust(80 - len(right)) + right


@pytest.fixture(name="image", scope="session")
def fixture_image():
    """build/kimformer.bin."""
    if not IMAGE.exists():
        pytest.fail(f"{IMAGE} missing: run make kim")
    return IMAGE


@pytest.fixture(name="stories", scope="session")
def fixture_stories(demo, zork):
    """The card: the demo, Zork I and a file that is not a story."""
    return {
        b"DEMO.Z3": demo.read_bytes(),
        b"README": b"not a story",
        b"ZORK1.Z3": zork.read_bytes(),
    }


def boot(image, stories, **kw):
    """A machine at the menu, with a fixed entropy source."""
    machine = Machine(dict(stories), image=image, timer=0x5A, **kw)
    assert machine.run().endswith(MENU)
    return machine


def check(machine):
    """Bus protocol and monitor zero page intact."""
    assert not machine.drive.errors
    assert machine.zp_intact


def play(machine, steps):
    """Send each command, check its reply; returns [(command, output, cycles)]."""
    log = []
    for command, reply in steps:
        before = machine.cycles
        out = machine.send(command)
        assert machine.reason == INPUT, out
        assert " ".join(reply.split()) in " ".join(out.split()), (command, out)
        log.append((command, out, machine.cycles - before))
    return log


def zork_run(image, stories, ram=sim.PAL2_RAM, **kw):
    """Pick Zork I from the menu and play the opening with a save and restore."""
    machine = boot(image, stories, ram=ram, **kw)
    log = play(machine, [("2", "West of House")] + ZORK_SAVE + ZORK_QUIT)
    check(machine)
    return machine, log


@pytest.fixture(name="zork_full", scope="session")
def fixture_zork_full(image, stories):
    """Zork I with all of a PAL-II's RAM."""
    return zork_run(image, stories)


def test_launcher(image, stories):
    machine = boot(image, stories)
    assert machine.output() == ""
    assert machine.kim.output.startswith(VT_OPEN + "KIMFORMER Z-MACHINE V3\r\n")
    for pick in ("0", "9", "x", ""):
        assert machine.send(pick) == pick + "\n" + MENU
    out = machine.send("1")
    assert "KIMFORMER DEMO" in out and out.endswith("\n>")
    assert machine.status == [status("Meadow", "Score: 0  Moves: 0")]
    assert machine.send("quit").endswith(MENU)
    assert machine.send("q") == "q\n"
    assert machine.reason == MONITOR
    assert machine.kim.output.endswith("q\r\n" + VT_CLOSE)
    check(machine)


def test_rerun_without_reload(image, stories):
    machine = boot(image, stories)
    machine.send("Q")
    assert machine.reason == MONITOR
    machine.kim.mpu.pc = sim.ORG
    assert machine.run().endswith(MENU)
    assert "Meadow" in machine.send("1")
    check(machine)


def test_no_drive(image, stories):
    machine = Machine(dict(stories), image=image, present=False)
    assert machine.run() == "KIMFORMER Z-MACHINE V3\nNo drive\nNo stories\n"
    assert machine.reason == MONITOR


def test_restore_missing(image, stories):
    machine = boot(image, stories)
    play(machine, [("1", "Meadow"), ("restore", "File [DEMO.SAV]: ")])
    out = machine.send("nosuch")
    assert out.startswith("nosuch\n62,FILE NOT FOUND,00,00\n")
    play(machine, [("look", "Meadow")])
    check(machine)


def test_too_little_ram(image, stories):
    machine = boot(image, stories, ram=((0, 0x3FF), (0x1700, 0x17FF), (0x2000, 0x8FFF)))
    out = machine.send("2")
    assert out.endswith("\nError 3\n" + MENU)
    check(machine)


def test_demo_walkthrough(image, stories):
    script = (REPO / "games" / "demo.walkthrough").read_text(encoding="ascii")
    expect = (REPO / "games" / "demo.expect").read_text(encoding="ascii")
    machine = boot(image, stories)
    out = machine.send("1")
    for command in script.splitlines():
        out += machine.send(command)
    assert walk.unmatched(expect.splitlines(), out.splitlines()) is None
    assert machine.drive.files[b"demo.sav"][:4] == b"KZ3\x02"
    check(machine)


@pytest.mark.zork
def test_zork_opening(zork_full):
    machine, log = zork_full
    out = "".join(o for _, o, _ in log)
    assert "ZORK I: The Great Underground Empire" in out
    assert "Release 119 / Serial number 880429" in out
    assert machine.drive.files[b"ZORK1.SAV"][:4] == b"KZ3\x02"
    assert machine.status[0] == status("West of House", "Score: 0  Moves: 0")
    assert machine.status[-2] == status("East of Chasm", "Score: 35  Moves: 15")


@pytest.mark.zork
def test_zork_restore_fresh(image, stories, zork_full):
    card = dict(stories)
    card[b"ZORK1.SAV"] = zork_full[0].drive.files[b"ZORK1.SAV"]
    machine = boot(image, card)
    steps = [("2", "West of House"), ("restore", "File [ZORK1.SAV]: ")]
    steps += [("", "Ok."), ("look", "Living Room"), ("inventory", "A brass lantern")]
    play(machine, steps)
    check(machine)


@pytest.mark.zork
def test_zork_eviction(image, stories, zork_full):
    machine, log = zork_full
    end = prof.symbols(image.with_suffix(".sym"))["__kim_end"] >> 8
    dyn = -(-int.from_bytes(stories[b"ZORK1.Z3"][14:16], "big") // 256)
    top = (end + dyn + SMALL_FRAMES) * 256 - 1
    small, small_log = zork_run(
        image, stories, ((0, 0x3FF), (0x1700, 0x17FF), (0x2000, top))
    )
    assert [o for _, o, _ in small_log[1:]] == [o for _, o, _ in log[1:]]
    assert small.drive.files[b"ZORK1.SAV"] == machine.drive.files[b"ZORK1.SAV"]
    assert small.seeks > 10 * machine.seeks


def test_cli(image, demo, tmp_path, monkeypatch, capsys):
    (tmp_path / "DEMO.Z3").write_bytes(demo.read_bytes())
    stdin = io.StringIO("1\nlook\nsave\n\nquit\nq\n")
    monkeypatch.setattr("sys.stdin", stdin)
    assert sim.main(["--dir", str(tmp_path), "--image", str(image)]) == 0
    out = capsys.readouterr()
    screen = sim.VT_STATUS.sub("", out.out)
    assert "1. DEMO.Z3" in screen and ">look\nMeadow\n" in screen
    assert "monitor after" in out.err
    assert (tmp_path / "DEMO.SAV").read_bytes()[:4] == b"KZ3\x02"


def test_profile(image, demo, tmp_path, capsys):
    story = tmp_path / "demo.z3"
    story.write_bytes(demo.read_bytes())
    funcs = dict((n, a) for a, n in prof.functions(image.with_suffix(".sym")))
    assert {"zpage", "var_get", "iec_acptr"} <= funcs.keys()
    assert min(funcs.values()) == sim.ORG
    assert prof.main(["--top", "5", "--image", str(image), str(story), "look"]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert len(lines) == 6 and lines[-1].endswith(" total")
    counts = [int(line.split()[0]) for line in lines]
    assert counts[:-1] == sorted(counts[:-1], reverse=True)
    assert 0 < sum(counts[:-1]) <= counts[-1]


@pytest.mark.zork
def test_zork_mirrored_ram(image, stories, zork_full):
    """Upper RAM that mirrors the image through an undecoded A15 is not used."""
    ram = ((0, 0x3FF), (0x1700, 0x17FF), (0x2000, 0xBFFF))
    machine, log = zork_run(image, stories, ram, mirrors=((0xC000, 0xFEFF, 0x4000),))
    assert [o for _, o, _ in log[1:]] == [o for _, o, _ in zork_full[1][1:]]
    assert machine.seeks > zork_full[0].seeks


def test_meter():
    out, err = [], io.StringIO()
    meter = sim.Meter(out.append, file=err)
    meter.feed("", 10)
    drawn = len(err.getvalue())
    assert drawn
    meter.feed("prompt>", 10)
    after = len(err.getvalue())
    meter.feed("", 5_000_000)
    assert len(err.getvalue()) == after
    meter.feed("line\n", 10)
    assert len(err.getvalue()) > after
    meter.close()
    assert out == ["prompt>", "line\n"]


def test_bench(image, demo, tmp_path, capsys):
    story = tmp_path / "demo.z3"
    story.write_bytes(demo.read_bytes())
    loads = []
    for flags in ([], ["--no-jiffy"]):
        args = ["--bench", *flags, "--image", str(image), str(story), "look"]
        assert prof.main(args) == 0
        lines = capsys.readouterr().out.splitlines()
        assert lines[0].split()[0] == "load"
        loads.append(int(lines[0].split()[1]))
        assert lines[1].split() == ["command", "interp", "io", "seeks"]
        assert lines[2].split()[0] == "look" and int(lines[2].split()[1]) > 0
        assert lines[-1].startswith("mean")
    assert loads[0] < loads[1]


def test_by_line(tmp_path):
    fake = "sh -c 'while read a; do echo f$a; echo /src/x.c:1; done'"
    pcs = {0x2000: 5, 0x2001: 7}
    counts = prof.by_line(pcs, tmp_path / "x.elf", addr2line=fake)
    assert counts == {"f0x2000 x.c:1": 5, "f0x2001 x.c:1": 7}


def test_bootstrap_from_disk(image, stories):
    """KIMDOS at $0400 loads KIMFORMER from the card; 2000 G starts it."""
    shell = packaged(0x0400)
    prg = (REPO / "build" / "kimformer.prg").read_bytes()
    machine = Machine({**stories, b"KIMFORMER": prg}, image=image, timer=0x5A)
    machine.kim.write(sim.ORG, bytes(len(prg) - 2))
    machine.kim.write(0x0400, shell.binary)
    machine.kim.mpu.pc = 0x0400
    assert machine.run().endswith(">")
    assert "$2000-$%04X" % (sim.ORG + len(prg) - 3) in machine.send("L KIMFORMER")
    machine.send("Q")
    assert machine.reason == MONITOR
    assert machine.kim.read(sim.ORG, len(prg) - 2) == prg[2:]
    machine.zp = machine.kim.read(sim.MONITOR_ZP.start, len(sim.MONITOR_ZP))
    machine.kim.mpu.pc = sim.ORG
    assert machine.run().endswith(MENU)
    play(machine, [("1", "Meadow")])
    check(machine)
