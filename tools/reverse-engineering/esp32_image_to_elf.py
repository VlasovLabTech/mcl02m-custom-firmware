#!/usr/bin/env python3
"""Convert an ESP32 application image into a mapped ELF32 Xtensa executable.

The source image is parsed by the installed Espressif esptool library. The
resulting ELF contains one PT_LOAD and one section for every image segment,
preserving the exact load addresses and entry point used by the firmware.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from dataclasses import asdict, dataclass
from pathlib import Path

from esptool.bin_image import ESP32FirmwareImage


ELF_HEADER_SIZE = 52
PROGRAM_HEADER_SIZE = 32
SECTION_HEADER_SIZE = 40
EM_XTENSA = 94
PT_LOAD = 1
SHT_NULL = 0
SHT_PROGBITS = 1
SHT_STRTAB = 3
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
PF_X = 0x1
PF_W = 0x2
PF_R = 0x4


@dataclass(frozen=True)
class SegmentInfo:
    index: int
    name: str
    load_address: int
    size: int
    image_header_offset: int
    image_data_offset: int
    elf_offset: int
    section_flags: int
    program_flags: int
    sha256: str


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def classify_segment(address: int, index: int) -> tuple[str, int, int]:
    if 0x400D0000 <= address < 0x40400000:
        return ".flash.text", SHF_ALLOC | SHF_EXECINSTR, PF_R | PF_X
    if 0x3F400000 <= address < 0x3F800000:
        return ".flash.rodata", SHF_ALLOC, PF_R
    if 0x40080000 <= address < 0x400D0000:
        suffix = "vectors" if address == 0x40080000 else f"seg{index}"
        return f".iram0.{suffix}", SHF_ALLOC | SHF_EXECINSTR, PF_R | PF_X
    if 0x3FF80000 <= address < 0x40000000:
        return f".dram0.seg{index}", SHF_ALLOC | SHF_WRITE, PF_R | PF_W
    return f".segment{index}", SHF_ALLOC, PF_R


def c_string(data: bytes) -> str:
    return data.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def app_descriptor(first_segment: bytes) -> dict | None:
    if len(first_segment) < 256 or first_segment[:4] != b"2T\xCD\xAB":
        return None
    return {
        "magic": f"0x{struct.unpack_from('<I', first_segment, 0)[0]:08x}",
        "secure_version": struct.unpack_from("<I", first_segment, 4)[0],
        "app_version": c_string(first_segment[16:48]),
        "project_name": c_string(first_segment[48:80]),
        "compile_time": c_string(first_segment[80:96]),
        "compile_date": c_string(first_segment[96:112]),
        "idf_version": c_string(first_segment[112:144]),
        "elf_sha256": first_segment[144:176].hex(),
    }


def build_elf(image_path: Path, elf_path: Path, manifest_path: Path) -> None:
    image_bytes = image_path.read_bytes()
    with image_path.open("rb") as stream:
        image = ESP32FirmwareImage(stream)

    program_header_offset = ELF_HEADER_SIZE
    segment_data_offset = align(
        ELF_HEADER_SIZE + PROGRAM_HEADER_SIZE * len(image.segments), 0x10
    )

    segment_layout: list[tuple[object, str, int, int, int]] = []
    cursor = segment_data_offset
    used_names: dict[str, int] = {}
    for index, segment in enumerate(image.segments):
        base_name, section_flags, program_flags = classify_segment(segment.addr, index)
        count = used_names.get(base_name, 0)
        used_names[base_name] = count + 1
        name = base_name if count == 0 else f"{base_name}.{count}"
        cursor = align(cursor, 0x10)
        segment_layout.append((segment, name, section_flags, program_flags, cursor))
        cursor += len(segment.data)

    shstr = bytearray(b"\0")
    name_offsets: dict[str, int] = {"": 0}
    for _, name, _, _, _ in segment_layout:
        name_offsets[name] = len(shstr)
        shstr.extend(name.encode("ascii") + b"\0")
    name_offsets[".shstrtab"] = len(shstr)
    shstr.extend(b".shstrtab\0")

    shstr_offset = align(cursor, 0x10)
    section_header_offset = align(shstr_offset + len(shstr), 0x10)
    section_count = len(segment_layout) + 2
    shstr_index = len(segment_layout) + 1
    total_size = section_header_offset + SECTION_HEADER_SIZE * section_count
    output = bytearray(total_size)

    e_ident = b"\x7fELF" + bytes(
        [1, 1, 1, 0, 0]
    ) + b"\0" * 7  # ELF32, little-endian, current version, SYSV
    elf_header = struct.pack(
        "<16sHHIIIIIHHHHHH",
        e_ident,
        2,  # ET_EXEC
        EM_XTENSA,
        1,
        image.entrypoint,
        program_header_offset,
        section_header_offset,
        0,
        ELF_HEADER_SIZE,
        PROGRAM_HEADER_SIZE,
        len(segment_layout),
        SECTION_HEADER_SIZE,
        section_count,
        shstr_index,
    )
    output[:ELF_HEADER_SIZE] = elf_header

    manifest_segments: list[SegmentInfo] = []
    for index, (segment, name, section_flags, program_flags, elf_offset) in enumerate(
        segment_layout
    ):
        data = bytes(segment.data)
        output[elf_offset : elf_offset + len(data)] = data
        program_header = struct.pack(
            "<IIIIIIII",
            PT_LOAD,
            elf_offset,
            segment.addr,
            segment.addr,
            len(data),
            len(data),
            program_flags,
            4,
        )
        ph_offset = program_header_offset + index * PROGRAM_HEADER_SIZE
        output[ph_offset : ph_offset + PROGRAM_HEADER_SIZE] = program_header

        file_header_offset = int(getattr(segment, "file_offs", -1))
        manifest_segments.append(
            SegmentInfo(
                index=index,
                name=name,
                load_address=segment.addr,
                size=len(data),
                image_header_offset=file_header_offset,
                image_data_offset=(file_header_offset + 8 if file_header_offset >= 0 else -1),
                elf_offset=elf_offset,
                section_flags=section_flags,
                program_flags=program_flags,
                sha256=hashlib.sha256(data).hexdigest(),
            )
        )

    output[shstr_offset : shstr_offset + len(shstr)] = shstr

    # Section zero is the mandatory all-zero SHT_NULL entry.
    for index, (_, name, section_flags, _, elf_offset) in enumerate(segment_layout, 1):
        segment = segment_layout[index - 1][0]
        section_header = struct.pack(
            "<IIIIIIIIII",
            name_offsets[name],
            SHT_PROGBITS,
            section_flags,
            segment.addr,
            elf_offset,
            len(segment.data),
            0,
            0,
            4,
            0,
        )
        offset = section_header_offset + index * SECTION_HEADER_SIZE
        output[offset : offset + SECTION_HEADER_SIZE] = section_header

    shstr_header = struct.pack(
        "<IIIIIIIIII",
        name_offsets[".shstrtab"],
        SHT_STRTAB,
        0,
        0,
        shstr_offset,
        len(shstr),
        0,
        0,
        1,
        0,
    )
    offset = section_header_offset + shstr_index * SECTION_HEADER_SIZE
    output[offset : offset + SECTION_HEADER_SIZE] = shstr_header

    elf_path.parent.mkdir(parents=True, exist_ok=True)
    elf_path.write_bytes(output)

    manifest = {
        "input": str(image_path.resolve()),
        "input_size": len(image_bytes),
        "input_sha256": hashlib.sha256(image_bytes).hexdigest(),
        "output": str(elf_path.resolve()),
        "output_size": len(output),
        "output_sha256": hashlib.sha256(output).hexdigest(),
        "architecture": "Xtensa little-endian 32-bit",
        "elf_machine": EM_XTENSA,
        "entry_point": image.entrypoint,
        "entry_point_hex": f"0x{image.entrypoint:08x}",
        "segment_count": len(manifest_segments),
        "app_descriptor": app_descriptor(bytes(image.segments[0].data)),
        "segments": [asdict(item) for item in manifest_segments],
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    manifest = args.manifest or args.elf.with_suffix(args.elf.suffix + ".json")
    build_elf(args.image, args.elf, manifest)
    print(f"ELF: {args.elf}")
    print(f"Manifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
