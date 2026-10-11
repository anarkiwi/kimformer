"""Fixture download, verification and Inform compilation."""

import hashlib
import io
import tarfile
import zipfile

import pytest

from kimformer import fixtures

FAKE_INFORM = r"""
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  FILE *f;
  if (argc != 4 || strstr(argv[2], "bad")) {
    puts("bad source");
    return 1;
  }
  f = fopen(argv[3], "w");
  fprintf(f, "%s %s", argv[1], argv[2]);
  return fclose(f);
}
"""


def source(tmp_path, name, data):
    """A file:// source entry for data."""
    path = tmp_path / "src" / name
    path.parent.mkdir(exist_ok=True)
    path.write_bytes(data)
    return {name: (path.as_uri(), hashlib.sha256(data).hexdigest())}


@pytest.fixture(name="fake_inform")
def fixture_fake_inform(tmp_path, monkeypatch):
    """A stand-in Inform compiler; returns its cache directory."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        data = FAKE_INFORM.encode()
        info = tarfile.TarInfo("Inform6-x/inform.c")
        info.size = len(data)
        tar.addfile(info, io.BytesIO(data))
    name = f"inform6-{fixtures.INFORM_VERSION}.tar.gz"
    monkeypatch.setitem(
        fixtures.SOURCES, name, source(tmp_path, name, buf.getvalue())[name]
    )
    return tmp_path / "cache"


def test_fetch(tmp_path, monkeypatch):
    sources = source(tmp_path, "a.bin", b"payload")
    cache = tmp_path / "cache"
    path = fixtures.fetch("a.bin", cache, sources)
    assert path.read_bytes() == b"payload"
    path.write_bytes(b"corrupt")
    assert fixtures.fetch("a.bin", cache, sources).read_bytes() == b"payload"

    def offline(*_args, **_kwargs):
        raise AssertionError("cached fixture fetched again")

    monkeypatch.setattr(fixtures.urllib.request, "urlopen", offline)
    assert fixtures.fetch("a.bin", cache, sources) == path
    assert sorted(p.name for p in cache.iterdir()) == ["a.bin"]


def test_fetch_bad_digest(tmp_path):
    url, _ = source(tmp_path, "a.bin", b"payload")["a.bin"]
    cache = tmp_path / "cache"
    with pytest.raises(fixtures.FixtureError, match="sha256"):
        fixtures.fetch("a.bin", cache, {"a.bin": (url, "0" * 64)})
    assert not list(cache.iterdir())


def test_compile_story(tmp_path, fake_inform):
    out = fixtures.compile_story("good.inf", tmp_path / "good.z3", 5, fake_inform)
    assert out.read_text(encoding="ascii") == "-v5 good.inf"
    assert fixtures.inform(fake_inform).exists()
    with pytest.raises(fixtures.FixtureError, match="bad source"):
        fixtures.compile_story("bad.inf", tmp_path / "bad.z3", 3, fake_inform)
    assert not (tmp_path / "bad.z3").exists()


def test_czech(tmp_path, fake_inform, monkeypatch):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        for name in ("czech.inf", "czech.z5", "czech.out3", "README.txt"):
            zf.writestr(name, name)
    monkeypatch.setitem(
        fixtures.SOURCES,
        "czech_0_8.zip",
        source(tmp_path, "czech_0_8.zip", buf.getvalue())["czech_0_8.zip"],
    )
    paths = fixtures.czech(fake_inform)
    assert paths["out3"].read_text(encoding="ascii") == "czech.out3"
    assert paths["z5"].read_text(encoding="ascii") == "czech.z5"
    assert paths["z3"].read_text(encoding="ascii") == "-v3 " + str(paths["inf"])
    assert fixtures.czech(fake_inform) == paths


def test_main(monkeypatch):
    fetched = []
    monkeypatch.setattr(fixtures, "fetch", fetched.append)
    monkeypatch.setattr(fixtures, "czech", lambda: fetched.append("czech"))
    fixtures.main()
    assert fetched == [*fixtures.SOURCES, "czech"]
