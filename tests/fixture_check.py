"""Guard for test modules that read the input files in fixtures/ (docs/SPEC.md: "Required
input files"). Call `require([...])` from a module's setUpModule(): if a file is missing, or
its SHA-256 differs from fixtures/SHA256SUMS, the module is skipped and a warning naming
the file is printed once per process."""
from __future__ import annotations

import hashlib
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "fixtures"
README = "fixtures/README.md"

_warned: set = set()


def expected_hashes(directory: Path = FIXTURES) -> dict:
    """{name: sha256} from directory/SHA256SUMS ('<hash>  <name>' per line, shasum format)."""
    out = {}
    for line in (directory / "SHA256SUMS").read_text().splitlines():
        if line.strip():
            digest, name = line.split(None, 1)
            out[name.strip().lstrip("*")] = digest.lower()
    return out


def require(names, directory: Path = FIXTURES) -> None:
    """Raise unittest.SkipTest unless every named file is present with the listed hash.

    A name that SHA256SUMS does not list is a programming error (KeyError), not a skip."""
    expected = expected_hashes(directory)
    problems = []
    for name in names:
        want = expected[name]
        path = directory / name
        if not path.is_file():
            problems.append("%s is missing (expected SHA-256 %s)" % (path, want))
        else:
            got = hashlib.sha256(path.read_bytes()).hexdigest()
            if got != want:
                problems.append("%s has SHA-256 %s, expected %s" % (path, got, want))
    if not problems:
        return
    for problem in problems:
        if problem not in _warned:
            _warned.add(problem)
            print("WARNING: %s -- see %s" % (problem, README), file=sys.stderr)
    raise unittest.SkipTest("; ".join(problems) + " -- see " + README)
