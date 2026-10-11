"""The board smoke test against a fake KIM-1 on a pty."""

import os
import pty

import pytest

from kimformer import board
from kimformer.sim import IMAGE, ORG
from tests.conftest import REPO
from tests.fake_kim import FakeKim

BAUD = "115200"
PLAIN = REPO / "build" / "plain" / "kimformer.bin"


@pytest.fixture(name="card", scope="module")
def fixture_card(demo):
    """The demo and a file that is not a story."""
    return {b"DEMO.Z3": demo.read_bytes(), b"README": b"not a story"}


@pytest.fixture(name="fake")
def fixture_fake(card):
    """A fake board with the card; closed after the test."""
    kim = FakeKim(card)
    yield kim
    kim.close()


def preload(kim, image):
    """Put a binary image at $2000, as if loaded earlier."""
    if not image.exists():
        pytest.skip(f"{image} missing: run make kim")
    kim.machine.kim.write(ORG, image.read_bytes())
    return kim


def test_load_and_play(fake, tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(board, "LOGS", tmp_path / "logs")
    args = [
        "--port",
        fake.dev,
        "--baud",
        BAUD,
        "--image",
        str(IMAGE.with_suffix(".ptp")),
    ]
    assert board.main(args) == 0
    assert fake.machine.kim.read(ORG, IMAGE.stat().st_size) == IMAGE.read_bytes()
    out = capsys.readouterr().out
    script = (REPO / "games" / "demo.walkthrough").read_text("ascii").splitlines()
    latency = [line.split(None, 1) for line in out.splitlines() if "s  " in line]
    assert [c for _, c in latency] == ["DEMO.Z3"] + script
    assert all(float(s[:-1]) > 0 for s, _ in latency)
    assert "all 22 expected lines found" in out
    (log,) = (tmp_path / "logs").iterdir()
    text = log.read_text("latin-1")
    assert "\x1b" not in text and "\x00" not in text and "\r" not in text
    assert "1. DEMO.Z3\nStory (Q quits): 1\n" in text
    assert "\n>look\n" in text and "File [DEMO.SAV]: demo.sav\n" in text


@pytest.mark.parametrize("image", [IMAGE, PLAIN])
def test_missing_line(fake, image, tmp_path, capsys):
    preload(fake, image)
    (tmp_path / "script").write_text("look\nscore\n")
    (tmp_path / "expect").write_text("Meadow\nYou have scored 0\nMeadow\n")
    log = tmp_path / "log"
    args = ["--port", fake.dev, "--baud", BAUD, "--no-load", "--log", str(log)]
    args += ["--script", str(tmp_path / "script"), "--expect", str(tmp_path / "expect")]
    assert board.main(args) == 1
    assert capsys.readouterr().out.endswith("missing: Meadow\n")
    text = log.read_text("latin-1")
    assert "\n>score\nYou have scored 0 out of 50" in text and text.endswith("\n>")


def test_no_expect(fake, tmp_path):
    preload(fake, IMAGE)
    (tmp_path / "script").write_text("look\n")
    args = [
        "--port",
        fake.dev,
        "--baud",
        BAUD,
        "--no-load",
        "--log",
        str(tmp_path / "log"),
    ]
    assert board.main(args + ["--script", str(tmp_path / "script")]) == 0


def test_not_in_menu(fake, tmp_path):
    preload(fake, IMAGE)
    args = ["--port", fake.dev, "--baud", BAUD, "--no-load", "--story", "zork1.z3"]
    with pytest.raises(LookupError, match="zork1.z3 not in the menu"):
        board.main(args + ["--log", str(tmp_path / "log")])


def test_timeout(tmp_path):
    master, slave = pty.openpty()
    try:
        with open(tmp_path / "log", "w", encoding="latin-1") as log:
            session = board.Session(os.ttyname(slave), int(BAUD), log)
            os.write(master, b"\r\nKIMFORMER")
            with pytest.raises(TimeoutError, match="no prompt"):
                session.wait_prompt(0.3)
            os.write(master, b"\x1b[2;24r\r\n>")
            with pytest.raises(TimeoutError):
                session.wait_prompt(0.3)
            assert session.vt100
            session.close()
        assert (tmp_path / "log").read_text("latin-1").count("[timeout]") == 2
    finally:
        os.close(master)
        os.close(slave)
