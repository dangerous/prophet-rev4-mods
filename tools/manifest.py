"""Patch manifest (docs/SPEC.md: "Patcher (distribution)"): the byte spans by which the
built image differs from the stock file, published as site/manifest.js and applied in the
browser by site/patcher.js or here by `python3 -m tools apply`."""
from __future__ import annotations

import base64
import datetime
import hashlib
import json
import subprocess
from pathlib import Path
from typing import List, Optional, Tuple

from . import records, syx

FORMAT = 1
NAME = "prophet10_native"
BASE_NAME = "prophet5_main_2.1.0.syx"
RESULT_PATTERN = "prophet5_main_2.1.0_patched_%s.syx"   # Sequential's naming pattern, plus our version
ROOT = Path(__file__).resolve().parents[1]
MERGE_GAP = 16                     # differences fewer than this many bytes apart share a span
PREFIX = "window.PATCH = "


class ManifestError(Exception):
    """The manifest cannot be made or applied; nothing is written."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


Span = Tuple[int, bytes, bool]          # (offset in the base payload, bytes, insert?)


def diff_spans(base: bytes, out: bytes) -> List[Span]:
    """Replacement spans by which `out` differs from `base`: ascending, non-overlapping, each
    trimmed to real differences, differences less than MERGE_GAP apart merged. Bytes of `out`
    beyond len(base) become one insertion at len(base)."""
    n = len(base)
    if len(out) < n:
        raise ManifestError("the image payload is shorter than the base payload")
    spans: List[Span] = []
    i = 0
    while i < n:
        if base[i] == out[i]:
            i += 1
            continue
        start = last = i
        j = i + 1
        while j < n and j - last < MERGE_GAP:
            if base[j] != out[j]:
                last = j
            j += 1
        spans.append((start, bytes(out[start:last + 1]), False))
        i = last + 1
    if len(out) > n:
        spans.append((n, bytes(out[n:]), True))
    return spans


def image_spans(base_payload: bytes, image_payload: bytes) -> List[Span]:
    """The image as edits of the base: the engine record appended to image A is an insertion
    (the SHARC image after it shifts, it is not copied); everything else is a replacement."""
    b, o = records.parse_images(base_payload), records.parse_images(image_payload)
    if len(b) != len(o) or len(o[0].records) != len(b[0].records) + 1 or o[0].records[-1].type != records.COPY:
        raise ManifestError("the image does not add exactly one record to image A")
    extra = o[0].records[-1]
    ins_off, ins_len = extra.offset, 16 + extra.w2
    inserted = image_payload[ins_off:ins_off + ins_len]
    without = image_payload[:ins_off] + image_payload[ins_off + ins_len:]
    if len(without) != len(base_payload):
        raise ManifestError("the image differs from the base by more than the added record")
    spans = diff_spans(base_payload, without)
    if any(off + len(data) > ins_off for off, data, _ in spans):
        raise ManifestError("the image changes bytes after the insertion point")
    return spans + [(ins_off, inserted, True)]


def read_version(root: Path = ROOT) -> str:
    """The release version: the VERSION file at the repository root (semantic versioning)."""
    return (root / "VERSION").read_text().strip()


def git_commit(cwd: Optional[Path] = None) -> str:
    try:
        r = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                           timeout=5, cwd=cwd or Path(__file__).resolve().parents[1])
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return "unknown"


def generate(base_file: bytes, image_file: bytes, commit: Optional[str] = None,
             built: Optional[str] = None, version: Optional[str] = None) -> dict:
    version = version or read_version()
    base_payload = syx.decode(base_file).payload
    image = syx.decode(image_file)
    if image.target != "main":
        raise ManifestError("the image is not a Main OS file")
    spans = image_spans(base_payload, image.payload)
    return {
        "format": FORMAT,
        "name": NAME,
        "version": version,
        "built": built or datetime.date.today().isoformat(),
        "commit": commit or git_commit(),
        "base": {"name": BASE_NAME, "size": len(base_file), "sha256": sha256(base_file)},
        "result": {"name": RESULT_PATTERN % version, "size": len(image_file), "sha256": sha256(image_file)},
        "spans": [dict({"offset": off, "data": base64.b64encode(data).decode("ascii")},
                       **({"insert": True} if insert else {})) for off, data, insert in spans],
    }


def to_js(m: dict) -> str:
    return PREFIX + json.dumps(m, indent=1) + ";\n"


def generate_js(base_file: bytes, image_file: bytes, commit: Optional[str] = None,
                built: Optional[str] = None, version: Optional[str] = None) -> str:
    return to_js(generate(base_file, image_file, commit, built, version))


def version_json(m: dict) -> dict:
    """What the page installs, for the README badge and other readers: site/version.json."""
    return {"version": m["version"], "built": m["built"], "commit": m["commit"],
            "result": {"name": m["result"]["name"], "sha256": m["result"]["sha256"]}}


def parse_js(text: str) -> dict:
    t = text.strip()
    if not t.startswith(PREFIX) or not t.endswith(";"):
        raise ManifestError("not a patch manifest (expected 'window.PATCH = {...};')")
    try:
        m = json.loads(t[len(PREFIX):-1])
    except ValueError as e:
        raise ManifestError("manifest JSON: %s" % e) from None
    if not isinstance(m, dict) or m.get("format") != FORMAT:
        raise ManifestError("unsupported manifest format %r" % (m.get("format") if isinstance(m, dict) else None))
    return m


def apply(m: dict, base_file: bytes) -> bytes:
    got = sha256(base_file)
    if got != m["base"]["sha256"]:
        raise ManifestError("this is not Sequential's Main OS 2.1.0 file (%s): its SHA-256 is %s, "
                            "expected %s" % (m["base"]["name"], got, m["base"]["sha256"]))
    base = syx.decode(base_file).payload
    chunks, cur = [], 0                                   # the base up to each span, then the span's bytes
    for s in m["spans"]:
        off, data, insert = int(s["offset"]), base64.b64decode(s["data"]), bool(s.get("insert"))
        nxt = off if insert else off + len(data)          # base position after the span
        if off < cur or nxt > len(base):
            raise ManifestError("span at offset %d does not fit the payload (%d bytes)" % (off, len(base)))
        chunks.append(base[cur:off])
        chunks.append(data)
        cur = nxt
    chunks.append(base[cur:])
    out = syx.encode(b"".join(chunks), "main")
    got = sha256(out)
    if got != m["result"]["sha256"]:
        raise ManifestError("the result does not match the manifest (SHA-256 %s, expected %s) -- do not "
                            "install it" % (got, m["result"]["sha256"]))
    return out
