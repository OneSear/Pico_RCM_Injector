#!/usr/bin/env python3
"""Convert an RP2040/RP2350 flash binary into a drag-and-drop UF2 file."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
FLASH_BASE = 0x10000000
UF2_PAYLOAD_SIZE = 256
UF2_BLOCK_SIZE = 512

# UF2 family IDs (from Microsoft uf2families.json / Raspberry Pi).
FAMILY_IDS = {
    "rp2040": 0xE48BFF56,
    "rp2350-arm-s": 0xE48BFF59,   # RP2350 Arm, secure-boot disabled (default SDK build)
    "rp2350-riscv": 0xE48BFF5A,
    "rp2350-arm-ns": 0xE48BFF5B,
}
RP2040_FLASH_BASE = FLASH_BASE  # retained for backwards-compatible --base default


def convert(data: bytes, base_address: int, family_id: int) -> bytes:
    block_count = (len(data) + UF2_PAYLOAD_SIZE - 1) // UF2_PAYLOAD_SIZE
    blocks = bytearray()

    for block_no in range(block_count):
        offset = block_no * UF2_PAYLOAD_SIZE
        chunk = data[offset : offset + UF2_PAYLOAD_SIZE]
        chunk = chunk.ljust(UF2_PAYLOAD_SIZE, b"\x00")

        header = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            UF2_FLAG_FAMILY_ID_PRESENT,
            base_address + offset,
            UF2_PAYLOAD_SIZE,
            block_no,
            block_count,
            family_id,
        )

        block = header + chunk + bytes(UF2_BLOCK_SIZE - len(header) - UF2_PAYLOAD_SIZE - 4)
        block += struct.pack("<I", UF2_MAGIC_END)
        blocks.extend(block)

    return bytes(blocks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Input .bin file")
    parser.add_argument("output", type=Path, help="Output .uf2 file")
    parser.add_argument("--base", type=lambda value: int(value, 0), default=FLASH_BASE)
    parser.add_argument(
        "--family",
        default="rp2350-arm-s",
        help="UF2 family: " + ", ".join(FAMILY_IDS) + " (or a raw 0x id)",
    )
    args = parser.parse_args()

    family_id = FAMILY_IDS.get(args.family)
    if family_id is None:
        family_id = int(args.family, 0)

    data = args.input.read_bytes()
    if not data:
        raise SystemExit("input binary is empty")

    args.output.write_bytes(convert(data, args.base, family_id))
    print(f"wrote {args.output} ({len(data)} input bytes, family 0x{family_id:08x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
