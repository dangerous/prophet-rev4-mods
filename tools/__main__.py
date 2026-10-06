"""Command-line interface: python3 -m tools {inspect,unpack,pack} ..."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import records, syx


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


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="python3 -m tools")
    sub = parser.add_subparsers(dest="command", required=True)

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
    except (syx.SyxError, records.RecordError, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
