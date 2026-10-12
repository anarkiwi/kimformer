"""Download, verify and cache third-party test fixtures.

Fixtures that are not ours are fetched once into ``tests/fixtures/cache``
(override with ``KIMFORMER_CACHE``), checked against pinned SHA-256 digests
and written atomically so concurrent test workers can share the cache.
"""

import hashlib
import io
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
PUNY_VERSION = "5_14_1"
IFARCHIVE_TOOLS = "https://ifarchive.org/if-archive/infocom/interpreters/tools/"

SOURCES = {
    "zork1.z3": (
        "https://raw.githubusercontent.com/historicalsource/zork1/master/"
        "COMPILED/zork1.z3",
        "37084966477dff679282de42974b2077156b1bd68fad92a65d4ea94d8eb64d79",
    ),
    "czech_0_8.zip": (
        IFARCHIVE_TOOLS + "czech_0_8.zip",
        "d0795723e98563050575c4d3c08fdd29ade2543e132d8190255763e3bf6fef6a",
    ),
    "praxix.zip": (
        IFARCHIVE_TOOLS + "praxix.zip",
        "c654d656fb426ae1b386e5ab9c44cc5062eade4a8c3bce69c9e61191221501ad",
    ),
    "etude.tar.Z": (
        IFARCHIVE_TOOLS + "etude.tar.Z",
        "f49f31a345db477240c9be29d6015afc45a0b057bafcad09666ba6b068a0d56a",
    ),
    f"PunyInform-{PUNY_VERSION}.tar.gz": (
        "https://github.com/johanberntsson/PunyInform/archive/refs/tags/"
        f"v{PUNY_VERSION}.tar.gz",
        "03f935a265a11ebe9e74b20812ac71b5e95e1bec93af4dd4fb892c0df73c3b1d",
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


def _member(path, data):
    """Writes data to path atomically; returns path."""
    tmp = _atomic(path)
    tmp.write_bytes(data)
    os.replace(tmp, path)
    return path


def czech(cache=CACHE):
    """CZECH paths: ``z3``, ``z4`` and ``z8`` (compiled here), ``z5`` (as
    shipped), ``inf`` and the reference transcripts ``out3`` to ``out8``."""
    cache = Path(cache)
    root = cache / "czech"
    shipped = ("inf", "z5", "out3", "out4", "out5", "out8")
    paths = {k: root / f"czech.{k}" for k in shipped + ("z3", "z4", "z8")}
    if not all(paths[k].exists() for k in shipped):
        with zipfile.ZipFile(fetch("czech_0_8.zip", cache)) as zf:
            for key in shipped:
                _member(paths[key], zf.read(paths[key].name))
    for version in (3, 4, 8):
        if not paths[f"z{version}"].exists():
            compile_story(paths["inf"], paths[f"z{version}"], version, cache)
    return paths


def praxix(cache=CACHE):
    """Path to praxix.z5, Andrew Plotkin's Z-machine test suite."""
    path = Path(cache) / "praxix" / "praxix.z5"
    if not path.exists():
        with zipfile.ZipFile(fetch("praxix.zip", cache)) as zf:
            _member(path, zf.read("praxix.z5"))
    return path


def etude(cache=CACHE):
    """Path to etude.z5 (TerpEtude) from its compress(1) archive."""
    path = Path(cache) / "etude" / "etude.z5"
    if not path.exists():
        raw = subprocess.run(
            ["gzip", "-dc", str(fetch("etude.tar.Z", cache))],
            capture_output=True,
            check=True,
        ).stdout
        with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
            _member(path, tar.extractfile("etude/etude.z5").read())
    return path


def library_of_horror(cache=CACHE):
    """Path to The Library Of Horror, the PunyInform demo game, compiled for
    version 5 from the pinned PunyInform release."""
    cache = Path(cache)
    out = cache / "loh" / "loh.z5"
    if out.exists():
        return out
    root = cache / f"PunyInform-{PUNY_VERSION}"
    if not root.exists():
        with tempfile.TemporaryDirectory(dir=cache) as tmp:
            with tarfile.open(fetch(f"PunyInform-{PUNY_VERSION}.tar.gz", cache)) as tar:
                tar.extractall(tmp, filter="data")
            os.replace(Path(tmp) / root.name, root)
    out.parent.mkdir(parents=True, exist_ok=True)
    return compile_story(
        root / "library_of_horror.inf",
        out,
        5,
        cache,
        flags=(f"+include_path={root / 'lib'}",),
    )


def main():
    """Fetch and build every fixture."""
    for name in tqdm(SOURCES, desc="fixtures"):
        fetch(name)
    for build in tqdm((czech, praxix, etude, library_of_horror), desc="builds"):
        build()


if __name__ == "__main__":
    main()
