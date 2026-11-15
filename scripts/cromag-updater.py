#!/usr/bin/env python3
"""Auto-updater and pre-launch wrapper for Cro-Mag Rally GitHub Releases."""

from __future__ import annotations

import argparse
import contextlib
import fcntl
import hashlib
import json
import os
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

DEFAULT_REPO = "Mirmirmi/CroMagRally"
APP_NAME_MAC = "Cro-Mag Rally.app"
PROCESS_NAME = "CroMagRally"
USER_AGENT = "CroMagRally-Updater/1.0"


def log(msg: str) -> None:
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{ts}] {msg}", file=sys.stderr, flush=True)


def parse_version(ver: str) -> tuple[int, ...]:
    cleaned = ver.strip().lstrip("vV")
    parts = re.findall(r"\d+", cleaned)
    if not parts:
        return (0,)
    return tuple(int(p) for p in parts)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def http_get_json(url: str, timeout: float) -> dict[str, Any]:
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "application/vnd.github+json, application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def download_file(url: str, dest: Path, timeout: float, on_progress=None) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    h = hashlib.sha256()
    with urllib.request.urlopen(req, timeout=timeout) as resp, dest.open("wb") as out:
        total_header = resp.headers.get("Content-Length")
        total_bytes = int(total_header) if total_header and total_header.isdigit() else 0
        downloaded = 0
        while True:
            chunk = resp.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
            out.write(chunk)
            downloaded += len(chunk)
            if on_progress:
                on_progress(downloaded, total_bytes)
    return h.hexdigest()


def detect_platform_key() -> tuple[str, str]:
    system = platform.system()
    machine = platform.machine().lower()
    if system == "Darwin":
        return "mac", "-mac.dmg"
    if system == "Linux":
        if machine in ("aarch64", "arm64"):
            return "linux_aarch64", "-linux-aarch64.AppImage"
        if machine in ("x86_64", "amd64"):
            return "linux_x86_64", "-linux-x86_64.AppImage"
        raise RuntimeError(f"Unsupported Linux architecture: {machine}")
    raise RuntimeError(f"Unsupported operating system: {system}")


def default_install_path() -> Path:
    if platform.system() == "Darwin":
        return Path.home() / "Library" / "Application Support" / "CroMagRally" / APP_NAME_MAC
    return Path.home() / ".local" / "share" / "CroMagRally" / "CroMagRally.AppImage"


def default_state_dir() -> Path:
    if platform.system() == "Darwin":
        return Path.home() / "Library" / "Application Support" / "CroMagRally" / "updater"
    xdg_state = os.environ.get("XDG_STATE_HOME")
    base = Path(xdg_state) if xdg_state else Path.home() / ".local" / "state"
    return base / "cromagrally"


def fetch_latest_release(repo: str, timeout: float) -> dict[str, Any]:
    asset_key, suffix = detect_platform_key()
    manifest_url = f"https://github.com/{repo}/releases/latest/download/latest.json"
    try:
        manifest = http_get_json(manifest_url, timeout=timeout)
        asset = manifest.get("assets", {}).get(asset_key)
        if asset and asset.get("name"):
            tag = manifest["tag"]
            name = asset["name"]
            return {
                "version": manifest["version"].lstrip("vV"),
                "tag": tag,
                "asset_name": name,
                "download_url": f"https://github.com/{repo}/releases/download/{tag}/{name}",
                "sha256": asset.get("sha256"),
                "source": "latest.json",
            }
    except (urllib.error.HTTPError, urllib.error.URLError, KeyError, json.JSONDecodeError):
        pass

    api_url = f"https://api.github.com/repos/{repo}/releases/latest"
    release = http_get_json(api_url, timeout=timeout)
    tag = release["tag_name"]
    for asset in release.get("assets", []):
        name = asset.get("name", "")
        if name.endswith(suffix):
            digest = asset.get("digest") or ""
            sha256 = digest.removeprefix("sha256:") if digest.startswith("sha256:") else None
            return {
                "version": tag.lstrip("vV"),
                "tag": tag,
                "asset_name": name,
                "download_url": asset["browser_download_url"],
                "sha256": sha256,
                "source": "github-api",
            }
    raise RuntimeError(f"No release asset matching *{suffix} found in {repo} {tag}")


def read_local_state(install_path: Path, state_file: Path) -> dict[str, Any]:
    state: dict[str, Any] = {}
    if state_file.exists():
        with contextlib.suppress(Exception):
            state = json.loads(state_file.read_text(encoding="utf-8"))

    if platform.system() == "Darwin":
        plist_path = install_path / "Contents" / "Info.plist"
        if plist_path.exists():
            with contextlib.suppress(Exception), plist_path.open("rb") as f:
                plist = plistlib.load(f)
                ver = plist.get("CFBundleShortVersionString")
                if ver:
                    state.setdefault("version", str(ver))
    else:
        if install_path.exists() and not state.get("sha256"):
            with contextlib.suppress(Exception):
                state["sha256"] = sha256_file(install_path)

    return state


def write_local_state(state_file: Path, release: dict[str, Any], sha256: str) -> None:
    state_file.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": release["version"],
        "tag": release["tag"],
        "asset_name": release["asset_name"],
        "sha256": sha256,
        "installed_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    tmp = state_file.with_suffix(".tmp")
    tmp.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, state_file)


def is_game_running() -> bool:
    res = subprocess.run(["pgrep", "-x", PROCESS_NAME], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return res.returncode == 0


def needs_update(
    install_path: Path,
    local_state: dict[str, Any],
    remote: dict[str, Any],
    force: bool = False,
) -> tuple[bool, str]:
    if not install_path.exists():
        return True, "not installed"
    if force:
        return True, "forced"

    local_sha = local_state.get("sha256")
    remote_sha = remote.get("sha256")
    if local_sha and remote_sha and local_sha == remote_sha:
        return False, "matching sha256"

    local_ver_str = local_state.get("version")
    if not local_ver_str:
        # Existing unmanaged install whose checksum doesn't match latest release.
        return True, "untracked local version"

    local_ver = parse_version(local_ver_str)
    remote_ver = parse_version(remote["version"])
    if remote_ver > local_ver:
        return True, f"{local_ver_str} -> {remote['version']}"
    if remote_ver == local_ver and local_sha and remote_sha and local_sha != remote_sha:
        return True, f"checksum changed for {remote['version']}"
    return False, f"up to date ({local_ver_str})"


@contextlib.contextmanager
def progress_ui(enabled: bool, tag: str):
    if not enabled:
        yield None
        return

    system = platform.system()
    if system == "Linux" and (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        zenity = shutil.which("zenity")
        if zenity:
            proc = subprocess.Popen(
                [
                    zenity,
                    "--progress",
                    "--percentage=0",
                    "--auto-close",
                    "--no-cancel",
                    "--title=Cro-Mag Rally",
                    f"--text=Downloading Cro-Mag Rally {tag}...",
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            last_pct = -1

            def update_progress(downloaded: int, total: int) -> None:
                nonlocal last_pct
                if not proc.stdin or proc.poll() is not None:
                    return
                if total > 0:
                    pct = min(99, int(downloaded * 99 / total))
                    text = f"# Downloading Cro-Mag Rally {tag} ({downloaded // (1024 * 1024)} / {total // (1024 * 1024)} MB)..."
                else:
                    pct = 0
                    text = f"# Downloading Cro-Mag Rally {tag} ({downloaded // (1024 * 1024)} MB)..."
                if pct != last_pct or total == 0:
                    last_pct = pct
                    with contextlib.suppress(Exception):
                        proc.stdin.write(f"{pct}\n{text}\n".encode("utf-8"))
                        proc.stdin.flush()

            with contextlib.suppress(Exception):
                if proc.stdin:
                    proc.stdin.write(f"0\n# Downloading Cro-Mag Rally {tag}...\n".encode("utf-8"))
                    proc.stdin.flush()
            try:
                yield update_progress
            finally:
                with contextlib.suppress(Exception):
                    if proc.stdin:
                        proc.stdin.write(b"100\n")
                        proc.stdin.flush()
                        proc.stdin.close()
                    proc.wait(timeout=2)
            return

    if system == "Darwin":
        with contextlib.suppress(Exception):
            subprocess.run(
                [
                    "osascript",
                    "-e",
                    f'display notification "Downloading {tag}..." with title "Cro-Mag Rally"',
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
    yield None


def install_macos(dmg_path: Path, install_path: Path) -> None:
    install_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cromag-mnt-") as mnt_dir:
        subprocess.run(
            ["hdiutil", "attach", "-nobrowse", "-readonly", "-mountpoint", mnt_dir, str(dmg_path)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        try:
            src_app = Path(mnt_dir) / APP_NAME_MAC
            if not src_app.exists():
                candidates = list(Path(mnt_dir).glob("*.app"))
                if not candidates:
                    raise RuntimeError(f"No .app bundle found inside {dmg_path.name}")
                src_app = candidates[0]

            staging = install_path.parent / f".{install_path.name}.staging"
            backup = install_path.parent / f".{install_path.name}.old"
            shutil.rmtree(staging, ignore_errors=True)
            shutil.rmtree(backup, ignore_errors=True)

            subprocess.run(["ditto", str(src_app), str(staging)], check=True)
            subprocess.run(
                ["xattr", "-dr", "com.apple.quarantine", str(staging)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )

            try:
                if install_path.exists():
                    os.rename(install_path, backup)
                os.rename(staging, install_path)
            except Exception:
                if not install_path.exists() and backup.exists():
                    os.rename(backup, install_path)
                shutil.rmtree(staging, ignore_errors=True)
                raise
            finally:
                shutil.rmtree(backup, ignore_errors=True)
        finally:
            subprocess.run(
                ["hdiutil", "detach", mnt_dir, "-quiet"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )


def install_linux(appimage_path: Path, install_path: Path) -> None:
    install_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_dest = install_path.parent / f".{install_path.name}.tmp"
    with contextlib.suppress(FileNotFoundError):
        tmp_dest.unlink()
    shutil.copy2(appimage_path, tmp_dest)
    os.chmod(tmp_dest, 0o755)
    os.replace(tmp_dest, install_path)


def perform_update(
    repo: str,
    install_path: Path,
    state_dir: Path,
    timeout: float,
    force: bool = False,
    gui: bool = False,
) -> bool:
    state_file = state_dir / "installed.json"
    local_state = read_local_state(install_path, state_file)
    remote = fetch_latest_release(repo, timeout=timeout)
    should_update, reason = needs_update(install_path, local_state, remote, force=force)

    if not should_update:
        log(f"No update needed ({reason}).")
        # Persist checksum for pre-existing unmanaged installs once verified.
        if install_path.exists() and not state_file.exists() and remote.get("sha256"):
            write_local_state(state_file, remote, remote["sha256"])
        return False

    if is_game_running():
        log(f"Update {remote['tag']} available ({reason}), but {PROCESS_NAME} is running; deferring.")
        return False

    log(f"Updating Cro-Mag Rally ({reason}) from {remote['download_url']}")
    with progress_ui(gui, remote["tag"]) as on_progress:
        with tempfile.TemporaryDirectory(prefix="cromag-dl-") as tmp_dir:
            dl_path = Path(tmp_dir) / remote["asset_name"]
            actual_sha = download_file(
                remote["download_url"],
                dl_path,
                timeout=max(timeout, 120.0),
                on_progress=on_progress,
            )
            expected_sha = remote.get("sha256")
            if expected_sha and actual_sha != expected_sha:
                raise RuntimeError(
                    f"SHA-256 mismatch for {remote['asset_name']}: expected {expected_sha}, got {actual_sha}"
                )

            if is_game_running():
                log(f"{PROCESS_NAME} started during download; deferring install of {remote['tag']}.")
                return False

            if platform.system() == "Darwin":
                install_macos(dl_path, install_path)
            else:
                install_linux(dl_path, install_path)

            write_local_state(state_file, remote, actual_sha)

    log(f"Successfully installed Cro-Mag Rally {remote['tag']} to {install_path}")
    if gui and platform.system() == "Darwin":
        with contextlib.suppress(Exception):
            subprocess.run(
                [
                    "osascript",
                    "-e",
                    f'display notification "Updated to {remote["tag"]}" with title "Cro-Mag Rally"',
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
    return True


def launch_game(install_path: Path, extra_args: list[str]) -> int:
    if not install_path.exists():
        log(f"Error: {install_path} does not exist.")
        return 1

    if platform.system() == "Darwin":
        cmd = ["open", str(install_path)]
        if extra_args:
            cmd += ["--args"] + extra_args
        return subprocess.run(cmd).returncode

    os.execv(str(install_path), [str(install_path)] + extra_args)
    return 0


@contextlib.contextmanager
def file_lock(lock_path: Path):
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open("w") as f:
        fcntl.flock(f.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(f.fileno(), fcntl.LOCK_UN)


def main() -> int:
    parser = argparse.ArgumentParser(description="Cro-Mag Rally auto-updater and launcher")
    parser.add_argument(
        "action",
        nargs="?",
        default="update",
        choices=("check", "update", "launch"),
        help="check status, update in place, or update-then-launch",
    )
    parser.add_argument("--repo", default=DEFAULT_REPO, help=f"GitHub repo (default: {DEFAULT_REPO})")
    parser.add_argument(
        "--install-path",
        type=Path,
        default=default_install_path(),
        help="Where Cro-Mag Rally is installed",
    )
    parser.add_argument(
        "--state-dir",
        type=Path,
        default=default_state_dir(),
        help="Directory for updater state and lock files",
    )
    parser.add_argument("--timeout", type=float, default=15.0, help="HTTP metadata timeout in seconds")
    parser.add_argument("--force", action="store_true", help="Reinstall even if version matches")
    parser.add_argument("--gui", action="store_true", help="Show GUI notification/progress when updating")
    parser.add_argument("game_args", nargs=argparse.REMAINDER, help="Arguments passed to game on launch")
    args = parser.parse_args()

    install_path: Path = args.install_path.expanduser()
    state_dir: Path = args.state_dir.expanduser()
    lock_path = state_dir / "updater.lock"

    if args.action == "check":
        local_state = read_local_state(install_path, state_dir / "installed.json")
        remote = fetch_latest_release(args.repo, timeout=args.timeout)
        should_update, reason = needs_update(install_path, local_state, remote, force=args.force)
        print(
            json.dumps(
                {
                    "installed": install_path.exists(),
                    "install_path": str(install_path),
                    "local": local_state,
                    "remote": remote,
                    "update_available": should_update,
                    "reason": reason,
                },
                indent=2,
            )
        )
        return 0

    if args.action == "update":
        with file_lock(lock_path):
            perform_update(
                repo=args.repo,
                install_path=install_path,
                state_dir=state_dir,
                timeout=args.timeout,
                force=args.force,
                gui=args.gui,
            )
        return 0

    # launch mode: update best-effort (never block offline play if already installed)
    if is_game_running():
        if platform.system() == "Darwin":
            return launch_game(install_path, args.game_args)
        log(f"{PROCESS_NAME} is already running; not launching a duplicate instance.")
        return 0

    with file_lock(lock_path):
        try:
            perform_update(
                repo=args.repo,
                install_path=install_path,
                state_dir=state_dir,
                timeout=min(args.timeout, 8.0),
                force=args.force,
                gui=args.gui,
            )
        except Exception as exc:
            log(f"Pre-launch update check failed ({exc}); launching existing install.")

    if is_game_running() and platform.system() != "Darwin":
        log(f"{PROCESS_NAME} started while updating; not launching a duplicate instance.")
        return 0

    return launch_game(install_path, args.game_args)


if __name__ == "__main__":
    sys.exit(main())
