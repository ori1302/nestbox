"""A small FAT12 image with long file names: the pet's read-only USB drive (NESTBOX).

No partition table ("superfloppy"), which macOS, Windows and Linux all mount. Files sit in the root folder,
each in one contiguous run of clusters. Deterministic: same files, same image.

    python3 fatimage.py OUT.img SIZE_BYTES LABEL FILE...     (for testing outside PlatformIO)
"""
import os
import struct
import sys

SECTOR = 512
ROOT_ENTRIES = 64
DATE = ((2026 - 1980) << 9) | (11 << 5) | 5          # 2026-11-05: a fixed date keeps the image reproducible
TIME = 12 << 11
MAX_FAT12_CLUSTERS = 4084
VOLUME_SERIAL = 0x584F424E                         # "NBOX"
ATTR_VOLUME_LABEL, ATTR_ARCHIVE, ATTR_LONG_NAME = 0x08, 0x20, 0x0F
MEDIA_FIXED = 0xF8
DIR_ENTRY = 32
LFN_CHARS = 13                                       # UTF-16 characters per long-name entry
END_OF_CHAIN = 0xFFF
JUMP_BOOT = b"\xEB\x3C\x90"
SECTORS_PER_TRACK, HEADS = 32, 64                   # CHS geometry for old readers; unused by modern ones
DRIVE_NUMBER, BOOT_SIGNATURE = 0x80, 0x29


def _clean(s):
    return "".join(c for c in s.upper() if c.isalnum() or c in "_-")


def _dotted(base, ext):
    return base + "." + ext if ext else base


def _short_names(names):
    """8.3 names for `names`: as they are if they already fit, else BASE~N."""
    used, out = set(), []
    for name in names:
        base, ext = os.path.splitext(name)
        short_base, short_ext = _clean(base), _clean(ext[1:])[:3]
        fits = short_base == base and short_ext == ext[1:] and len(short_base) <= 8
        if not fits or _dotted(short_base, short_ext) in used:
            n = 1
            while _dotted(short_base[:8 - len("~%d" % n)] + "~%d" % n, short_ext) in used:
                n += 1
            short_base = short_base[:8 - len("~%d" % n)] + "~%d" % n
        used.add(_dotted(short_base, short_ext))
        out.append((short_base.ljust(8) + short_ext.ljust(3)).encode("ascii"))
    return out


def _checksum(short11):
    s = 0
    for b in short11:
        s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
    return s


def _lfn_entries(name, short11):
    chars = [ord(c) for c in name] + [0]
    chars += [0xFFFF] * (-len(chars) % LFN_CHARS)
    parts = [chars[i:i + LFN_CHARS] for i in range(0, len(chars), LFN_CHARS)]
    out = []
    for seq in range(len(parts), 0, -1):                  # stored last part first
        p = parts[seq - 1]
        e = bytes([seq | (0x40 if seq == len(parts) else 0)])
        e += struct.pack("<5H", *p[0:5]) + bytes([ATTR_LONG_NAME, 0, _checksum(short11)])
        e += struct.pack("<6H", *p[5:11]) + b"\0\0" + struct.pack("<2H", *p[11:13])
        out.append(e)
    return b"".join(out)


def _entry(name11, attr, cluster, size):
    return struct.pack("<11sBBBHHHHHHHI", name11, attr, 0, 0, TIME, DATE, DATE, 0, TIME, DATE, cluster, size)


def _layout(total_sectors):
    """(sectors per cluster, sectors per FAT, clusters) for a FAT12 volume of `total_sectors`."""
    root_sectors = ROOT_ENTRIES * DIR_ENTRY // SECTOR
    per_cluster = 1
    while True:
        fat_sectors = 1
        while True:
            clusters = (total_sectors - 1 - root_sectors - 2 * fat_sectors) // per_cluster
            needed = -(-((clusters + 2) * 3 // 2 + 1) // SECTOR)
            if needed <= fat_sectors:
                break
            fat_sectors = needed
        if clusters <= MAX_FAT12_CLUSTERS:
            return per_cluster, fat_sectors, clusters
        per_cluster *= 2


def _fat12(chain, fat_sectors):
    """The FAT as bytes: 12-bit entries, two to every three bytes."""
    out = bytearray(fat_sectors * SECTOR)
    for cluster, value in chain.items():
        at = cluster * 3 // 2
        if cluster & 1:
            out[at] = (out[at] & 0x0F) | ((value << 4) & 0xF0)
            out[at + 1] = (value >> 4) & 0xFF
        else:
            out[at] = value & 0xFF
            out[at + 1] = (out[at + 1] & 0xF0) | ((value >> 8) & 0x0F)
    return out


def _boot_sector(total_sectors, per_cluster, fat_sectors, label11):
    boot = bytearray(SECTOR)
    boot[0:3] = JUMP_BOOT
    boot[3:11] = b"MSDOS5.0"
    small = total_sectors < 65536
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, per_cluster, 1, 2, ROOT_ENTRIES,
                     total_sectors if small else 0, MEDIA_FIXED, fat_sectors, SECTORS_PER_TRACK, HEADS, 0,
                     0 if small else total_sectors)
    struct.pack_into("<BBBI11s8s", boot, 36, DRIVE_NUMBER, 0, BOOT_SIGNATURE, VOLUME_SERIAL, label11, b"FAT12   ")
    boot[510:512] = b"\x55\xAA"
    return boot


def build(size, label, files):
    """files: [(name, bytes)]. Returns the image (bytes). Raises ValueError if they don't fit."""
    total = size // SECTOR
    per_cluster, fat_sectors, clusters = _layout(total)
    cluster_bytes = per_cluster * SECTOR
    root_start = (1 + 2 * fat_sectors) * SECTOR
    data_start = root_start + ROOT_ENTRIES * DIR_ENTRY

    chain = {0: 0xFF8, 1: END_OF_CHAIN}
    label11 = label.upper().ljust(11)[:11].encode("ascii")
    root = _entry(label11, ATTR_VOLUME_LABEL, 0, 0)
    data = bytearray()
    next_cluster = 2
    for (name, content), short in zip(files, _short_names([n for n, _ in files])):
        count = -(-len(content) // cluster_bytes)
        first = next_cluster if count else 0
        for i in range(count):
            chain[next_cluster + i] = next_cluster + i + 1 if i < count - 1 else END_OF_CHAIN
        next_cluster += count
        data += content + b"\0" * (count * cluster_bytes - len(content))
        if name != _dotted(short[:8].decode().rstrip(), short[8:].decode().rstrip()):   # not plain 8.3
            root += _lfn_entries(name, short)
        root += _entry(short, ATTR_ARCHIVE, first, len(content))
    if next_cluster - 2 > clusters:
        raise ValueError("drive files need %d KB, the drive holds %d KB"
                         % ((next_cluster - 2) * cluster_bytes // 1024, clusters * cluster_bytes // 1024))
    if len(root) > ROOT_ENTRIES * DIR_ENTRY:
        raise ValueError("too many files for the root folder")

    fat = _fat12(chain, fat_sectors)
    img = bytearray(size)
    img[0:SECTOR] = _boot_sector(total, per_cluster, fat_sectors, label11)
    for i in range(2):
        at = (1 + i * fat_sectors) * SECTOR
        img[at:at + len(fat)] = fat
    img[root_start:root_start + len(root)] = root
    img[data_start:data_start + len(data)] = data
    return bytes(img)


def _read(path):
    with open(path, "rb") as f:
        return f.read()


if __name__ == "__main__":
    out, size, label, paths = sys.argv[1], int(sys.argv[2], 0), sys.argv[3], sys.argv[4:]
    image = build(size, label, [(os.path.basename(p), _read(p)) for p in paths])
    with open(out, "wb") as f:
        f.write(image)
