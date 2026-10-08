"""Command-line interface: python3 -m tools {inspect,unpack,pack,diff,fwbuild,build,manifest,apply} ..."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import build, fw, manifest, records, syx


def cmd_inspect(args) -> int:
    raw = Path(args.file).read_bytes()
    c = syx.decode(raw, check_trailer=False)
    expected = syx.trailer_for(c.payload)
    status = "ok" if expected == c.trailer else "BAD expected %s (got %s)" % (
        expected.hex(" "), c.trailer.hex(" "))
    print("target: %s" % c.target)
    print("groups: %d" % c.groups)
    print("tail: %d" % c.tail)
    print("trailer: %s" % status)
    print("payload: %d bytes" % len(c.payload))
    try:
        images = records.parse_images(c.payload)
    except records.RecordError as e:
        print("records: ERROR %s" % e)
        return 0
    for i, im in enumerate(images):
        lo_hi = im.load_range()
        span = "0x%08X..0x%08X" % lo_hi if lo_hi else "-"
        print("image %d: family: %02X  entry: 0x%08X  declared: 0x%X  records: %d  load: %s"
              % (i, im.family, im.entry, im.declared_len, len(im.records), span))
    return 0


def cmd_unpack(args) -> int:
    raw = Path(args.file).read_bytes()
    c = syx.decode(raw)  # raises before anything is written
    out = Path(args.outdir)
    out.mkdir(parents=True, exist_ok=False)
    (out / "payload.bin").write_bytes(c.payload)
    (out / "header.json").write_text(json.dumps({
        "target": c.target, "groups": c.groups, "tail": c.tail, "trailer": list(c.trailer),
    }, indent=2) + "\n")
    print("wrote %s (%d bytes) and header.json" % (out / "payload.bin", len(c.payload)))
    return 0


def cmd_pack(args) -> int:
    payload = Path(args.payload).read_bytes()
    data = syx.encode(payload, args.target)
    Path(args.out).write_bytes(data)
    print("wrote %s (%d bytes)" % (args.out, len(data)))
    return 0


def _record(spec: str):
    base, size = spec.split(":")
    return int(base, 16), int(size, 16)


def cmd_build(args) -> int:
    rec_base, rec_size = _record(args.record)
    build.build_image(args.base, args.wrapper, args.map, args.hooks, args.out, rec_base, rec_size)
    base = syx.decode(Path(args.base).read_bytes()).payload
    out = syx.decode(Path(args.out).read_bytes()).payload
    spans = build.image_diff(base, out)
    print("wrote %s; %d changed span(s):" % (args.out, len(spans)))
    print(build.format_diff(spans), end="")
    return 0


def cmd_diff(args) -> int:
    a = syx.decode(Path(args.a).read_bytes(), check_trailer=False).payload
    b = syx.decode(Path(args.b).read_bytes(), check_trailer=False).payload
    print(build.format_diff(build.image_diff(a, b)), end="")
    return 0


def cmd_manifest(args) -> int:
    base, image = Path(args.base).read_bytes(), Path(args.image).read_bytes()
    text = manifest.generate_js(base, image, commit=args.commit, built=args.built)
    Path(args.out).write_text(text)
    m = manifest.parse_js(text)
    print("wrote %s: %d span(s), result %s %s" % (args.out, len(m["spans"]), m["result"]["name"],
                                                   m["result"]["sha256"]))
    return 0


def cmd_apply(args) -> int:
    m = manifest.parse_js(Path(args.manifest).read_text())
    out = manifest.apply(m, Path(args.base).read_bytes())    # raises before anything is written
    Path(args.out).write_bytes(out)
    print("wrote %s (%d bytes, SHA-256 %s)" % (args.out, len(out), m["result"]["sha256"]))
    return 0


def cmd_fwbuild(args) -> int:
    bin_path, map_path = fw.build_wrapper(args.src, args.out)
    print("wrote %s and %s" % (bin_path, map_path))
    print((Path(args.out) / (fw.NAME + ".layout")).read_text(), end="")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="python3 -m tools")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("fwbuild", help="cross-compile and link the engine (native.bin/.map)")
    p.add_argument("src")
    p.add_argument("out")
    p.set_defaults(func=cmd_fwbuild)

    p = sub.add_parser("build", help="place the engine and retarget hooks in a base OS .syx")
    p.add_argument("--base", required=True)
    p.add_argument("--wrapper", required=True)
    p.add_argument("--map", required=True)
    p.add_argument("--hooks", required=True)
    p.add_argument("-o", "--out", required=True)
    p.add_argument("--record", default="0x%X:0x%X" % (build.WRAPPER_REC_BASE, build.WRAPPER_REC_SIZE),
                   help="appended record BASE:SIZE (hex)")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("manifest", help="write the patch manifest (site/manifest.js) for an image")
    p.add_argument("--base", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("-o", "--out", required=True)
    p.add_argument("--commit", help="recorded commit (default: git HEAD)")
    p.add_argument("--built", help="recorded date, YYYY-MM-DD (default: today)")
    p.set_defaults(func=cmd_manifest)

    p = sub.add_parser("apply", help="apply a patch manifest to the stock OS file (the reference applier)")
    p.add_argument("manifest")
    p.add_argument("base")
    p.add_argument("out")
    p.set_defaults(func=cmd_apply)

    p = sub.add_parser("diff", help="list differing byte spans between two OS .syx files")
    p.add_argument("a")
    p.add_argument("b")
    p.set_defaults(func=cmd_diff)

    p = sub.add_parser("inspect", help="describe an OS .syx file")
    p.add_argument("file")
    p.set_defaults(func=cmd_inspect)

    p = sub.add_parser("unpack", help="decode an OS .syx into payload.bin + header.json")
    p.add_argument("file")
    p.add_argument("outdir")
    p.set_defaults(func=cmd_unpack)

    p = sub.add_parser("pack", help="encode a payload.bin into an OS .syx")
    p.add_argument("payload")
    p.add_argument("out")
    p.add_argument("--target", choices=syx.TARGETS, default="main")
    p.set_defaults(func=cmd_pack)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except FileNotFoundError as e:
        print("error: input file not found: %s -- see fixtures/README.md for the files this "
              "project needs and where to get them" % e.filename, file=sys.stderr)
        return 1
    except (syx.SyxError, records.RecordError, build.BuildError, fw.FirmwareBuildError,
            manifest.ManifestError, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
