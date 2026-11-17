#!/usr/bin/env python3
"""Stage and compress Cro-Mag Rally Data/ assets for Emscripten web builds.

Generates two output directories without modifying source Data/:
  <out_dir>/preload/   -> packed into CroMagRally.data (--preload-file)
  <out_dir>/lazy/Data/ -> served alongside CroMagRally.js for on-demand fetch()
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

LAZY_TRACK_MODELS = {
    "atlantis.bg3d",
    "aztec.bg3d",
    "china.bg3d",
    "coliseum.bg3d",
    "crete.bg3d",
    "desert.bg3d",
    "egypt.bg3d",
    "europe.bg3d",
    "ice.bg3d",
    "jungle.bg3d",
    "ramps.bg3d",
    "scandinavia.bg3d",
    "stonehenge.bg3d",
    "tarpits.bg3d",
}

PRELOAD_SONGS = {"ThemeSong.aiff"}


def needs_update(src: str, dst: str) -> bool:
    if not os.path.exists(dst):
        return True
    return os.path.getmtime(src) > os.path.getmtime(dst)


def copy_if_newer(src: str, dst: str) -> None:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if needs_update(src, dst):
        shutil.copy2(src, dst)


def transcode_aiff_to_ogg(src: str, dst_ogg: str, quality: str = "2") -> bool:
    os.makedirs(os.path.dirname(dst_ogg), exist_ok=True)
    if not needs_update(src, dst_ogg):
        return True

    afconvert = shutil.which("afconvert")
    oggenc = shutil.which("oggenc")
    ffmpeg = shutil.which("ffmpeg")

    if afconvert and oggenc:
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            tmp_wav = tmp.name
        try:
            subprocess.run(
                [afconvert, "-f", "WAVE", "-d", "LEI16", src, tmp_wav],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            subprocess.run(
                [oggenc, "-Q", "-q", quality, tmp_wav, "-o", dst_ogg],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            return True
        finally:
            if os.path.exists(tmp_wav):
                os.remove(tmp_wav)
    elif ffmpeg:
        subprocess.run(
            [ffmpeg, "-y", "-loglevel", "error", "-i", src, "-c:a", "libvorbis", "-q:a", quality, dst_ogg],
            check=True,
        )
        return True

    return False


def stage_assets(data_dir: str, out_dir: str) -> None:
    preload_dir = os.path.join(out_dir, "preload")
    lazy_data_dir = os.path.join(out_dir, "lazy", "Data")

    os.makedirs(preload_dir, exist_ok=True)
    os.makedirs(lazy_data_dir, exist_ok=True)

    # Track expected relative paths so deleted files are cleaned up
    expected_preload = set()
    expected_lazy = set()

    for root, _dirs, files in os.walk(data_dir):
        rel_root = os.path.relpath(root, data_dir)
        for name in files:
            if name == ".DS_Store":
                continue
            src = os.path.join(root, name)
            rel_path = name if rel_root == "." else os.path.join(rel_root, name)
            norm_rel = rel_path.replace(os.sep, "/")

            if norm_rel.startswith("Terrain/"):
                dst = os.path.join(lazy_data_dir, rel_path)
                copy_if_newer(src, dst)
                expected_lazy.add(os.path.normpath(dst))
            elif norm_rel.startswith("Models/") and name in LAZY_TRACK_MODELS:
                dst = os.path.join(lazy_data_dir, rel_path)
                copy_if_newer(src, dst)
                expected_lazy.add(os.path.normpath(dst))
            elif norm_rel.startswith("Audio/") and "/" not in norm_rel[len("Audio/"):] and name.endswith(".aiff"):
                ogg_rel = rel_path[:-5] + ".ogg"
                target_root = preload_dir if name in PRELOAD_SONGS else lazy_data_dir
                dst_ogg = os.path.join(target_root, ogg_rel)
                if transcode_aiff_to_ogg(src, dst_ogg):
                    if name in PRELOAD_SONGS:
                        expected_preload.add(os.path.normpath(dst_ogg))
                    else:
                        expected_lazy.add(os.path.normpath(dst_ogg))
                else:
                    # Fallback if no encoder available: copy .aiff into preload so game still boots
                    dst_aiff = os.path.join(preload_dir, rel_path)
                    copy_if_newer(src, dst_aiff)
                    expected_preload.add(os.path.normpath(dst_aiff))
            else:
                dst = os.path.join(preload_dir, rel_path)
                copy_if_newer(src, dst)
                expected_preload.add(os.path.normpath(dst))

    for root_dir, expected in ((preload_dir, expected_preload), (lazy_data_dir, expected_lazy)):
        for cur, _dirs, files in os.walk(root_dir):
            for name in files:
                p = os.path.normpath(os.path.join(cur, name))
                if p not in expected:
                    os.remove(p)


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare web assets for Emscripten build")
    parser.add_argument("--data-dir", required=True, help="Path to source Data/ directory")
    parser.add_argument("--out-dir", required=True, help="Output staging directory")
    args = parser.parse_args()

    stage_assets(os.path.abspath(args.data_dir), os.path.abspath(args.out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
