#!/usr/bin/env python3
"""Static checks for the XP x86 cross build; this is not a driver boot test."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

import pefile


def check(path, prefix, xp_system32=None, scsiport=None):
    pe = pefile.PE(str(path))
    opt = pe.OPTIONAL_HEADER
    assert pe.FILE_HEADER.Machine == 0x14C, "not x86"
    assert opt.Magic == 0x10B and opt.Subsystem == 1, "not a native PE32 image"
    assert not pe.FILE_HEADER.Characteristics & 0x2000, "DLL flag set"
    assert (opt.MajorSubsystemVersion, opt.MinorSubsystemVersion) == (5, 1)
    assert (opt.MajorOperatingSystemVersion, opt.MinorOperatingSystemVersion) == (5, 1)
    assert opt.CheckSum == pe.generate_checksum(), "invalid PE checksum"
    assert pe.get_section_by_rva(opt.AddressOfEntryPoint).Characteristics & 0x20000000
    assert opt.DATA_DIRECTORY[5].Size, "missing base relocations"
    assert opt.DATA_DIRECTORY[9].Size == 0, "unexpected TLS runtime"
    assert {11, 16} <= {x.id for x in pe.DIRECTORY_ENTRY_RESOURCE.entries}, "missing message/version resources"

    symbols = subprocess.check_output([prefix + "nm", "-g", str(path)], text=True)
    entry = re.search(r"^([0-9a-fA-F]+) T _DriverEntry@8$", symbols, re.M)
    assert entry and int(entry[1], 16) == opt.ImageBase + opt.AddressOfEntryPoint, "DriverEntry ABI/entry mismatch"

    imports = {}
    references = {}
    for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
        name = descriptor.dll.decode().lower()
        assert name in {"ntoskrnl.exe", "hal.dll", "scsiport.sys"}, f"unexpected import DLL: {name}"
        assert all(x.name for x in descriptor.imports), "unexpected ordinal import"
        imports[name] = sorted(x.name.decode() for x in descriptor.imports)
        if xp_system32:
            ref = (Path(scsiport) if scsiport else xp_system32 / "drivers/scsiport.sys") if name == "scsiport.sys" else xp_system32 / name
            exported = pefile.PE(str(ref))
            names = {x.name.decode() for x in exported.DIRECTORY_ENTRY_EXPORT.symbols if x.name}
            missing = set(imports[name]) - names
            assert not missing, f"not exported by XP {name}: {sorted(missing)}"
            references[name] = {"path": str(ref.resolve()), "sha256": hashlib.sha256(ref.read_bytes()).hexdigest()}

    disassembly = subprocess.check_output([prefix + "objdump", "-d", "-Mintel", "--no-show-raw-insn", str(path)], text=True)
    forbidden = []
    for line in disassembly.splitlines():
        instruction = re.match(r"\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\b(.*)", line)
        if not instruction:
            continue
        mnemonic, operands = instruction.groups()
        if (mnemonic.startswith("f") or mnemonic in {"emms", "ldmxcsr", "stmxcsr", "lfence", "mfence", "sfence"}
                or re.search(r"\b(?:[xyz]mm\d+|mm[0-7])\b", operands)):
            forbidden.append(line.strip())
    assert not forbidden, f"unexpected x87/SIMD instructions: {forbidden[:10]}"

    return {
        "file": str(path.resolve()),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "size": path.stat().st_size,
        "entry_rva": hex(opt.AddressOfEntryPoint),
        "checksum": hex(opt.CheckSum),
        "imports": imports,
        "xp_export_references": references,
        "x87_simd_scan": "passed",
        "runtime_test": "not performed",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("driver", type=Path)
    parser.add_argument("--prefix", default="i686-w64-mingw32-")
    parser.add_argument("--xp-system32", type=Path)
    parser.add_argument("--scsiport", type=Path)
    args = parser.parse_args()
    report = check(args.driver, args.prefix, args.xp_system32, args.scsiport)
    output = args.driver.with_suffix(".validation.json")
    output.write_text(json.dumps(report, indent=2) + "\n")
    count = sum(map(len, report["imports"].values()))
    print(f"PASS: {args.driver}: native XP x86, DriverEntry@8, checksum, relocations, resources; {count} kernel imports; no x87/SIMD")
    print("XP export comparison: " + ("passed" if args.xp_system32 else "not requested"))
    print("Runtime/hardware validation: not performed")
