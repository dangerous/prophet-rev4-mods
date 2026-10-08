"""Firmware build driver: cross-compile firmware/native.c with clang for thumbv7a
(Cortex-A5) and link it at the engine record base (docs/SPEC.md: "Engine record")."""
from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path
from typing import Dict, Tuple

from . import fwlink

ENGINE_BASE = 0x20088000         # the appended engine record (tools/build.py)
CODE_LIMIT = 0x2008C000          # code+rodata must end at or below this; state above (16 KB each)
SOURCE = "native.c"
NAME = "native"
REQUIRED_SYMBOLS = ("hook_kbd_scan", "hook_local_note", "hook_midi_note_on", "hook_midi_note_off",
                    "hook_cc123", "hook_hold", "hook_button", "hook_pot_store", "hook_pot_change",
                    "hook_hold_query", "hook_local_midi_out_on", "hook_local_midi_out_off",
                    "hook_rt_trampoline", "hook_program_loaded", "stock_iface")

CFLAGS = [
    "-target", "thumbv7a-none-eabi", "-mcpu=cortex-a5", "-mthumb", "-mfloat-abi=soft",
    "-Oz", "-ffreestanding", "-fno-builtin", "-nostdlib", "-fno-exceptions",
    "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fno-stack-protector",
    "-fomit-frame-pointer", "-fno-jump-tables",   # text = instructions + literal pools only (the image
    "-std=c11", "-Wall", "-Wextra", "-Werror",     # test sweeps it for addresses; a tbb table fooled it)
]


class FirmwareBuildError(Exception):
    pass


def _clang() -> str:
    for candidate in ("clang", "/usr/bin/clang"):
        path = shutil.which(candidate)
        if path:
            return path
    raise FirmwareBuildError("clang not found")


def compile_object(src_dir: Path, out_dir: Path) -> Path:
    src_dir, out_dir = Path(src_dir), Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    obj = out_dir / (NAME + ".o")
    cmd = [_clang(), *CFLAGS, "-I", str(src_dir), "-c", str(src_dir / SOURCE), "-o", str(obj)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise FirmwareBuildError("clang failed:\n%s" % r.stderr)
    return obj


def build_wrapper(src_dir: Path, out_dir: Path) -> Tuple[Path, Path]:
    """Compile and link; write native.bin, native.map and native.layout; return bin, map."""
    out_dir = Path(out_dir)
    obj = compile_object(src_dir, out_dir)
    try:
        image, symbols, info = fwlink.link(obj.read_bytes(), ENGINE_BASE, limit=CODE_LIMIT)
    except fwlink.LinkError as e:
        raise FirmwareBuildError(str(e))
    missing = [s for s in REQUIRED_SYMBOLS if s not in symbols]
    if missing:
        raise FirmwareBuildError("engine is missing required symbols: %s" % ", ".join(missing))
    bin_path, map_path = out_dir / (NAME + ".bin"), out_dir / (NAME + ".map")
    bin_path.write_bytes(image)
    map_path.write_text("".join("%08x %s\n" % (a, n) for n, a in sorted(symbols.items(), key=lambda kv: kv[1])))
    (out_dir / (NAME + ".layout")).write_text(
        "base 0x%08X end 0x%08X (%d bytes, limit 0x%08X)\n" % (info["base"], info["end"], info["end"] - info["base"], CODE_LIMIT)
        + "".join("  %-12s 0x%08X %d\n" % s for s in info["sections"]))
    return bin_path, map_path


def inspect_object(obj_path: Path) -> Dict[str, object]:
    """Section sizes and the set of mnemonics used (via objdump), for structural tests."""
    elf = fwlink.parse_elf(Path(obj_path).read_bytes())
    data_size = sum(s.size for s in elf.sections
                    if s.type == fwlink.SHT_PROGBITS and (s.flags & fwlink.SHF_ALLOC) and (s.flags & fwlink.SHF_WRITE))
    bss_size = sum(s.size for s in elf.sections if s.type == fwlink.SHT_NOBITS and (s.flags & fwlink.SHF_ALLOC))
    mnemonics = set()
    objdump = shutil.which("objdump") or "/usr/bin/objdump"
    r = subprocess.run([objdump, "-d", "--triple=thumbv7a-none-eabi", str(obj_path)],
                       capture_output=True, text=True)
    if r.returncode == 0:
        for line in r.stdout.splitlines():
            m = re.match(r"\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} ?)+\s*\t?([a-z][a-z0-9.]*)", line)
            if m:
                mnemonics.add(m.group(1).split(".")[0])
    return {"data_size": data_size, "bss_size": bss_size, "mnemonics": mnemonics}
