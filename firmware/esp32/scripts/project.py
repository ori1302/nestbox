# What the PlatformIO scripts and release.py read from the project: the version, the partition table, the pets
# list and the eggs' descriptions.
import json
import os
import re

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EGG_HEADER_BYTES = 8                     # "NEGG", then the description's length as a little-endian u32


def version():
    """(major, minor, patch) from project() in ../../CMakeLists.txt, the one place the version is set."""
    cmake = os.path.join(PROJECT, "..", "..", "CMakeLists.txt")
    with open(cmake) as f:
        match = re.search(r"project\(\s*nestbox\s+VERSION\s+(\d+)\.(\d+)\.(\d+)", f.read())
    if not match:
        raise ValueError("no project(nestbox VERSION x.y.z) in " + cmake)
    return match.groups()


def partitions():
    """{name: (offset as written, offset, size)} from partitions.csv."""
    out = {}
    with open(os.path.join(PROJECT, "partitions.csv")) as f:
        for line in f:
            cols = [c.strip() for c in line.split("#")[0].split(",")]
            if len(cols) >= 5 and cols[0]:
                out[cols[0]] = (cols[3], int(cols[3], 0), int(cols[4], 0))
    return out


def default_pets():
    """The pets in custom_pets (platformio.ini)."""
    with open(os.path.join(PROJECT, "platformio.ini")) as f:
        match = re.search(r"^custom_pets\s*=\s*(.+)$", f.read(), re.M)
    return match.group(1).split(";")[0].replace(",", " ").split() if match else []


def egg_description(path):
    """The egg's JSON description ({} if unreadable); the layout is in https://nestbox.pet/docs/egg-format/."""
    try:
        with open(path, "rb") as f:
            head = f.read(EGG_HEADER_BYTES)
            return json.loads(f.read(int.from_bytes(head[4:8], "little")))
    except (OSError, ValueError):
        return {}
