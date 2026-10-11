"""Download, verify and cache third-party test fixtures.

Fixtures that are not ours are fetched once into ``tests/fixtures/cache``
(override with ``KIMFORMER_CACHE``), checked against pinned SHA-256 digests
and written atomically so concurrent test workers can share the cache.
"""

import hashlib
import os
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

from tqdm import tqdm

REPO = Path(__file__).resolve().parent.parent
CACHE = Path(os.environ.get("KIMFORMER_CACHE", REPO / "tests" / "fixtures" / "cache"))

INFORM_VERSION = "6.43"

SOURCES = {
    "zork1.z3": (
        "https://raw.githubusercontent.com/historicalsource/zork1/master/"
        "COMPILED/zork1.z3",
        "37084966477dff679282de42974b2077156b1bd68fad92a65d4ea94d8eb64d79",
    ),
    "czech_0_8.zip": (
        "https://ifarchive.org/if-archive/infocom/interpreters/tools/czech_0_8.zip",
        "d0795723e98563050575c4d3c08fdd29ade2543e132d8190255763e3bf6fef6a",
    ),
    f"inform6-{INFORM_VERSION}.tar.gz": (
        "https://github.com/DavidKinder/Inform6/archive/refs/tags/"
        f"v{INFORM_VERSION}.tar.gz",
        "6a66bb83c931eeebad0a57c38f96c6aeafac9667e8a6d2815efabab3cd07e2b2",
    ),
}


class FixtureError(RuntimeError):
    """A fixture could not be fetched, verified or built."""


def sha256(path):
    """Hex SHA-256 digest of a file."""
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def _atomic(path):
    """A temporary file next to path, to be os.replace()d into place."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.")
    os.close(fd)
    return Path(tmp)


def fetch(name, cache=CACHE, sources=None):
    """Path to the cached fixture ``name``, downloading it if needed."""
    url, digest = (sources or SOURCES)[name]
    path = Path(cache) / name
    if path.exists() and sha256(path) == digest:
        return path
    tmp = _atomic(path)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "kimformer"})
        with urllib.request.urlopen(req, timeout=60) as resp, open(tmp, "wb") as out:
            total = int(resp.headers.get("Content-Length") or 0) or None
            with tqdm(total=total, unit="B", unit_scale=True, desc=name) as progress:
                for block in iter(lambda: resp.read(1 << 16), b""):
                    out.write(block)
                    progress.update(len(block))
        got = sha256(tmp)
        if got != digest:
            raise FixtureError(f"{name}: sha256 {got}, expected {digest}")
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)
    return path


def inform(cache=CACHE):
    """Path to an Inform 6 compiler built from the pinned source release."""
    cache = Path(cache)
    exe = cache / f"inform6-{INFORM_VERSION}" / "inform"
    if exe.exists():
        return exe
    src = fetch(f"inform6-{INFORM_VERSION}.tar.gz", cache)
    exe.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=exe.parent) as tmp:
        with tarfile.open(src) as tar:
            tar.extractall(tmp, filter="data")
        sources = sorted(Path(tmp).glob("*/*.c"))
        out = Path(tmp) / "inform"
        subprocess.run(
            ["cc", "-O2", "-w", "-o", str(out), *map(str, sources)], check=True
        )
        os.replace(out, exe)
    return exe


def compile_story(source, out, version=3, cache=CACHE, flags=()):
    """Compile Inform 6 ``source`` to the story file ``out``."""
    out = Path(out)
    tmp = _atomic(out)
    try:
        result = subprocess.run(
            [str(inform(cache)), f"-v{version}", *flags, str(source), str(tmp)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode:
            raise FixtureError(f"inform {source}: {result.stdout}{result.stderr}")
        os.replace(tmp, out)
    finally:
        tmp.unlink(missing_ok=True)
    return out


def czech(cache=CACHE):
    """CZECH paths: ``z3`` (compiled here), ``z5`` (as shipped), ``out3``."""
    cache = Path(cache)
    root = cache / "czech"
    paths = {"inf": root / "czech.inf", "z5": root / "czech.z5"}
    paths["out3"] = root / "czech.out3"
    paths["z3"] = root / "czech.z3"
    if not all(paths[k].exists() for k in ("inf", "z5", "out3")):
        with zipfile.ZipFile(fetch("czech_0_8.zip", cache)) as zf:
            for key in ("inf", "z5", "out3"):
                tmp = _atomic(paths[key])
                tmp.write_bytes(zf.read(paths[key].name))
                os.replace(tmp, paths[key])
    if not paths["z3"].exists():
        compile_story(paths["inf"], paths["z3"], 3, cache)
    return paths


def main():
    """Fetch and build every fixture."""
    for name in tqdm(SOURCES, desc="fixtures"):
        fetch(name)
    czech()


if __name__ == "__main__":
    main()
