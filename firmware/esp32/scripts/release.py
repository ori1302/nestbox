#!/usr/bin/env python3
"""Builds the files a browser installer writes to a board, and a manifest.json that lists them:

    python3 scripts/release.py OUT_DIR [--pets "nibble kitchen-cat"] [--eggs DIR]

It runs `pio run` (the firmware and the USB drive image) and `pio run -t buildfs` (the pets), then copies into OUT_DIR
the bootloader, the partition table, the OTA selector, the firmware, the pets image and the drive image, each with
the flash address it goes to and its SHA-256. The nvs partition (Wi-Fi, the key, the name) is not among them, so a
reinstall keeps the board's settings.

Pets come from ../../pets and ../../pets/retro, or from --eggs first (as NESTBOX_EGGS for `pio run -t uploadfs`).
Only eggs that may be shared go in: one marked "private", or with no license, stops the release (see scripts/pets.py).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

from project import PROJECT, default_pets, egg_description, partitions, version

BUILD = os.path.join(PROJECT, ".pio", "build", "nestbox")
BOOTLOADER_OFFSET = 0x0          # ESP32-S3
PARTITION_TABLE_OFFSET = 0x8000
BOOT_APP0 = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")


def pio():
    found = shutil.which("pio") or os.path.expanduser("~/.platformio/penv/bin/pio")
    if not os.path.exists(found):
        sys.exit("release: PlatformIO's `pio` is not on PATH")
    return found


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--pets", default=" ".join(default_pets()),
                    help='the pets (default: custom_pets in platformio.ini)')
    ap.add_argument("--eggs", help="a folder of eggs to take pets from first")
    args = ap.parse_args()
    pets = args.pets.replace(",", " ").split()

    env = dict(os.environ, NESTBOX_PETS=" ".join(pets))
    env.pop("NESTBOX_INCLUDE_PRIVATE", None)          # a release only ever holds pets that may be shared
    if args.eggs:
        env["NESTBOX_EGGS"] = os.path.expanduser(args.eggs)
    for target in ([], ["-t", "buildfs"]):
        r = subprocess.run([pio(), "run", "-e", "nestbox", *target], cwd=PROJECT, env=env, capture_output=True,
                           text=True)
        for line in r.stdout.splitlines():
            if line.startswith("nestbox:"):
                print(line)
        if r.returncode:
            sys.exit("release: pio run %s failed:\n%s" % (" ".join(target), r.stdout[-3000:] + r.stderr[-2000:]))

    data = os.path.join(PROJECT, "data")
    missing = [p for p in pets if not os.path.exists(os.path.join(data, p + ".egg"))]
    if missing:
        sys.exit("release: left out %s (see the lines above): give each a license, or leave it out" %
                 ", ".join(missing))

    os.makedirs(args.out, exist_ok=True)
    table = {name: offset for name, (_, offset, _) in partitions().items()}
    if not os.path.exists(BOOT_APP0):
        sys.exit("release: no boot_app0.bin in the Arduino framework package")
    parts = [
        ("bootloader", os.path.join(BUILD, "bootloader.bin"), BOOTLOADER_OFFSET),
        ("partition table", os.path.join(BUILD, "partitions.bin"), PARTITION_TABLE_OFFSET),
        ("OTA selector", BOOT_APP0, table["otadata"]),
        ("firmware", os.path.join(BUILD, "firmware.bin"), table["app0"]),
        ("pets", os.path.join(BUILD, "littlefs.bin"), table["spiffs"]),
        ("USB drive", os.path.join(BUILD, "drive.img"), table["drive"]),
    ]
    files = []
    for what, src, offset in parts:
        name = re.sub(r"[^a-z0-9]+", "-", what.lower()).strip("-") + ".bin"
        shutil.copyfile(src, os.path.join(args.out, name))
        files.append({"what": what, "file": name, "offset": offset, "size": os.path.getsize(src),
                      "sha256": sha256(src)})
    pet_list = []
    for p in pets:
        desc = egg_description(os.path.join(data, p + ".egg"))
        pet_list.append({"name": p, "title": desc.get("name", p), "license": desc.get("license", "")})
    manifest = {"name": "NestBox", "version": ".".join(version()), "chip": "ESP32-S3", "flash": "16MB",
                "pets": pet_list, "parts": files}
    with open(os.path.join(args.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    total = sum(p["size"] for p in files)
    print("release: NestBox %s with %s, %d files, %.1f MB, in %s" % (
        manifest["version"], ", ".join(p["title"] for p in pet_list), len(files), total / 1048576, args.out))


if __name__ == "__main__":
    main()
