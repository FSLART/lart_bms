"""
eeprom_dump_to_bin.py

Converts the UART hex dump from BmsConfig_HexDump() into a .bin file.

Usage:
    python eeprom_dump_to_bin.py dump.txt eeprom.bin

Paste the raw UART output into dump.txt, then run the script.
The output eeprom.bin can be opened in any hex editor (HxD, ImHex, 010 Editor).
"""

import sys

def parse_dump(input_path, output_path):

    binary_data = bytearray()

    with open(input_path, "r") as f:
        lines = f.readlines()

    for line in lines:
        line = line.strip()

        # Only process lines that start with a hex address like "0x000"
        if not line.startswith("0x"):
            continue

        # Split off the address part (e.g. "0x000")
        parts = line.split()
        if len(parts) < 2:
            continue

        # Collect all hex byte tokens — stop at the ASCII section "|...|"
        hex_bytes = []
        for token in parts[1:]:
            if token.startswith("|"):
                break
            # Skip the extra space marker between byte groups (not a real token)
            try:
                byte_value = int(token, 16)
                hex_bytes.append(byte_value)
            except ValueError:
                continue

        binary_data.extend(hex_bytes)

    with open(output_path, "wb") as f:
        f.write(binary_data)

    print(f"Done: {len(binary_data)} bytes written to '{output_path}'")
    print(f"Expected: 1024 bytes (24FC08 full dump)")

    if len(binary_data) != 1024:
        print(f"WARNING: got {len(binary_data)} bytes instead of 1024 — check the dump is complete")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python eep_to_bin.py <dump.txt> <output.bin>")
        sys.exit(1)

    parse_dump(sys.argv[1], sys.argv[2])