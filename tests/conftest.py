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


@pytest.fixture(name="demo", scope="session")
def fixture_demo(tmp_path_factory):
    """The demo adventure, compiled as games/games.mk does."""
    out = tmp_path_factory.mktemp("demo") / "demo.z3"
    flags = ("-e", "-~S", "$OMIT_UNUSED_ROUTINES=1")
    return fixtures.compile_story(REPO / "games" / "demo.inf", out, flags=flags)
