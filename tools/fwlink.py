"""Minimal ELF32 (ARM, little-endian, REL) linker: places the allocatable code/rodata
sections of a single relocatable object at a fixed base address, applies the relocation
types clang emits for freestanding Thumb-2 code, and returns the flat binary plus a symbol
map. Refuses objects with initialised data or BSS (the wrapper keeps its state at a fixed
address instead)."""
from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

from . import thumb

SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS, SHT_REL = 1, 2, 3, 4, 8, 9
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 1, 2, 4
STT_OBJECT, STT_FUNC = 1, 2

R_ARM_NONE = 0
R_ARM_ABS32 = 2
R_ARM_THM_CALL = 10
R_ARM_THM_JUMP24 = 30
R_ARM_PREL31 = 42
R_ARM_THM_MOVW_ABS_NC = 47
R_ARM_THM_MOVT_ABS = 48

DISCARD_PREFIXES = (".ARM.exidx", ".ARM.extab")


class LinkError(Exception):
    pass


@dataclass
class Section:
    index: int
    name: str
    type: int
    flags: int
    offset: int
    size: int
    link: int
    info: int
    align: int
    data: bytes
    addr: Optional[int] = None   # assigned by layout


@dataclass
class Symbol:
    name: str
    value: int
    size: int
    type: int
    shndx: int


@dataclass
class Elf:
    sections: List[Section]
    symbols: List[Symbol]


def parse_elf(data: bytes) -> Elf:
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise LinkError("not a little-endian ELF32 object")
    e_machine = struct.unpack_from("<H", data, 18)[0]
    if e_machine != 40:
        raise LinkError("not an ARM object (e_machine=%d)" % e_machine)
    e_shoff, = struct.unpack_from("<I", data, 32)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)
    raw = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        raw.append(struct.unpack_from("<IIIIIIIIII", data, off))
    shstr = raw[e_shstrndx]
    strtab = data[shstr[4]:shstr[4] + shstr[5]]

    def name_at(table: bytes, idx: int) -> str:
        end = table.index(b"\0", idx)
        return table[idx:end].decode()

    sections = []
    for i, (sh_name, sh_type, sh_flags, _sh_addr, sh_offset, sh_size, sh_link, sh_info,
            sh_align, _entsize) in enumerate(raw):
        body = b"" if sh_type == SHT_NOBITS else data[sh_offset:sh_offset + sh_size]
        sections.append(Section(i, name_at(strtab, sh_name), sh_type, sh_flags, sh_offset,
                                sh_size, sh_link, sh_info, sh_align or 1, body))
    symbols: List[Symbol] = []
    symtab = next((s for s in sections if s.type == SHT_SYMTAB), None)
    if symtab is not None:
        names = sections[symtab.link].data
        for off in range(0, len(symtab.data), 16):
            st_name, st_value, st_size, st_info, _other, st_shndx = struct.unpack_from(
                "<IIIBBH", symtab.data, off)
            symbols.append(Symbol(name_at(names, st_name), st_value, st_size, st_info & 0xF,
                                  st_shndx))
    return Elf(sections, symbols)


def _align(x: int, a: int) -> int:
    return (x + a - 1) & ~(a - 1)


def link(obj: bytes, base: int, limit: Optional[int] = None) -> Tuple[bytes, Dict[str, int], dict]:
    elf = parse_elf(obj)
    placed: List[Section] = []
    for s in elf.sections:
        if not (s.flags & SHF_ALLOC) or s.name.startswith(DISCARD_PREFIXES):
            continue
        if s.type == SHT_NOBITS:
            if s.size:
                raise LinkError("object has BSS (%s, %d bytes); the wrapper must keep state at a fixed address"
                                % (s.name, s.size))
            continue
        if s.flags & SHF_WRITE:
            if s.size:
                raise LinkError("object has initialised data (%s, %d bytes); not supported"
                                % (s.name, s.size))
            continue
        placed.append(s)
    placed.sort(key=lambda s: (0 if s.flags & SHF_EXECINSTR else 1, s.index))

    cursor = base
    for s in placed:
        cursor = _align(cursor, max(s.align, 4))
        s.addr = cursor
        cursor += s.size
    end = cursor
    if limit is not None and end > limit:
        raise LinkError("linked image ends at 0x%08X, beyond the limit 0x%08X" % (end, limit))

    image = bytearray(end - base)
    for s in placed:
        image[s.addr - base:s.addr - base + s.size] = s.data

    def sym_addr(sym: Symbol) -> Tuple[int, int]:
        """(S, T): symbol address without Thumb bit, and the Thumb bit for functions."""
        if sym.shndx == 0 or sym.shndx >= len(elf.sections):
            raise LinkError("undefined symbol '%s'" % sym.name)
        sec = elf.sections[sym.shndx]
        if sec.addr is None:
            raise LinkError("symbol '%s' lives in a discarded section %s" % (sym.name, sec.name))
        thumb_bit = 1 if (sym.type == STT_FUNC or (sym.value & 1)) and (sec.flags & SHF_EXECINSTR) else 0
        return sec.addr + (sym.value & ~1), thumb_bit

    for rel in elf.sections:
        if rel.type not in (SHT_REL, SHT_RELA):
            continue
        target = elf.sections[rel.info]
        if target.addr is None:
            continue  # relocations for discarded sections (e.g. .ARM.exidx PREL31)
        if rel.type == SHT_RELA:
            raise LinkError("RELA relocations are not supported")
        for off in range(0, len(rel.data), 8):
            r_offset, r_info = struct.unpack_from("<II", rel.data, off)
            r_type, r_sym = r_info & 0xFF, r_info >> 8
            if r_type == R_ARM_NONE:
                continue
            place = target.addr + r_offset
            at = place - base
            s_addr, t_bit = sym_addr(elf.symbols[r_sym])
            if r_type == R_ARM_ABS32:
                addend = struct.unpack_from("<I", image, at)[0]
                struct.pack_into("<I", image, at, ((s_addr + addend) | t_bit) & 0xFFFFFFFF)
            elif r_type in (R_ARM_THM_MOVW_ABS_NC, R_ARM_THM_MOVT_ABS):
                hw1, hw2 = struct.unpack_from("<HH", image, at)
                addend = thumb.movw_imm16(hw1, hw2)
                if addend & 0x8000:
                    addend -= 0x10000
                if r_type == R_ARM_THM_MOVW_ABS_NC:
                    value = ((s_addr + addend) | t_bit) & 0xFFFF
                else:
                    value = ((s_addr + (addend << 16)) >> 16) & 0xFFFF
                struct.pack_into("<HH", image, at, *thumb.set_movw_imm16(hw1, hw2, value))
            elif r_type in (R_ARM_THM_CALL, R_ARM_THM_JUMP24):
                insn = bytes(image[at:at + 4])
                decoded = thumb.decode_branch(place, insn)
                if decoded is None:
                    raise LinkError("relocation at 0x%08X is not on a BL/B.W" % place)
                cur_target, is_bl = decoded
                addend = cur_target - (place + 4)
                new_target = (s_addr | t_bit) + addend + 4
                image[at:at + 4] = thumb.encode_branch(place, new_target, is_bl)
            else:
                raise LinkError("unsupported relocation type %d at 0x%08X" % (r_type, place))

    symbols: Dict[str, int] = {}
    for sym in elf.symbols:
        if not sym.name or sym.name.startswith("$") or sym.shndx == 0 or sym.shndx >= len(elf.sections):
            continue
        sec = elf.sections[sym.shndx]
        if sec.addr is None:
            continue
        addr, t_bit = sym_addr(sym)
        symbols[sym.name] = addr | t_bit
    info = {"base": base, "end": end,
            "sections": [(s.name, s.addr, s.size) for s in placed]}
    return bytes(image), symbols, info
