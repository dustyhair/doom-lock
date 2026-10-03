#!/usr/bin/env python3
"""Measure maze rendering on fixed tours, optionally against a Git revision."""
import argparse
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compare", help="Git revision containing the reference level.c")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
output = root / "build/performance/maze-benchmark"
output.mkdir(parents=True, exist_ok=True)
headers = root / ".build-deps/root/usr/include"
source = output / "benchmark.c"
source.write_text(r'''
#include <time.h>
#include <inttypes.h>
#include LEVEL_SOURCE

static double now(void) {
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return timestamp.tv_sec + timestamp.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    srand(strtoul(argv[2], NULL, 10));
    if (!level_init(argv[1])) return 1;
    /* Advance and render every frame, without X11 uploads or sprite drawing. */
    double started = now();
    for (int i = 0; i < 1500; i++) {
        level_tick(0.14);
        render(260);
    }
    double frame_ms = (now() - started) * 1000 / 1500;
    uint64_t hash = UINT64_C(14695981039346656037);
    unsigned char *pixels = cairo_image_surface_get_data(view);
    for (int i = 0; i < cairo_image_surface_get_stride(view) * 260; i++) {
        hash ^= pixels[i];
        hash *= UINT64_C(1099511628211);
    }
    printf("%s %.4f %016" PRIx64 "\n", style->map, frame_ms, hash);
    return 0;
}
''')

versions = {"current": root / "level.c"}
if args.compare:
    reference = output / "reference-level.c"
    reference.write_bytes(subprocess.check_output(
        ["git", "show", args.compare + ":level.c"], cwd=root))
    versions = {"reference": reference, **versions}

results = {}
for label, level_source in versions.items():
    binary = output / label
    subprocess.run([
        "cc", "-std=c11", "-D_GNU_SOURCE", "-O2", "-fno-strict-aliasing",
        '-DLEVEL_SOURCE="' + str(level_source) + '"',
        "-I" + str(root / "include"), "-I" + str(headers),
        "-I" + str(headers / "cairo"), "-I/usr/include/cairo",
        str(source), "-o", str(binary), "-l:libcairo.so.2", "-lm",
    ], check=True)
    rows = {}
    for seed in [1, 2, 3, 7, 16]:
        name, elapsed, checksum = subprocess.check_output(
            [str(binary), str(root / "assets"), str(seed)], text=True).split()
        rows[name] = {"frame_ms": float(elapsed), "checksum": checksum}
        print(f"{label}: {name} {elapsed} ms/frame, checksum {checksum}", flush=True)
    results[label] = rows

if args.compare:
    for name, current in results["current"].items():
        reference = results["reference"][name]
        speedup = reference["frame_ms"] / current["frame_ms"]
        print(f"{name}: {speedup:.2f}x faster; "
              f"matching frame: {reference['checksum'] == current['checksum']}")

(output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
