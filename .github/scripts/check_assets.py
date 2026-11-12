#!/usr/bin/env python3
"""Lightweight asset, localization, and version metadata sanity checks for CI."""

import csv
import glob
import os
import re
import struct
import subprocess
import sys

NAV_HEADER_FMT = "<4sHHHH5fHHhh4f"
NAV_HEADER_SIZE = struct.calcsize(NAV_HEADER_FMT)
assert NAV_HEADER_SIZE == 56


def fail(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def check_version_sync() -> None:
    with open("CMakeLists.txt", encoding="utf-8") as f:
        cmake_text = f.read()
    m_cmake = re.search(r'set\(\s*GAME_VERSION\s+"([^"]+)"\s*\)', cmake_text)
    if not m_cmake:
        fail("Could not find GAME_VERSION in CMakeLists.txt")

    with open("Source/Headers/version.h", encoding="utf-8") as f:
        header_text = f.read()
    m_hdr = re.search(r'#define\s+GAME_VERSION\s+"([^"]+)"', header_text)
    if not m_hdr:
        fail("Could not find GAME_VERSION in Source/Headers/version.h")

    cmake_ver = m_cmake.group(1)
    hdr_ver = m_hdr.group(1)
    if cmake_ver != hdr_ver:
        fail(
            f"Version mismatch: CMakeLists.txt has '{cmake_ver}' "
            f"but Source/Headers/version.h has '{hdr_ver}'"
        )

    res = subprocess.run(
        [sys.executable, "build.py", "--print-artifact-name"],
        check=True,
        capture_output=True,
        text=True,
    )
    artifact_name = res.stdout.strip()
    if not artifact_name or cmake_ver not in artifact_name:
        fail(f"Unexpected artifact name from build.py: {artifact_name!r}")

    print(f"Version sync OK: {cmake_ver} (artifact: {artifact_name})")


def check_models() -> None:
    for path in ("Data/Models/carparts.bg3d", "Data/Models/carselect.bg3d"):
        if not os.path.isfile(path) or os.path.getsize(path) == 0:
            fail(f"Missing or empty model file: {path}")
    print("Core model files OK")


def check_strings_csv() -> None:
    path = "Data/System/strings.csv"
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    if len(rows) < 100:
        fail(f"{path} has unexpectedly few rows: {len(rows)}")
    for line_no, row in enumerate(rows, start=1):
        if len(row) not in (1, 6):
            fail(f"{path}:{line_no} expected 1 or 6 columns, got {len(row)}: {row!r}")
    print(f"Localization CSV OK: {len(rows)} rows")


def check_battle_nav_files() -> None:
    rsrc_files = sorted(glob.glob("Data/Terrain/Battle_*.ter.rsrc"))
    if len(rsrc_files) < 8:
        fail(f"Expected at least 8 Battle_*.ter.rsrc files, found {len(rsrc_files)}")

    for rsrc_path in rsrc_files:
        nav_path = rsrc_path.replace(".ter.rsrc", ".nav")
        if not os.path.isfile(nav_path):
            fail(f"Missing precomputed navigation file for {rsrc_path}: {nav_path}")

        with open(nav_path, "rb") as f:
            data = f.read()
        if len(data) < NAV_HEADER_SIZE:
            fail(f"{nav_path} is truncated ({len(data)} bytes)")

        (
            magic,
            version,
            _flags,
            grid_w,
            grid_h,
            cell_size,
            _min_x,
            _min_z,
            _max_x,
            _max_z,
            num_nodes,
            num_pickups,
            base0,
            base1,
            _b0x,
            _b0z,
            _b1x,
            _b1z,
        ) = struct.unpack(NAV_HEADER_FMT, data[:NAV_HEADER_SIZE])

        if magic != b"CMNV" or version != 1:
            fail(f"{nav_path} invalid magic/version: {magic!r} v{version}")
        if grid_w == 0 or grid_h == 0 or cell_size <= 0.0 or num_nodes == 0:
            fail(
                f"{nav_path} invalid dimensions: grid={grid_w}x{grid_h}, "
                f"cell_size={cell_size}, nodes={num_nodes}"
            )
        if not (0 <= base0 < num_nodes and 0 <= base1 < num_nodes):
            fail(f"{nav_path} invalid base nodes: ({base0}, {base1}) for {num_nodes} nodes")

        expected_size = (
            NAV_HEADER_SIZE
            + (grid_w * grid_h) * 3
            + num_nodes * 20
            + num_pickups * 16
            + (num_nodes * num_nodes) * 4
        )
        if len(data) != expected_size:
            fail(f"{nav_path} size mismatch: got {len(data)}, expected {expected_size}")

    print(f"Battle navigation files OK: {len(rsrc_files)} arenas verified")


def main() -> None:
    check_version_sync()
    check_models()
    check_strings_csv()
    check_battle_nav_files()


if __name__ == "__main__":
    main()
