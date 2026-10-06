"""SysEx container codec for Prophet-5/10 Rev4 OS updates.

Format (docs/SPEC.md, "SysEx container"):

    F0 01 32 7C [7D] 7A <6-byte header group> <payload> <trailer: 2 bytes> F7

Packed groups: one MS byte followed by up to 7 data bytes; data byte k takes bit 7 from
bit k of the MS byte. The header group decodes to a big-endian u32 count of full payload
groups and a u8 count of data bytes in the final partial group. The trailer is the low 16
bits of the sum of little-endian u16 halfwords of the decoded payload, each byte masked
to 7 bits.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

PREFIX = bytes([0xF0, 0x01, 0x32])
CMD_OS_UPDATE = 0x7C
PANEL_SELECTOR = 0x7D
HEADER_MAGIC = 0x7A
EOX = 0xF7

TARGETS = ("main", "panel")


class SyxError(ValueError):
    """The input is not a well-formed OS update message."""


@dataclass(frozen=True)
class Container:
    target: str
    groups: int
    tail: int
    trailer: bytes
    payload: bytes


def pack7(data: bytes) -> bytes:
    """Encode 8-bit data as 7-bit packed groups (full groups, then one partial group)."""
    out = bytearray()
    for i in range(0, len(data), 7):
        chunk = data[i:i + 7]
        ms = 0
        for k, b in enumerate(chunk):
            if b & 0x80:
                ms |= 1 << k
        out.append(ms)
        out.extend(b & 0x7F for b in chunk)
    return bytes(out)


def unpack7(packed: bytes) -> bytes:
    """Decode 7-bit packed groups back to 8-bit data."""
    out = bytearray()
    for i in range(0, len(packed), 8):
        group = packed[i:i + 8]
        if len(group) < 2:
            raise SyxError("packed data ends with an MS byte and no data bytes")
        ms = group[0]
        for k, b in enumerate(group[1:]):
            out.append(b | (((ms >> k) & 1) << 7))
    return bytes(out)


def trailer_for(payload: bytes) -> bytes:
    even = len(payload) & ~1
    total = sum(struct.unpack_from("<%dH" % (even // 2), payload, 0)) if even else 0
    return bytes([total & 0x7F, (total >> 8) & 0x7F])


def _header_group(groups: int, tail: int) -> bytes:
    return pack7(struct.pack(">IB", groups, tail))


def decode(raw: bytes, check_trailer: bool = True) -> Container:
    if len(raw) < 4 + 7 + 2 + 1 or raw[:3] != PREFIX or raw[-1] != EOX:
        raise SyxError("bad SysEx prefix/terminator (expected F0 01 32 ... F7)")
    if raw[3] != CMD_OS_UPDATE:
        raise SyxError("unsupported command 0x%02X (expected 7C = OS update)" % raw[3])
    pos = 4
    target = "main"
    if raw[pos] == PANEL_SELECTOR:
        target = "panel"
        pos += 1
    if raw[pos] != HEADER_MAGIC:
        raise SyxError("missing 7A header byte at offset %d (found 0x%02X)" % (pos, raw[pos]))
    pos += 1
    header = raw[pos:pos + 6]
    if len(header) != 6:
        raise SyxError("truncated header group")
    groups, tail = struct.unpack(">IB", unpack7(header))
    if tail > 6:
        raise SyxError("header tail count %d out of range (0-6)" % tail)
    pos += 6
    body = raw[pos:-1]
    if len(body) < 2:
        raise SyxError("missing trailer")
    packed, trailer = body[:-2], body[-2:]
    expected_packed = groups * 8 + (tail + 1 if tail else 0)
    if len(packed) != expected_packed:
        raise SyxError("payload length %d inconsistent with header (groups=%d, tail=%d -> %d)"
                       % (len(packed), groups, tail, expected_packed))
    bad = next((i for i, b in enumerate(body) if b & 0x80), None)
    if bad is not None:
        raise SyxError("non-7-bit byte 0x%02X inside message at offset %d" % (body[bad], pos + bad))
    payload = unpack7(packed)
    if check_trailer and trailer_for(payload) != trailer:
        raise SyxError("trailer mismatch: expected %s, got %s"
                       % (trailer_for(payload).hex(" "), trailer.hex(" ")))
    return Container(target=target, groups=groups, tail=tail, trailer=bytes(trailer),
                     payload=payload)


def encode(payload: bytes, target: str) -> bytes:
    if target not in TARGETS:
        raise ValueError("target must be one of %s" % (TARGETS,))
    groups, tail = divmod(len(payload), 7)
    out = bytearray(PREFIX)
    out.append(CMD_OS_UPDATE)
    if target == "panel":
        out.append(PANEL_SELECTOR)
    out.append(HEADER_MAGIC)
    out += _header_group(groups, tail)
    out += pack7(payload)
    out += trailer_for(payload)
    out.append(EOX)
    return bytes(out)
