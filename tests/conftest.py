"""Shared fixtures for the host interpreter tests."""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

from kimformer import fixtures

REPO = Path(__file__).resolve().parent.parent
ZM = Path(os.environ.get("ZM", REPO / "build" / "cov" / "zm"))


class Runner:
    """Runs the host interpreter in a directory of story files."""

    def __init__(self, workdir):
        self.dir = Path(workdir)
        self.dir.mkdir(parents=True, exist_ok=True)

    def add(self, story, name=None):
        """Copy a story into the run directory; returns its file name."""
        name = name or Path(story).name
        shutil.copyfile(story, self.dir / name)
        return name

    def run(self, *args, commands=(), stdin=None, timeout=120):
        """Run zm with args, feeding commands one per line."""
        if stdin is None:
            stdin = "".join(c + "\n" for c in commands)
        if not ZM.exists():
            pytest.fail(f"{ZM} missing: run make cov")
        return subprocess.run(
            [str(ZM), "-d", str(self.dir), *map(str, args)],
            input=stdin.encode("latin-1"),
            capture_output=True,
            timeout=timeout,
            check=False,
        )


def text(result):
    """Interpreter stdout with the load-progress line removed."""
    out = result.stdout.decode("latin-1")
    return out.split("\n", 1)[1] if out.startswith(".") else out


def flat(result):
    """Interpreter output with all whitespace runs collapsed."""
    return " ".join(text(result).split())


@pytest.fixture(name="runner")
def fixture_runner(tmp_path):
    """A runner in a fresh directory."""
    return Runner(tmp_path)


@pytest.fixture(name="zork", scope="session")
def fixture_zork():
    """Zork I release 119."""
    return fixtures.fetch("zork1.z3")


@pytest.fixture(name="czech", scope="session")
def fixture_czech():
    """CZECH compiled for version 3, plus its shipped files."""
    return fixtures.czech()


@pytest.fixture(name="probe", scope="session")
def fixture_probe(tmp_path_factory):
    """The probe story compiled for version 3."""
    out = tmp_path_factory.mktemp("probe") / "probe.z3"
    return fixtures.compile_story(REPO / "tests" / "stories" / "probe.inf", out)


@pytest.fixture(name="clock", scope="session")
def fixture_clock(tmp_path_factory):
    """The status line probe: a time game."""
    out = tmp_path_factory.mktemp("clock") / "clock.z3"
    return fixtures.compile_story(REPO / "tests" / "stories" / "clock.inf", out)


def demo_story(tmp_path_factory, version):
    """The demo adventure compiled for version, as games/games.mk does."""
    out = tmp_path_factory.mktemp("demo") / f"demo.z{version}"
    flags = ("-e", "-~S", "$OMIT_UNUSED_ROUTINES=1")
    return fixtures.compile_story(
        REPO / "games" / "demo.inf", out, version, flags=flags
    )


@pytest.fixture(name="demo", scope="session")
def fixture_demo(tmp_path_factory):
    """The demo adventure for version 3."""
    return demo_story(tmp_path_factory, 3)


@pytest.fixture(name="demo5", scope="session")
def fixture_demo5(tmp_path_factory):
    """The demo adventure for version 5."""
    return demo_story(tmp_path_factory, 5)


@pytest.fixture(name="probe5", scope="session")
def fixture_probe5(tmp_path_factory):
    """The version 5 probe story."""
    out = tmp_path_factory.mktemp("probe5") / "probe5.z5"
    return fixtures.compile_story(REPO / "tests" / "stories" / "probe5.inf", out, 5)


@pytest.fixture(name="loh", scope="session")
def fixture_loh():
    """The Library Of Horror, PunyInform's demo game, for version 5."""
    return fixtures.library_of_horror()
