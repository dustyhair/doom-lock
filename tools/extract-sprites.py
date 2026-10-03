#!/usr/bin/env python3
"""Extract local Doom II sprites, preserving the patch origins and palette."""
import argparse
import csv
import json
import struct
from pathlib import Path

from PIL import Image
from PIL import ImageChops

# Read the same simple catalogue included by the C loader and renderer.
_catalogue = Path(__file__).resolve().parents[1] / "include/monsters.def"
_records = [next(csv.reader([line.removeprefix("MONSTER(").removesuffix(")")], skipinitialspace=True))
            for line in _catalogue.read_text().splitlines() if line.startswith("MONSTER(")]
MONSTERS = [(slug, label, prefix, death) for slug, label, prefix, death, walk, pain, floating in _records]
WALK = {record[2]: record[4] for record in _records}
PAIN = {record[2]: record[5] for record in _records}
# Matching combinations from MAP01, MAP02, MAP05, MAP14, and MAP24.
WALLS = ["TEKGREN2", "TEKGREN5", "STONE4", "PIPEWAL1", "BIGBRIK1", "BIGBRIK2",
         "BSTONE1", "BSTONE2", "SKIN2", "TANROCK5"]
FLATS = ["FLOOR3_3", "GRNLITE1", "FLAT5_4", "FLAT1", "FLAT10", "FLOOR5_4",
         "FLAT1_2", "FLOOR7_1", "CEIL5_1", "NUKAGE1", "NUKAGE2", "NUKAGE3"]


def extract_level(lumps, palette, patch, destination):
    """Compose wall patches from TEXTURE1/PNAMES and decode palette-indexed flats."""
    destination.mkdir(parents=True, exist_ok=True)
    pnames = lumps["PNAMES"]
    count = struct.unpack_from("<I", pnames)[0]
    names = [pnames[4 + index * 8:12 + index * 8].rstrip(b"\0").decode("ascii").upper()
             for index in range(count)]
    definitions = {}
    for table_name in ["TEXTURE1", "TEXTURE2"]:
        if table_name not in lumps:
            continue
        table = lumps[table_name]
        for index in range(struct.unpack_from("<I", table)[0]):
            offset = struct.unpack_from("<I", table, 4 + index * 4)[0]
            name, _, width, height, _, patch_count = struct.unpack_from("<8sIHHIH", table, offset)
            name = name.rstrip(b"\0").decode("ascii").upper()
            if name not in WALLS:
                continue
            if not (0 < width <= 1024 and 0 < height <= 1024):
                raise ValueError(f"Invalid texture dimensions: {name}")
            image = Image.new("RGBA", (width, height), (0, 0, 0, 255))
            for part in range(patch_count):
                x, y, patch_index, _, _ = struct.unpack_from("<hhhhh", table, offset + 22 + part * 10)
                if not 0 <= patch_index < len(names):
                    raise ValueError(f"Invalid texture patch index: {name}")
                fragment = patch(names[patch_index], False)
                image.paste(fragment, (x, y), fragment)
            definitions[name] = image
    for name in WALLS:
        definitions[name].convert("RGB").save(destination / f"{name}.png")
    for name in FLATS:
        if len(lumps[name]) != 4096:
            raise ValueError(f"Invalid flat size: {name}")
        image = Image.frombytes("P", (64, 64), lumps[name])
        image.putpalette(palette)
        image.convert("RGB").save(destination / f"{name}.png")
    print(f"Extracted {len(WALLS)} walls and {len(FLATS)} floors/ceilings to {destination}")


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
        lumps[name.rstrip(b"\0").decode("ascii").upper()] = data[offset:offset + size]
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
    extract_level(lumps, palette, patch, destination / "level")
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
        # Doom patch origins locate the actor's center and floor independently
        # of padding needed by its movement, pain, and death frames.
        (folder / "origin.txt").write_text(f"{128 - bounds[0]} {170 - bounds[1]}\n")
        images[0].crop(bounds).save(folder / "idle.png")
        for index, image in enumerate(images[1:]):
            image.crop(bounds).save(folder / f"death-{index:02d}.png")
        for index, image in enumerate(walks):
            image.crop(bounds).save(folder / f"walk-{index:02d}.png")
        pain.crop(bounds).save(folder / "pain.png")
        manifest.append({"id": slug, "name": label, "frames": len(frames), "walk_frames": len(walk),
                         "origin_x": 128 - bounds[0], "origin_y": 170 - bounds[1]})
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Extracted {len(manifest)} monsters to {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wad", type=Path)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    extract(arguments.wad, arguments.destination)
