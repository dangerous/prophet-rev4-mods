"""Record-stream model for the decoded OS payload (docs/SPEC.md, "Decoded payload").

    0D <type> <chk> <fam> | w1 (u32 LE) | w2 (u32 LE) | w3 (u32 LE)

EXEC (0x50) opens an image: w1 = entry, w3 = total length of what follows in the image.
COPY (0x00): w2 payload bytes follow, loaded at w1.  FILL (0x01): w2 bytes at w1 filled
with the u32 w3.  START (0x80): release the target core at w1; last record of its image.
`chk` makes the XOR of the 16 record bytes zero.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List

MAGIC = 0x0D
COPY = 0x00
FILL = 0x01
EXEC = 0x50
START = 0x80
RECORD_SIZE = 16


class RecordError(ValueError):
    """The payload is not a well-formed record stream."""


@dataclass(frozen=True)
class Record:
    type: int
    family: int
    w1: int
    w2: int
    w3: int
    payload: bytes = b""
    offset: int = -1

    @property
    def size(self) -> int:
        return RECORD_SIZE + len(self.payload)

    def to_bytes(self) -> bytes:
        return record_bytes(self.type, self.family, self.w1, self.w2, self.w3) + self.payload


@dataclass
class Image:
    family: int
    entry: int
    declared_len: int
    offset: int
    records: List[Record] = field(default_factory=list)

    @property
    def end(self) -> int:
        return self.offset + RECORD_SIZE + self.declared_len

    def load_range(self):
        """(lowest load address, highest load address + 1) over COPY/FILL records."""
        spans = [(r.w1, r.w1 + r.w2) for r in self.records if r.type in (COPY, FILL)]
        if not spans:
            return None
        return min(s[0] for s in spans), max(s[1] for s in spans)


def record_bytes(rtype: int, family: int, w1: int, w2: int, w3: int) -> bytes:
    body = bytes([MAGIC, rtype, 0, family]) + struct.pack("<III", w1, w2, w3)
    chk = 0
    for b in body:
        chk ^= b
    return body[:2] + bytes([chk]) + body[3:]


def _read_record(payload: bytes, offset: int) -> Record:
    raw = payload[offset:offset + RECORD_SIZE]
    if len(raw) < RECORD_SIZE:
        raise RecordError("truncated record at 0x%X" % offset)
    if raw[0] != MAGIC:
        raise RecordError("bad record magic 0x%02X at 0x%X" % (raw[0], offset))
    chk = 0
    for b in raw:
        chk ^= b
    if chk != 0:
        raise RecordError("record XOR check failed at 0x%X" % offset)
    w1, w2, w3 = struct.unpack_from("<III", raw, 4)
    return Record(type=raw[1], family=raw[3], w1=w1, w2=w2, w3=w3, offset=offset)


def parse_images(payload: bytes) -> List[Image]:
    images: List[Image] = []
    offset = 0
    while offset < len(payload):
        head = _read_record(payload, offset)
        if head.type != EXEC:
            raise RecordError("expected EXEC record at 0x%X, found type 0x%02X" % (offset, head.type))
        image = Image(family=head.family, entry=head.w1, declared_len=head.w3, offset=offset)
        image.records.append(head)
        if image.end > len(payload):
            raise RecordError("image at 0x%X declares length 0x%X beyond payload end"
                              % (offset, head.w3))
        pos = offset + RECORD_SIZE
        while pos < image.end:
            rec = _read_record(payload, pos)
            if rec.type == EXEC:
                raise RecordError("image at 0x%X: declared length 0x%X is wrong, a new image "
                                  "starts at 0x%X" % (offset, head.w3, pos))
            if rec.family != image.family:
                raise RecordError("record family 0x%02X at 0x%X differs from image family 0x%02X"
                                  % (rec.family, pos, image.family))
            if rec.type == COPY:
                data = payload[pos + RECORD_SIZE:pos + RECORD_SIZE + rec.w2]
                if len(data) != rec.w2 or pos + RECORD_SIZE + rec.w2 > image.end:
                    raise RecordError("COPY record at 0x%X runs past the image length" % pos)
                rec = Record(rec.type, rec.family, rec.w1, rec.w2, rec.w3, bytes(data), pos)
            elif rec.type == FILL:
                pass
            elif rec.type == START:
                if pos + RECORD_SIZE != image.end:
                    raise RecordError("START record at 0x%X is not the last record of its image" % pos)
            else:
                raise RecordError("unexpected record type 0x%02X at 0x%X inside image"
                                  % (rec.type, pos))
            image.records.append(rec)
            pos += rec.size
        if pos != image.end:
            raise RecordError("image at 0x%X: record walk ends at 0x%X but declared length ends at 0x%X"
                              % (offset, pos, image.end))
        images.append(image)
        offset = image.end
    return images


def serialize_images(images: List[Image]) -> bytes:
    out = bytearray()
    for image in images:
        for rec in image.records:
            out += rec.to_bytes()
    return bytes(out)
