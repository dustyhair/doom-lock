#!/usr/bin/env python3
"""Exercise WAD decoding, malformed input, and optional local artwork parity."""
import argparse
import importlib.util
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--wad", type=Path, help="Also compare a local WAD against extracted PNGs")
parser.add_argument("--assets", type=Path, help="PNG reference folder for --wad")
args = parser.parse_args()
if bool(args.wad) != bool(args.assets):
    parser.error("--wad and --assets must be supplied together")
root = Path(__file__).resolve().parents[1]
output = root / "build/test-assets"
output.mkdir(parents=True, exist_ok=True)
headers = root / ".build-deps/root/usr/include"
binary = output / "test-assets"
subprocess.run([
    "cc", "-std=c11", "-D_GNU_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
    "-Wno-missing-field-initializers", "-fsanitize=address,undefined",
    "-fno-omit-frame-pointer", "-I" + str(root / "include"),
    "-I" + str(root / "build"), "-I" + str(headers),
    "-I" + str(headers / "cairo"), "-I/usr/include/cairo",
    str(root / "tests/test-assets.c"), "-o", str(binary), "-l:libcairo.so.2",
], check=True)
spec = importlib.util.spec_from_file_location("extract", root / "tools/extract-sprites.py")
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)


def patch(width=8, height=12, left=3, top=10):
    columns = []
    offsets = []
    position = 8 + width * 4
    for x in range(width):
        column = bytes([0, height, 0]) + bytes((x * 7 + y * 3) % 256 for y in range(height)) + bytes([0, 255])
        offsets.append(position)
        columns.append(column)
        position += len(column)
    return struct.pack("<hhhh", width, height, left, top) + struct.pack("<" + "I" * width, *offsets) + b"".join(columns)


def make_wad(lumps):
    payload = b"".join(value for name, value in lumps)
    entries = []
    position = 12
    for name, value in lumps:
        entries.append(struct.pack("<ii8s", position, len(value), name.encode("ascii")))
        position += len(value)
    return struct.pack("<4sii", b"IWAD", len(lumps), position) + payload + b"".join(entries)


palette = bytes(value for i in range(256) for value in (i, 255 - i, i // 2))
lumps = [("PLAYPAL", palette), ("STFDEAD0", patch()), ("PNAMES", struct.pack("<I8s", 1, b"TESTPAT")),
         ("TESTPAT", patch())]
for prefix, letters in [("BFE2", "ABCD"), ("BFS1", "AB")]:
    lumps.extend((prefix + letter + "0", patch()) for letter in letters)
for slug, label, prefix, death in extract.MONSTERS:
    frames = set("A" + death + extract.WALK.get(prefix, "ABCD") + extract.PAIN[prefix])
    if prefix == "POSS":
        # Both halves of a paired rotation name, including mirrored C1.
        frames -= {"A", "C"}
        lumps.append(("POSSA1C1", patch()))
    lumps.extend((prefix + letter + "0", patch()) for letter in sorted(frames))
definitions = [struct.pack("<8sIHHIHhhhhh", name.encode("ascii"), 0, 64, 128, 0, 1, 0, 0, 0, 0, 0)
               for name in extract.WALLS]
offsets = []
position = 4 + 4 * len(definitions)
for definition in definitions:
    offsets.append(position)
    position += len(definition)
table = struct.pack("<I", len(definitions)) + struct.pack("<" + "I" * len(offsets), *offsets) + b"".join(definitions)
lumps.append(("TEXTURE1", table))
lumps.extend((f"STCFN{code:03d}", patch(8, 7, 0, 0)) for code in range(33, 96))
lumps.append(("GRNROCK", bytes(i % 256 for i in range(4096))))
lumps.extend((name, bytes(i % 256 for i in range(4096))) for name in extract.FLATS)
valid = output / "generated.wad"
valid.write_bytes(make_wad(lumps))
reference = output / "generated-png"
extract.extract(valid, reference)
subprocess.run([str(binary), "--compare", str(valid), str(reference)], check=True)


def replace_lump(name, replacement):
    return make_wad([(key, replacement if key == name else value) for key, value in lumps])


def change(data, offset, value):
    return data[:offset] + value + data[offset + len(value):]


raw = valid.read_bytes()
patch_data = patch()
bad_texture = change(table, offsets[0] + 26, struct.pack("<H", 65535))
cases = {
    "short header": b"IWAD",
    "wrong magic": b"NOPE" + raw[4:],
    "negative count": change(raw, 4, struct.pack("<i", -1)),
    "directory outside file": change(raw, 8, struct.pack("<I", 0x7fffffff)),
    "lump outside file": change(raw, len(raw) - len(lumps) * 16, struct.pack("<I", 0x7fffffff)),
    "missing palette": make_wad([(name, value) for name, value in lumps if name != "PLAYPAL"]),
    "short palette": replace_lump("PLAYPAL", palette[:-1]),
    "short patch": replace_lump("STFDEAD0", b"\0" * 4),
    "negative patch width": replace_lump("STFDEAD0", change(patch_data, 0, b"\xff\xff")),
    "column points at header": replace_lump("STFDEAD0", change(patch_data, 8, struct.pack("<I", 0))),
    "column outside patch": replace_lump("STFDEAD0", change(patch_data, 8, struct.pack("<I", 0xffffffff))),
    "post exceeds height": replace_lump("STFDEAD0", change(patch_data, 8 + 8 * 4 + 1, bytes([250]))),
    "missing post terminator": replace_lump("STFDEAD0", patch_data[:-1]),
    "too many empty posts": replace_lump("STFDEAD0", struct.pack("<hhhhI", 1, 1, 0, 0, 12) + bytes([0, 0, 0, 0]) * 3 + bytes([255])),
    "short flat": replace_lump(extract.FLATS[0], bytes(4095)),
    "pnames count overflow": replace_lump("PNAMES", struct.pack("<I", 0xffffffff)),
    "texture count overflow": replace_lump("TEXTURE1", struct.pack("<I", 0xffffffff)),
    "texture offset overflow": replace_lump("TEXTURE1", change(table, 4, struct.pack("<I", 0xffffffff))),
    "texture patch index overflow": replace_lump("TEXTURE1", bad_texture),
    "texture patch list truncated": replace_lump("TEXTURE1", table[:-1]),
    "invalid optional font": replace_lump("STCFN065", b"\0" * 4),
    "invalid UI flat": replace_lump("GRNROCK", b"\0" * 4095),
    "required sprite missing": make_wad([(name, value) for name, value in lumps if name != "BFS1A0"]),
}
for name, content in cases.items():
    malformed = output / "malformed.wad"
    malformed.write_bytes(content)
    result = subprocess.run([str(binary), "--validate", str(malformed)], capture_output=True, text=True)
    assert result.returncode == 1, (name, result.returncode, result.stderr)
    assert "Sanitizer" not in result.stderr, (name, result.stderr)
print(f"PASS: {len(cases)} malformed/incomplete WADs rejected under ASan/UBSan")
complete_pwad = output / "complete.pwad"
complete_pwad.write_bytes(b"PWAD" + raw[4:])
subprocess.run([str(binary), "--validate", str(complete_pwad)], check=True)
print("PASS: complete standalone PWAD accepted")

# Old content sets without UI graphics remain valid and use the fallback HUD.
no_ui = output / "without-ui.wad"
no_ui.write_bytes(make_wad([(name, value) for name, value in lumps
                           if not name.startswith("STCFN") and name != "GRNROCK"]))
subprocess.run([str(binary), "--validate", str(no_ui)], check=True)
print("PASS: WAD without optional font and panel graphics remains valid")

# Last duplicate lump wins, including the palette used by every decoded image.
duplicate = output / "duplicate.wad"
duplicate.write_bytes(make_wad(lumps + [("PLAYPAL", bytes(reversed(palette)))]))
duplicate_png = output / "duplicate-png"
extract.extract(duplicate, duplicate_png)
subprocess.run([str(binary), "--compare", str(duplicate), str(duplicate_png)], check=True)
if args.wad:
    subprocess.run([str(binary), "--compare", str(args.wad), str(args.assets)], check=True)
