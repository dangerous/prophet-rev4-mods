"""Thumb-2 `BL` encoding/decoding (ARMv7 T1 encoding)."""
from __future__ import annotations

import struct
from typing import Optional


class RangeError(ValueError):
    """Branch target is out of the ±16 MB BL range or misaligned."""


def encode_bl(addr: int, target: int) -> bytes:
    """Encode `BL target` located at `addr` (both byte addresses; Thumb bit ignored)."""
    target &= ~1
    off = target - ((addr & ~1) + 4)
    if off & 1 or not -(1 << 24) <= off < (1 << 24):
        raise RangeError("BL from 0x%08X to 0x%08X out of range" % (addr, target))
    off &= (1 << 25) - 1
    s = (off >> 24) & 1
    i1 = (off >> 23) & 1
    i2 = (off >> 22) & 1
    imm10 = (off >> 12) & 0x3FF
    imm11 = (off >> 1) & 0x7FF
    j1 = (1 - i1) ^ s
    j2 = (1 - i2) ^ s
    hw1 = 0xF000 | (s << 10) | imm10
    hw2 = 0xD000 | (j1 << 13) | (j2 << 11) | imm11
    return struct.pack("<HH", hw1, hw2)


def decode_bl(addr: int, data: bytes) -> Optional[int]:
    """Return the (even) target of the `BL` encoded in `data` at `addr`, or None if `data`
    is not a BL instruction."""
    if len(data) < 4:
        return None
    hw1, hw2 = struct.unpack_from("<HH", data, 0)
    if (hw1 & 0xF800) != 0xF000 or (hw2 & 0xD000) != 0xD000:
        return None
    s = (hw1 >> 10) & 1
    imm10 = hw1 & 0x3FF
    j1 = (hw2 >> 13) & 1
    j2 = (hw2 >> 11) & 1
    imm11 = hw2 & 0x7FF
    i1 = 1 - (j1 ^ s)
    i2 = 1 - (j2 ^ s)
    off = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
    if s:
        off -= 1 << 25
    return ((addr & ~1) + 4 + off) & 0xFFFFFFFF
