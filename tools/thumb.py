"""Thumb-2 instruction helpers: BL/B.W encoding, MOVW/MOVT immediates, and a scan for the
absolute addresses a piece of code references."""
from __future__ import annotations

import struct
from typing import Optional, Set, Tuple


class RangeError(ValueError):
    """Branch target is out of the ±16 MB range or misaligned."""


# --- BL / B.W (T1 / T4 encodings) -----------------------------------------------------

def encode_branch(addr: int, target: int, is_bl: bool = True) -> bytes:
    target &= ~1
    off = target - ((addr & ~1) + 4)
    if off & 1 or not -(1 << 24) <= off < (1 << 24):
        raise RangeError("branch from 0x%08X to 0x%08X out of range" % (addr, target))
    off &= (1 << 25) - 1
    s = (off >> 24) & 1
    i1 = (off >> 23) & 1
    i2 = (off >> 22) & 1
    imm10 = (off >> 12) & 0x3FF
    imm11 = (off >> 1) & 0x7FF
    j1 = (1 - i1) ^ s
    j2 = (1 - i2) ^ s
    hw1 = 0xF000 | (s << 10) | imm10
    hw2 = (0xD000 if is_bl else 0x9000) | (j1 << 13) | (j2 << 11) | imm11
    return struct.pack("<HH", hw1, hw2)


def encode_bl(addr: int, target: int) -> bytes:
    return encode_branch(addr, target, True)


def decode_branch(addr: int, data: bytes) -> Optional[Tuple[int, bool]]:
    """(target, is_bl) for a BL or B.W at addr, else None."""
    if len(data) < 4:
        return None
    hw1, hw2 = struct.unpack_from("<HH", data, 0)
    if (hw1 & 0xF800) != 0xF000:
        return None
    if (hw2 & 0xD000) == 0xD000:
        is_bl = True
    elif (hw2 & 0xD000) == 0x9000:
        is_bl = False
    else:
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
    return ((addr & ~1) + 4 + off) & 0xFFFFFFFF, is_bl


def decode_bl(addr: int, data: bytes) -> Optional[int]:
    d = decode_branch(addr, data)
    return d[0] if d and d[1] else None


# --- MOVW / MOVT -------------------------------------------------------------------------

def is_movw(hw1: int) -> bool:
    return (hw1 & 0xFBF0) == 0xF240


def is_movt(hw1: int) -> bool:
    return (hw1 & 0xFBF0) == 0xF2C0


def movw_imm16(hw1: int, hw2: int) -> int:
    imm4 = hw1 & 0xF
    i = (hw1 >> 10) & 1
    imm3 = (hw2 >> 12) & 7
    imm8 = hw2 & 0xFF
    return (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8


def set_movw_imm16(hw1: int, hw2: int, value: int) -> Tuple[int, int]:
    value &= 0xFFFF
    hw1 = (hw1 & ~0x040F) | (((value >> 11) & 1) << 10) | ((value >> 12) & 0xF)
    hw2 = (hw2 & ~0x70FF) | (((value >> 8) & 7) << 12) | (value & 0xFF)
    return hw1, hw2


def _is_32bit(hw1: int) -> bool:
    return (hw1 & 0xE000) == 0xE000 and (hw1 & 0x1800) != 0


def find_absolute_addresses(code: bytes, base: int) -> Set[int]:
    """Absolute 32-bit values the code materialises via MOVW/MOVT pairs or loads from its
    literal pool (LDR Rt, [PC, #imm]). Best-effort linear sweep; data words inside the
    code are not followed."""
    found: Set[int] = set()
    pending = {}  # register -> low half from a MOVW
    pos = 0
    n = len(code) & ~1
    while pos + 2 <= n:
        hw1 = struct.unpack_from("<H", code, pos)[0]
        pc = base + pos
        if _is_32bit(hw1) and pos + 4 <= n:
            hw2 = struct.unpack_from("<H", code, pos + 2)[0]
            rd = (hw2 >> 8) & 0xF
            if is_movw(hw1):
                pending[rd] = movw_imm16(hw1, hw2)
            elif is_movt(hw1):
                if rd in pending:
                    found.add((movw_imm16(hw1, hw2) << 16) | pending.pop(rd))
            elif (hw1 & 0xFF7F) == 0xF85F:            # LDR.W Rt, [PC, #±imm12]
                imm = hw2 & 0xFFF
                lit = ((pc + 4) & ~3) + (imm if hw1 & 0x80 else -imm)
                if 0 <= lit - base <= len(code) - 4:
                    found.add(struct.unpack_from("<I", code, lit - base)[0])
            else:
                pending.pop(rd, None)
            pos += 4
            continue
        if (hw1 & 0xF800) == 0x4800:                   # LDR Rt, [PC, #imm8*4]
            lit = ((pc + 4) & ~3) + (hw1 & 0xFF) * 4
            if 0 <= lit - base <= len(code) - 4:
                found.add(struct.unpack_from("<I", code, lit - base)[0])
        pos += 2
    return found
