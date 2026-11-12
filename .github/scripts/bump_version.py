#!/usr/bin/env python3
"""Read, bump, and synchronize GAME_VERSION across CMakeLists.txt and version.h."""

import argparse
import re
import sys

CMAKE_PATH = "CMakeLists.txt"
HEADER_PATH = "Source/Headers/version.h"

CMAKE_RE = re.compile(r'(set\(\s*GAME_VERSION\s+")([^"]+)("\s*\))')
HDR_STR_RE = re.compile(r'(#define\s+GAME_VERSION\s+")([^"]+)(")')
HDR_NUM_RE = {
    "MAJOR": re.compile(r'(#define\s+GAME_VERSION_MAJOR\s+)(\d+)'),
    "MINOR": re.compile(r'(#define\s+GAME_VERSION_MINOR\s+)(\d+)'),
    "PATCH": re.compile(r'(#define\s+GAME_VERSION_PATCH\s+)(\d+)'),
}


def parse_version(version_str: str) -> tuple[int, int, int]:
    match = re.fullmatch(r"(\d+)\.(\d+)(?:\.(\d+))?", version_str.strip())
    if not match:
        raise ValueError(f"Version must be major.minor or major.minor.patch, got {version_str!r}")
    major = int(match.group(1))
    minor = int(match.group(2))
    patch = int(match.group(3) or 0)
    return major, minor, patch


def read_cmake_version() -> str:
    with open(CMAKE_PATH, encoding="utf-8") as f:
        text = f.read()
    match = CMAKE_RE.search(text)
    if not match:
        raise RuntimeError(f"Could not find GAME_VERSION in {CMAKE_PATH}")
    return match.group(2)


def compute_bumped_version(current: str, mode: str) -> str:
    major, minor, patch = parse_version(current)
    if mode == "major":
        return f"{major + 1}.0"
    if mode == "minor":
        return f"{major}.{minor + 1}"
    if mode == "patch":
        return f"{major}.{minor}.{patch + 1}"
    raise ValueError(f"Unknown bump mode: {mode}")


def write_version(new_version: str) -> None:
    major, minor, patch = parse_version(new_version)

    with open(CMAKE_PATH, encoding="utf-8") as f:
        cmake_text = f.read()
    updated_cmake, cmake_count = CMAKE_RE.subn(rf"\g<1>{new_version}\g<3>", cmake_text, count=1)
    if cmake_count != 1:
        raise RuntimeError(f"Expected 1 GAME_VERSION replacement in {CMAKE_PATH}, got {cmake_count}")

    with open(HEADER_PATH, encoding="utf-8") as f:
        hdr_text = f.read()
    updated_hdr, hdr_count = HDR_STR_RE.subn(rf"\g<1>{new_version}\g<3>", hdr_text, count=1)
    if hdr_count != 1:
        raise RuntimeError(f"Expected 1 GAME_VERSION replacement in {HEADER_PATH}, got {hdr_count}")

    for field, val in (("MAJOR", major), ("MINOR", minor), ("PATCH", patch)):
        updated_hdr, count = HDR_NUM_RE[field].subn(rf"\g<1>{val}", updated_hdr, count=1)
        if count != 1:
            raise RuntimeError(
                f"Expected 1 GAME_VERSION_{field} replacement in {HEADER_PATH}, got {count}"
            )

    with open(CMAKE_PATH, "w", encoding="utf-8") as f:
        f.write(updated_cmake)
    with open(HEADER_PATH, "w", encoding="utf-8") as f:
        f.write(updated_hdr)


def main() -> None:
    parser = argparse.ArgumentParser(description="Manage Cro-Mag Rally version numbers")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--print-current", action="store_true", help="Print current GAME_VERSION")
    group.add_argument("--mode", choices=("patch", "minor", "major"), help="Increment version")
    group.add_argument("--set", dest="exact_version", help="Set exact version (e.g. 5.0)")
    parser.add_argument("--dry-run", action="store_true", help="Compute version without writing files")
    args = parser.parse_args()

    current = read_cmake_version()
    if args.print_current:
        print(current)
        return

    new_version = args.exact_version.strip() if args.exact_version else compute_bumped_version(current, args.mode)
    parse_version(new_version)  # validate format

    if not args.dry_run:
        write_version(new_version)
    print(new_version)


if __name__ == "__main__":
    main()
