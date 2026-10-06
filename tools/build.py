"""Image patching: place the wrapper in V5's RAM-window record and retarget hook sites
(docs/SPEC.md: "Image patching" and "Safety invariants")."""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List

from . import records, syx, thumb

STOCK_CODE_BASE = 0x2002EF00      # the big stock code COPY record
V5_WINDOW_BASE = 0x20088000       # V5's arp blob COPY record
V5_WINDOW_SIZE = 0x2000
WRAPPER_LO = 0x20089600           # wrapper window inside the V5 record
WRAPPER_HI = 0x2008A000
DIFF_MERGE_GAP = 8                # bytes of unchanged data that still join two spans


class BuildError(Exception):
    """The build inputs violate the patching rules; nothing is written."""


@dataclass(frozen=True)
class DiffSpan:
    image: int
    record_offset: int
    ram_lo: int
    ram_hi: int

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


def build_payload(base_payload: bytes, wrapper: bytes, symbols: Dict[str, int],
                  hooks: List[dict]) -> bytes:
    images = records.parse_images(base_payload)
    image_a = images[0]
    out = bytearray(base_payload)

    # --- wrapper placement
    window = next((r for r in image_a.records
                   if r.type == records.COPY and r.w1 == V5_WINDOW_BASE and r.w2 == V5_WINDOW_SIZE),
                  None)
    if window is None:
        raise BuildError("base has no V5 RAM-window record (COPY 0x%X bytes at 0x%08X)"
                         % (V5_WINDOW_SIZE, V5_WINDOW_BASE))
    if len(wrapper) > WRAPPER_HI - WRAPPER_LO:
        raise BuildError("wrapper binary (%d bytes) exceeds the wrapper window (0x%X bytes)"
                         % (len(wrapper), WRAPPER_HI - WRAPPER_LO))
    win_off = window.offset + records.RECORD_SIZE + (WRAPPER_LO - window.w1)
    win_len = WRAPPER_HI - WRAPPER_LO
    dirty = next((i for i, b in enumerate(base_payload[win_off:win_off + win_len]) if b), None)
    if dirty is not None:
        raise BuildError("wrapper window is not free in base: non-zero byte at 0x%08X"
                         % (WRAPPER_LO + dirty))
    out[win_off:win_off + len(wrapper)] = wrapper

    # --- hook retargeting
    code = next((r for r in image_a.records if r.type == records.COPY and r.w1 == STOCK_CODE_BASE),
                None)
    if code is None:
        raise BuildError("base has no stock code record at 0x%08X" % STOCK_CODE_BASE)
    for hook in hooks:
        site = int(str(hook["site"]), 16)
        expect = int(str(hook["expect"]), 16)
        symbol = hook["symbol"]
        if symbol not in symbols:
            raise BuildError("hook symbol '%s' is not in the wrapper map" % symbol)
        if not code.w1 <= site <= code.w1 + code.w2 - 4:
            raise BuildError("hook site 0x%08X is not inside the stock code record" % site)
        off = code.offset + records.RECORD_SIZE + (site - code.w1)
        current = thumb.decode_bl(site, base_payload[off:off + 4])
        if current is None or current != (expect & ~1):
            found = "BL to 0x%08X" % current if current is not None else \
                "bytes %s" % base_payload[off:off + 4].hex(" ")
            raise BuildError("hook site 0x%08X: expected BL to 0x%08X, found %s"
                             % (site, expect, found))
        try:
            out[off:off + 4] = thumb.encode_bl(site, symbols[symbol])
        except thumb.RangeError as e:
            raise BuildError(str(e))
    return bytes(out)


def build_image(base: Path, wrapper: Path, map_path: Path, hooks_path: Path, out: Path) -> None:
    base_raw = Path(base).read_bytes()
    container = syx.decode(base_raw)
    if container.target != "main":
        raise BuildError("base is not a Main OS file")
    payload = build_payload(container.payload, Path(wrapper).read_bytes(),
                            parse_map(Path(map_path).read_text()),
                            json.loads(Path(hooks_path).read_text()))
    Path(out).write_bytes(syx.encode(payload, "main"))


def image_diff(base_payload: bytes, out_payload: bytes) -> List[DiffSpan]:
    bi, oi = records.parse_images(base_payload), records.parse_images(out_payload)
    if len(bi) != len(oi):
        raise BuildError("record structure differs: %d vs %d images" % (len(bi), len(oi)))
    spans: List[DiffSpan] = []
    for idx, (b, o) in enumerate(zip(bi, oi)):
        if [(r.type, r.w1, r.w2, r.w3) for r in b.records] != [(r.type, r.w1, r.w2, r.w3) for r in o.records]:
            raise BuildError("record structure differs in image %d" % idx)
        for rb, ro in zip(b.records, o.records):
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
    return spans


def format_diff(spans: List[DiffSpan]) -> str:
    return "".join("image %d record @0x%08X ram 0x%08X..0x%08X: %d bytes\n"
                   % (s.image, s.record_offset, s.ram_lo, s.ram_hi, s.size) for s in spans)
