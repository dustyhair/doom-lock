#!/usr/bin/env python3
"""Extract local Doom II sprites, preserving the patch origins and palette."""
import argparse
import json
import struct
from pathlib import Path

from PIL import Image
from PIL import ImageChops

# Normal death frames from id Software's linuxdoom-1.10/info.c.
MONSTERS = [
    ("zombieman", "ZOMBIEMAN", "POSS", "HIJKL"),
    ("shotgun-guy", "SHOTGUN GUY", "SPOS", "HIJKL"),
    ("imp", "IMP", "TROO", "IJKLM"),
    ("demon", "DEMON", "SARG", "IJKLMN"),
    ("cacodemon", "CACODEMON", "HEAD", "GHIJKL"),
    ("baron", "BARON OF HELL", "BOSS", "IJKLMNO"),
    ("hell-knight", "HELL KNIGHT", "BOS2", "IJKLMNO"),
    ("revenant", "REVENANT", "SKEL", "LMNOPQ"),
    ("mancubus", "MANCUBUS", "FATT", "KLMNOPQRST"),
    ("chaingunner", "CHAINGUNNER", "CPOS", "HIJKLMN"),
    ("arachnotron", "ARACHNOTRON", "BSPI", "JKLMNOP"),
    ("lost-soul", "LOST SOUL", "SKUL", "FGHIJK"),
    ("pain-elemental", "PAIN ELEMENTAL", "PAIN", "HIJKLM"),
    ("cyberdemon", "CYBERDEMON", "CYBR", "HIJKLMNOP"),
]
WALK = {"HEAD": "A", "SKUL": "AB", "PAIN": "ABC",
        "SKEL": "ABCDEF", "FATT": "ABCDEF", "BSPI": "ABCDEF"}
PAIN = {"POSS": "G", "SPOS": "G", "TROO": "H", "SARG": "H", "HEAD": "E",
        "BOSS": "H", "BOS2": "H", "SKEL": "L", "FATT": "J", "CPOS": "G",
        "BSPI": "I", "SKUL": "E", "PAIN": "G", "CYBR": "G"}


def extract(wad_path, destination):
    data = wad_path.read_bytes()
    magic, count, directory = struct.unpack_from("<4sii", data)
    if magic not in (b"IWAD", b"PWAD") or count < 0:
        raise ValueError("Invalid WAD header")
    lumps = {}
    for index in range(count):
        offset, size, name = struct.unpack_from("<ii8s", data, directory + index * 16)
        if offset < 0 or size < 0 or offset + size > len(data):
            raise ValueError("Invalid lump bounds")
        lumps[name.rstrip(b"\0").decode("ascii")] = data[offset:offset + size]
    palette = lumps["PLAYPAL"][:768]
    aliases = {}
    for name in lumps:
        if len(name) == 8 and name[5].isdigit() and name[7].isdigit():
            aliases[name[:6]] = (name, False)
            aliases[name[:4] + name[6:8]] = (name, True)

    def patch(name, frame_canvas=True):
        name, flip = aliases.get(name, (name, False))
        raw = lumps[name]
        width, height, left, top = struct.unpack_from("<hhhh", raw)
        if not (0 < width <= 512 and 0 < height <= 512):
            raise ValueError(f"Invalid patch dimensions: {name}")
        sprite = Image.new("RGBA", (width, height))
        for x in range(width):
            offset = struct.unpack_from("<I", raw, 8 + 4 * x)[0]
            while raw[offset] != 255:
                y, length = raw[offset:offset + 2]
                for row in range(length):
                    color = raw[offset + 3 + row] * 3
                    sprite.putpixel((x, y + row), tuple(palette[color:color + 3]) + (255,))
                offset += length + 4
        if not frame_canvas:
            return sprite
        if flip:
            sprite = sprite.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            left = width - left
        canvas = Image.new("RGBA", (256, 192))
        canvas.paste(sprite, (128 - left, 170 - top))
        return canvas

    destination.mkdir(parents=True, exist_ok=True)
    patch("STFDEAD0", False).save(destination / "player-dead.png")
    bfg = [patch("BFE2" + frame + "0") for frame in "ABCD"]
    bounds = Image.new("L", bfg[0].size)
    for image in bfg:
        bounds = ImageChops.lighter(bounds, image.getchannel("A"))
    for index, image in enumerate(bfg):
        image.crop(bounds.getbbox()).save(destination / f"bfg-{index:02d}.png")
    for index, frame in enumerate("AB"):
        projectile = patch("BFS1" + frame + "0", False)
        projectile.save(destination / f"bfg-projectile-{index:02d}.png")
    def sprite_name(prefix, frame):
        name = prefix + frame + "0"
        return name if name in lumps else prefix + frame + "1"
    manifest = []
    for slug, label, prefix, frames in MONSTERS:
        folder = destination / slug
        folder.mkdir(exist_ok=True)
        walk = WALK.get(prefix, "ABCD")
        images = [patch(sprite_name(prefix, "A"))]
        for index, frame in enumerate(frames):
            images.append(patch(sprite_name(prefix, frame)))
        walks = [patch(sprite_name(prefix, frame)) for frame in walk]
        pain = patch(sprite_name(prefix, PAIN[prefix]))
        combined = Image.new("L", images[0].size)
        for image in images + walks + [pain]:
            combined = ImageChops.lighter(combined, image.getchannel("A"))
        bounds = combined.getbbox()
        images[0].crop(bounds).save(folder / "idle.png")
        for index, image in enumerate(images[1:]):
            image.crop(bounds).save(folder / f"death-{index:02d}.png")
        for index, image in enumerate(walks):
            image.crop(bounds).save(folder / f"walk-{index:02d}.png")
        pain.crop(bounds).save(folder / "pain.png")
        manifest.append({"id": slug, "name": label, "frames": len(frames), "walk_frames": len(walk)})
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Extracted {len(manifest)} monsters to {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wad", type=Path)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    extract(arguments.wad, arguments.destination)
