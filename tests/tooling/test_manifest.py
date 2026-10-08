"""Acceptance tests for the patcher (docs/SPEC.md: "Patcher (distribution)"): the patch
manifest, the reference applier and the page's JavaScript applier. The image under test
is built with the stub engine from test_build, so no cross toolchain is needed."""
import base64
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests import fixture_check
from tests.tooling.test_build import REC_BASE, REC_SIZE, _write_inputs
from tools import build, manifest, records, syx

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "fixtures" / "prophet5_main_2.1.0.syx"
SITE = ROOT / "site"


def setUpModule():
    fixture_check.require([MAIN.name])


def _cli(*args):
    return subprocess.run([sys.executable, "-m", "tools", *map(str, args)],
                          cwd=ROOT, capture_output=True, text=True)


class StubImage(unittest.TestCase):
    """A base file and an image built from it with the stub engine."""

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        base, wbin, wmap, hooks = _write_inputs(self.dir)
        self.image_path = self.dir / "image.syx"
        build.build_image(base, wbin, wmap, hooks, self.image_path, REC_BASE, REC_SIZE)
        self.base_path = base
        self.base = base.read_bytes()
        self.image = self.image_path.read_bytes()
        self.text = manifest.generate_js(self.base, self.image, commit="abc1234", built="2026-10-08")
        self.m = manifest.parse_js(self.text)

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)


class ManifestFormatTests(StubImage):
    def test_assigns_window_patch_with_the_described_fields(self):
        self.assertTrue(self.text.startswith("window.PATCH = "))
        self.assertTrue(self.text.rstrip().endswith(";"))
        m = self.m
        self.assertEqual(m["format"], 1)
        self.assertEqual(m["name"], "prophet10_native")
        self.assertEqual(m["built"], "2026-10-08")
        self.assertEqual(m["commit"], "abc1234")
        self.assertEqual(m["base"], {"name": "prophet5_main_2.1.0.syx", "size": len(self.base),
                                     "sha256": hashlib.sha256(self.base).hexdigest()})
        self.assertEqual(m["result"], {"name": "prophet10_native.syx", "size": len(self.image),
                                       "sha256": hashlib.sha256(self.image).hexdigest()})
        self.assertEqual(set(m), {"format", "name", "built", "commit", "base", "result", "spans"})

    def test_spans_are_ascending_minimal_and_end_with_the_appended_record(self):
        base_payload = syx.decode(self.base).payload
        image_payload = syx.decode(self.image).payload
        spans = [(s["offset"], base64.b64decode(s["data"]), s.get("insert", False)) for s in self.m["spans"]]
        self.assertGreater(len(spans), 1)
        for (o1, d1, _), (o2, _, _) in zip(spans, spans[1:]):
            self.assertLess(o1 + len(d1), o2 + 1)                     # ascending, non-overlapping
        for off, data, insert in spans[:-1]:                          # replacements: the image's bytes ...
            self.assertFalse(insert)
            self.assertEqual(image_payload[off:off + len(data)], data)
            self.assertNotEqual(base_payload[off], data[0])           # ... trimmed to real differences
            self.assertNotEqual(base_payload[off + len(data) - 1], data[-1])
        off, data, insert = spans[-1]
        self.assertTrue(insert)                                       # the engine record, inserted ...
        image_b = records.parse_images(base_payload)[1].records[0].offset
        self.assertEqual(off, image_b)                                # ... at the end of image A
        self.assertEqual(data, image_payload[off:off + len(data)])
        self.assertEqual(len(data), 16 + REC_SIZE)
        self.assertEqual(image_payload[off + len(data):], base_payload[off:])   # the SHARC image: shifted, not copied
        self.assertLess(sum(len(d) for _, d, i in spans if not i), 512)        # the edits: a few hundred bytes
        self.assertLess(len(self.text), 50000)                                  # so the manifest stays small

    def test_nearby_differences_merge_into_one_span(self):
        base = bytes(200)
        out = bytearray(base)
        out[10] = 1; out[20] = 1                                      # 9 apart: one span 10..21
        out[60] = 1                                                   # on its own
        out[76] = 1                                                   # 16 bytes past 60: not merged
        spans = manifest.diff_spans(bytes(base), bytes(out) + b"tail!")
        self.assertEqual([(o, len(d), i) for o, d, i in spans],
                         [(10, 11, False), (60, 1, False), (76, 1, False), (200, 5, True)])
        with self.assertRaises(manifest.ManifestError):
            manifest.diff_spans(base, base[:100])                     # a shorter image is not a patch


class ApplyTests(StubImage):
    def test_apply_reproduces_the_build_byte_for_byte(self):
        self.assertEqual(manifest.apply(self.m, self.base), self.image)

    def test_wrong_base_is_refused_naming_both_hashes(self):
        other = syx.encode(b"\x00" + syx.decode(self.base).payload[1:], "main")   # valid, not the file
        with self.assertRaises(manifest.ManifestError) as cm:
            manifest.apply(self.m, other)
        msg = str(cm.exception)
        self.assertIn(hashlib.sha256(other).hexdigest(), msg)
        self.assertIn(self.m["base"]["sha256"], msg)

    def test_tampered_manifest_is_refused(self):
        bad = json.loads(json.dumps(self.m))
        data = bytearray(base64.b64decode(bad["spans"][-1]["data"]))
        data[100] ^= 0xFF
        bad["spans"][-1]["data"] = base64.b64encode(bytes(data)).decode()
        with self.assertRaises(manifest.ManifestError) as cm:
            manifest.apply(bad, self.base)
        self.assertIn("result", str(cm.exception).lower())
        bad = json.loads(json.dumps(self.m))
        bad["result"]["sha256"] = "0" * 64
        with self.assertRaises(manifest.ManifestError):
            manifest.apply(bad, self.base)

    def test_cli_manifest_and_apply(self):
        mpath, out = self.dir / "manifest.js", self.dir / "out.syx"
        r = _cli("manifest", "--base", self.base_path, "--image", self.image_path, "-o", mpath,
                 "--commit", "abc1234", "--built", "2026-10-08")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(mpath.read_text(), self.text)
        r = _cli("apply", mpath, self.base_path, out)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(out.read_bytes(), self.image)
        out.unlink()
        other = self.dir / "other.syx"
        other.write_bytes(syx.encode(b"\x00" + syx.decode(self.base).payload[1:], "main"))
        r = _cli("apply", mpath, other, out)
        self.assertEqual(r.returncode, 1)
        self.assertEqual(len(r.stderr.strip().splitlines()), 1)      # one line, nothing written
        self.assertIn(self.m["base"]["sha256"], r.stderr)
        self.assertFalse(out.exists())

    def test_default_commit_and_date_come_from_git_and_today(self):
        text = manifest.generate_js(self.base, self.image)
        m = manifest.parse_js(text)
        self.assertRegex(m["commit"], r"^([0-9a-f]{7,}|unknown)$")
        self.assertRegex(m["built"], r"^\d{4}-\d{2}-\d{2}$")


class PageTests(StubImage):
    def test_page_loads_only_its_own_files_and_no_analytics(self):
        html = (SITE / "index.html").read_text()
        for tag in re.findall(r"<(?:script|link|img|iframe)\b[^>]*>", html):
            self.assertNotRegex(tag, r"(?:src|href)\s*=\s*[\"'](?:https?:)?//", tag)
        self.assertEqual(sorted(re.findall(r"<script\s+src=[\"']([^\"']+)[\"']", html)),
                         ["manifest.js", "patcher.js"])
        self.assertNotIn("analytics", html.lower())
        self.assertNotIn("fetch(", html)
        for f in ("index.html", "patcher.js"):
            self.assertNotIn("fetch(", (SITE / f).read_text())
            self.assertNotIn("XMLHttpRequest", (SITE / f).read_text())

    def test_javascript_applier_matches_the_reference(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node not installed")
        (self.dir / "manifest.js").write_text(self.text)
        other = self.dir / "other.syx"
        other.write_bytes(syx.encode(b"\x00" + syx.decode(self.base).payload[1:], "main"))
        script = r"""
const fs = require('fs'); const path = require('path');
const [site, manifestJs, baseFile, outFile, otherFile] = process.argv.slice(1);   // node -e: argv[1] is the first argument
const P = require(path.join(site, 'patcher.js'));
global.window = {}; eval(fs.readFileSync(manifestJs, 'utf8')); const patch = window.PATCH;
const base = new Uint8Array(fs.readFileSync(baseFile));
const r = P.apply(patch, base);
if (!r.ok) { console.error('unexpected: ' + r.error); process.exit(2); }
fs.writeFileSync(outFile, Buffer.from(r.bytes));
const wrong = P.apply(patch, new Uint8Array(fs.readFileSync(otherFile)));
if (wrong.ok || !/not .*Main OS 2\.1\.0/i.test(wrong.error) || !wrong.error.includes(patch.base.sha256)) {
  console.error('wrong base not refused: ' + wrong.error); process.exit(3); }
console.log(r.sha256);
"""
        out = self.dir / "js.syx"
        r = subprocess.run([node, "-e", script, "--", str(SITE), str(self.dir / "manifest.js"),
                            str(self.base_path), str(out), str(other)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(out.read_bytes(), self.image)
        self.assertEqual(r.stdout.strip(), hashlib.sha256(self.image).hexdigest())


if __name__ == "__main__":
    unittest.main()
