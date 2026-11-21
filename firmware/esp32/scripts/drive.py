# The pet's read-only USB drive (NESTBOX): the files in drive/ plus build/NestBox.zip, the Mac app made by
# scripts/package_app.sh. Built into a FAT image for the `drive` partition and flashed with every `upload`.
import os
import sys

Import("env")  # noqa: F821 (PlatformIO)

project = env.subst("$PROJECT_DIR")  # noqa: F821
sys.path.insert(0, os.path.join(project, "scripts"))
import fatimage  # noqa: E402
from project import partitions  # noqa: E402

APP_NAME = "NestBox.zip"
MISSING_APP_NOTE = (b"This pet was flashed before the Mac app was packaged.\n"
                    b"Build it with scripts/package_app.sh, then flash again (pio run -t upload).\n")


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def drive_files():
    folder = os.path.join(project, "drive")
    files = [(name, read_bytes(os.path.join(folder, name)))
             for name in sorted(os.listdir(folder)) if not name.startswith(".")]
    app = os.path.normpath(os.path.join(project, "..", "..", "build", APP_NAME))
    if os.path.isfile(app):
        files.insert(0, (APP_NAME, read_bytes(app)))
    else:
        print("nestbox: no build/%s yet (scripts/package_app.sh) - the drive gets a note instead" % APP_NAME)
        files.insert(0, (APP_NAME + " is missing.txt", MISSING_APP_NOTE))
    return files


offset, _, size = partitions()["drive"]
files = drive_files()
image = os.path.join(env.subst("$BUILD_DIR"), "drive.img")  # noqa: F821
os.makedirs(os.path.dirname(image), exist_ok=True)
data = fatimage.build(size, "NESTBOX", files)
if not os.path.isfile(image) or read_bytes(image) != data:
    with open(image, "wb") as f:
        f.write(data)
    print("nestbox: drive image with %s" % ", ".join(n for n, _ in files))

# `upload` writes the drive too; `uploadfs` writes only the eggs.
if "uploadfs" not in COMMAND_LINE_TARGETS:  # noqa: F821
    env.Append(UPLOADERFLAGS=[offset, image])  # noqa: F821
