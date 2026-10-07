"""Image patching: append the engine record to image A and retarget hook sites
(docs/SPEC.md: "Image patching" and "Safety invariants")."""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List

from . import records, syx, thumb

STOCK_CODE_BASE = 0x2002EF00      # the big stock code COPY record
WRAPPER_REC_BASE = 0x20088000     # the appended engine COPY record
WRAPPER_REC_SIZE = 0x8000
DIFF_MERGE_GAP = 8                # bytes of unchanged data that still join two spans


class BuildError(Exception):
    """The build inputs violate the patching rules; nothing is written."""


@dataclass(frozen=True)
class DiffSpan:
    image: int
    record_offset: int
    ram_lo: int
    ram_hi: int
    appended: bool = False

    @property
    def size(self) -> int:
        return self.ram_hi - self.ram_lo


def _copy_record_for(images: List[records.Image], ram: int, image_index: int = 0):
    for rec in images[image_index].records:
        if rec.type == records.COPY and rec.w1 <= ram < rec.w1 + rec.w2:
            return rec
    raise BuildError("RAM address 0x%08X is not covered by any COPY record of image %d"
                     % (ram, image_index))


def ram_to_payload_offset(payload: bytes, ram: int) -> int:
    rec = _copy_record_for(records.parse_images(payload), ram)
    return rec.offset + records.RECORD_SIZE + (ram - rec.w1)


def read_ram(payload: bytes, ram: int, n: int) -> bytes:
    off = ram_to_payload_offset(payload, ram)
    return payload[off:off + n]


def parse_map(text: str) -> Dict[str, int]:
    symbols: Dict[str, int] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            symbols[parts[-1]] = int(parts[0], 16)
    return symbols


def _patch_code_record(code: records.Record, symbols: Dict[str, int], hooks: List[dict]) -> bytes:
    data = bytearray(code.payload)
    for hook in hooks:
        kind = hook.get("kind", "bl")
        site = int(str(hook["site"]), 16)
        expect = int(str(hook["expect"]), 16)
        symbol = hook["symbol"]
        if symbol not in symbols:
            raise BuildError("hook symbol '%s' is not in the wrapper map" % symbol)
        if not code.w1 <= site <= code.w1 + code.w2 - 4:
            raise BuildError("hook site 0x%08X is not inside the stock code record" % site)
        off = site - code.w1
        current = bytes(code.payload[off:off + 4])
        if kind == "bl":
            target = thumb.decode_bl(site, current)
            if target is None or target != (expect & ~1):
                found = "BL to 0x%08X" % target if target is not None else "bytes %s" % current.hex(" ")
                raise BuildError("hook site 0x%08X: expected BL to 0x%08X, found %s" % (site, expect, found))
            try:
                data[off:off + 4] = thumb.encode_bl(site, symbols[symbol])
            except thumb.RangeError as e:
                raise BuildError(str(e))
        elif kind == "word":
            word = struct.unpack("<I", current)[0]
            if word != expect:
                raise BuildError("hook site 0x%08X: expected word 0x%08X, found 0x%08X" % (site, expect, word))
            data[off:off + 4] = struct.pack("<I", symbols[symbol])
        else:
            raise BuildError("hook site 0x%08X: unknown kind '%s'" % (site, kind))
    return bytes(data)


def build_payload(base_payload: bytes, wrapper: bytes, symbols: Dict[str, int],
                  hooks: List[dict], rec_base: int = WRAPPER_REC_BASE,
                  rec_size: int = WRAPPER_REC_SIZE) -> bytes:
    """Patch the hook sites and append the engine as one COPY record at rec_base/rec_size
    (0x20088000/0x8000 by default)."""
    images = records.parse_images(base_payload)
    image_a = images[0]

    if len(wrapper) > rec_size:
        raise BuildError("wrapper binary (%d bytes) exceeds the wrapper record (0x%X bytes)"
                         % (len(wrapper), rec_size))
    for rec in image_a.records:
        if rec.type in (records.COPY, records.FILL) and rec.w1 < rec_base + rec_size \
                and rec.w1 + rec.w2 > rec_base:
            raise BuildError("base already has a record covering 0x%08X (at 0x%08X, %d bytes)"
                             % (rec_base, rec.w1, rec.w2))
    code = next((r for r in image_a.records if r.type == records.COPY and r.w1 == STOCK_CODE_BASE), None)
    if code is None:
        raise BuildError("base has no stock code record at 0x%08X" % STOCK_CODE_BASE)

    patched_code = _patch_code_record(code, symbols, hooks)
    head = image_a.records[0]
    new_records = [records.Record(head.type, head.family, head.w1, head.w2,
                                  head.w3 + records.RECORD_SIZE + rec_size, b"", head.offset)]
    for rec in image_a.records[1:]:
        if rec is code:
            rec = records.Record(rec.type, rec.family, rec.w1, rec.w2, rec.w3, patched_code, rec.offset)
        new_records.append(rec)
    new_records.append(records.Record(records.COPY, image_a.family, rec_base, rec_size, 0,
                                      wrapper + bytes(rec_size - len(wrapper))))
    new_a = records.Image(image_a.family, image_a.entry, new_records[0].w3, image_a.offset, new_records)
    return records.serialize_images([new_a] + images[1:])


def build_image(base: Path, wrapper: Path, map_path: Path, hooks_path: Path, out: Path,
                rec_base: int = WRAPPER_REC_BASE, rec_size: int = WRAPPER_REC_SIZE) -> None:
    base_raw = Path(base).read_bytes()
    container = syx.decode(base_raw)
    if container.target != "main":
        raise BuildError("base is not a Main OS file")
    payload = build_payload(container.payload, Path(wrapper).read_bytes(),
                            parse_map(Path(map_path).read_text()),
                            json.loads(Path(hooks_path).read_text()), rec_base, rec_size)
    Path(out).write_bytes(syx.encode(payload, "main"))


def image_diff(base_payload: bytes, out_payload: bytes) -> List[DiffSpan]:
    """Byte spans by which `out` differs from `base`. The output's image A may carry one
    extra trailing COPY record (the wrapper); it is reported as an `appended` span."""
    bi, oi = records.parse_images(base_payload), records.parse_images(out_payload)
    if len(bi) != len(oi):
        raise BuildError("record structure differs: %d vs %d images" % (len(bi), len(oi)))
    spans: List[DiffSpan] = []
    for idx, (b, o) in enumerate(zip(bi, oi)):
        o_records = o.records
        extra = None
        if len(o_records) == len(b.records) + 1 and o_records[-1].type == records.COPY:
            extra = o_records[-1]
            o_records = o_records[:-1]
        shape = lambda recs: [(r.type, r.w1, r.w2, r.w3) for r in recs[1:]]  # EXEC length may differ
        if shape(b.records) != shape(o_records) or (b.records[0].type, b.records[0].w1) != (o_records[0].type, o_records[0].w1):
            raise BuildError("record structure differs in image %d" % idx)
        for rb, ro in zip(b.records, o_records):
            if rb.type != records.COPY or rb.payload == ro.payload:
                continue
            cur = None
            for hw in range(0, len(rb.payload), 2):
                if rb.payload[hw:hw + 2] != ro.payload[hw:hw + 2]:
                    if cur is not None and hw - cur[1] <= DIFF_MERGE_GAP:
                        cur[1] = hw + 2
                    else:
                        if cur is not None:
                            spans.append(DiffSpan(idx, rb.offset, rb.w1 + cur[0], rb.w1 + cur[1]))
                        cur = [hw, hw + 2]
            if cur is not None:
                spans.append(DiffSpan(idx, rb.offset, rb.w1 + cur[0], rb.w1 + cur[1]))
        if extra is not None:
            spans.append(DiffSpan(idx, extra.offset, extra.w1, extra.w1 + extra.w2, appended=True))
    return spans


def format_diff(spans: List[DiffSpan]) -> str:
    out = []
    for s in spans:
        if s.appended:
            out.append("image %d appended record ram 0x%08X..0x%08X: %d bytes\n"
                       % (s.image, s.ram_lo, s.ram_hi, s.size))
        else:
            out.append("image %d record @0x%08X ram 0x%08X..0x%08X: %d bytes\n"
                       % (s.image, s.record_offset, s.ram_lo, s.ram_hi, s.size))
    return "".join(out)
