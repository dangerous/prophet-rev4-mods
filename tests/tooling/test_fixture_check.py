"""Acceptance tests for the fixture guard (docs/SPEC.md: "Required input files"): a test
module whose input files are missing or wrong is skipped with a warning, not failed."""
import contextlib
import hashlib
import io
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests import fixture_check

ROOT = Path(__file__).resolve().parents[2]
GOOD = bytes(range(256)) * 4
GOOD_HASH = hashlib.sha256(GOOD).hexdigest()
OTHER_HASH = "0" * 64
REQUIRED = {
    "prophet5_main_2.1.0.syx",
    "prophet5Panel_v1.1.3.syx",
    "V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx",
}


class FixtureGuardTests(unittest.TestCase):
    def _dir(self, files):
        d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, d)
        (d / "SHA256SUMS").write_text("%s  good.syx\n%s  other.syx\n" % (GOOD_HASH, OTHER_HASH))
        for name, data in files.items():
            (d / name).write_bytes(data)
        return d

    def test_present_file_with_matching_hash_passes_silently(self):
        d = self._dir({"good.syx": GOOD})
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            fixture_check.require(["good.syx"], d)
        self.assertEqual(err.getvalue(), "")

    def test_missing_file_skips_naming_file_hash_and_readme(self):
        d = self._dir({})
        err = io.StringIO()
        with contextlib.redirect_stderr(err), self.assertRaises(unittest.SkipTest) as cm:
            fixture_check.require(["good.syx"], d)
        for text in (str(cm.exception), err.getvalue()):
            self.assertIn("good.syx", text)
            self.assertIn("missing", text)
            self.assertIn(GOOD_HASH, text)
            self.assertIn("fixtures/README.md", text)

    def test_wrong_hash_skips_reporting_both_hashes(self):
        d = self._dir({"good.syx": b"not the real file"})
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(unittest.SkipTest) as cm:
            fixture_check.require(["good.syx"], d)
        self.assertIn(GOOD_HASH, str(cm.exception))
        self.assertIn(hashlib.sha256(b"not the real file").hexdigest(), str(cm.exception))
        self.assertIn("fixtures/README.md", str(cm.exception))

    def test_warning_is_printed_once_per_process(self):
        d = self._dir({})
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            for _ in range(2):
                with self.assertRaises(unittest.SkipTest):
                    fixture_check.require(["other.syx"], d)
        self.assertEqual(err.getvalue().count("other.syx"), 1)

    def test_file_not_listed_in_sha256sums_is_a_hard_error(self):
        d = self._dir({})
        with self.assertRaises(KeyError):
            fixture_check.require(["unlisted.syx"], d)

    def test_repo_sha256sums_lists_exactly_the_three_inputs(self):
        self.assertEqual(set(fixture_check.expected_hashes()), REQUIRED)
        for h in fixture_check.expected_hashes().values():
            self.assertRegex(h, r"^[0-9a-f]{64}$")


def _cli(*args):
    return subprocess.run(
        [sys.executable, "-m", "tools", *map(str, args)],
        cwd=ROOT, capture_output=True, text=True,
    )


class MissingInputCliTests(unittest.TestCase):
    """A missing input file gives a one-line pointer to fixtures/README.md, not a traceback."""

    def assert_one_line_error(self, r, missing: Path):
        self.assertEqual(r.returncode, 1, r.stderr)
        self.assertNotIn("Traceback", r.stderr)
        self.assertEqual(len(r.stderr.strip().splitlines()), 1, r.stderr)
        self.assertIn(str(missing), r.stderr)
        self.assertIn("fixtures/README.md", r.stderr)

    def test_build_with_missing_base_writes_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            missing = Path(d) / "V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx"
            out = Path(d) / "out.syx"
            r = _cli("build", "--base", missing, "--wrapper", Path(d) / "w.bin",
                     "--map", Path(d) / "w.map", "--hooks", Path(d) / "h.json", "-o", out)
            self.assert_one_line_error(r, missing)
            self.assertFalse(out.exists())

    def test_inspect_with_missing_file(self):
        with tempfile.TemporaryDirectory() as d:
            missing = Path(d) / "prophet5_main_2.1.0.syx"
            self.assert_one_line_error(_cli("inspect", missing), missing)


if __name__ == "__main__":
    unittest.main()
