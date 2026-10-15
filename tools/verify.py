#!/usr/bin/env python3
"""Verify retained image bytes, optional public URLs, and live PR embeds."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from urllib.request import urlopen
import zlib


ROOT = Path(__file__).resolve().parents[1]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify_png(data, asset):
    name = asset["path"]
    require(len(data) == asset["bytes"], f"{name}: size mismatch")
    require(hashlib.sha256(data).hexdigest() == asset["sha256"], f"{name}: SHA-256 mismatch")
    require(data.startswith(PNG_SIGNATURE), f"{name}: invalid PNG signature")
    offset = len(PNG_SIGNATURE)
    kinds = []
    while offset < len(data):
        require(offset + 12 <= len(data), f"{name}: truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        end = offset + 8 + size
        require(end + 4 <= len(data), f"{name}: truncated PNG payload")
        kind = data[offset + 4:offset + 8]
        expected_crc = struct.unpack_from(">I", data, end)[0]
        require(zlib.crc32(data[offset + 4:end]) == expected_crc, f"{name}: PNG CRC mismatch")
        if not kinds:
            require(kind == b"IHDR" and size == 13, f"{name}: invalid PNG header")
            dimensions = struct.unpack_from(">II", data, offset + 8)
            require(dimensions == (asset["width"], asset["height"]), f"{name}: dimensions mismatch")
        kinds.append(kind)
        offset = end + 4
        if kind == b"IEND":
            require(size == 0 and offset == len(data), f"{name}: invalid PNG ending")
            break
    require(kinds[-1:] == [b"IEND"] and b"IDAT" in kinds, f"{name}: incomplete PNG")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--online", action="store_true", help="verify remote image bytes and live PR bodies")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "manifest.json").read_text())
    assets = manifest["assets"]
    require(manifest["schema_version"] == 1 and assets, "unsupported or empty manifest")
    paths = [a["path"] for a in assets]
    require(len(paths) == len(set(paths)), "duplicate image paths")
    actual = {p.relative_to(ROOT).as_posix() for p in (ROOT / "screenshots").rglob("*.png")}
    require(actual == set(paths), "manifest and retained PNG inventory differ")
    sums = "".join(f"{a['sha256']}  {a['path']}\n" for a in assets)
    require((ROOT / "SHA256SUMS").read_text() == sums, "SHA256SUMS differs from manifest")
    bodies = {}
    for asset in assets:
        name = asset["path"]
        path = (ROOT / name).resolve()
        require(path.is_relative_to(ROOT), f"{name}: path leaves archive")
        url = (f"https://raw.githubusercontent.com/{manifest['repository']}/"
               f"{asset['image_commit']}/{name}")
        require(asset["url"] == url, f"{name}: URL is not pinned to original commit")
        verify_png(path.read_bytes(), asset)
        if args.online:
            with urlopen(asset["url"], timeout=30) as response:
                require(response.status == 200, f"{name}: remote status is not 200")
                require(response.headers.get_content_type() == "image/png", f"{name}: remote is not PNG")
                verify_png(response.read(asset["bytes"] + 1), asset)
            pr = asset["pr"]
            if pr not in bodies:
                bodies[pr] = subprocess.check_output(
                    ["gh", "pr", "view", pr, "--json", "body", "--jq", ".body"],
                    text=True, timeout=30)
            require(asset["url"] in bodies[pr], f"{name}: expected URL missing from PR body")
        print(f"OK {name}")
    print(f"Verified {len(assets)} images" + (", public bytes and PR links." if args.online else " offline."))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Verification failed: {error}", file=sys.stderr)
        sys.exit(1)
