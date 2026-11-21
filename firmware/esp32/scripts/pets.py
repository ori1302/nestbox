# Fills data/, the LittleFS image `pio run -t uploadfs` flashes, with the eggs named in custom_pets (platformio.ini)
# or NESTBOX_PETS, e.g.  NESTBOX_PETS="nibble kitchen-cat" pio run -t uploadfs
# They come from NESTBOX_EGGS (a folder of personal eggs) if set, else from ../../pets and ../../pets/retro. data/
# mirrors the list: eggs not on it are removed.
#
# An egg whose art may not be redistributed is left out unless NESTBOX_INCLUDE_PRIVATE=1: one marked "private" in
# its description (pet.json "private": true), or one with no `license`.
import os
import shutil
import sys

Import("env")  # noqa: F821 (PlatformIO)

sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "scripts"))  # noqa: F821
from project import default_pets, egg_description  # noqa: E402

EGG = ".egg"
INCLUDE_PRIVATE = os.environ.get("NESTBOX_INCLUDE_PRIVATE") == "1"


def why_private(desc):
    """Why an egg's art may not be shared, or None if it may."""
    if desc.get("private") is True:
        return "it is marked private"
    if not desc.get("license"):
        return "it has no license"
    return None


def egg_sources(project):
    pets = os.path.normpath(os.path.join(project, "..", "..", "pets"))
    sources = [pets, os.path.join(pets, "retro")]
    if os.environ.get("NESTBOX_EGGS"):
        sources.insert(0, os.path.expanduser(os.environ["NESTBOX_EGGS"]))
    return sources


def copy_if_changed(src, dst):
    if (os.path.isfile(dst) and os.path.getmtime(src) <= os.path.getmtime(dst)
            and os.path.getsize(src) == os.path.getsize(dst)):
        return
    shutil.copy2(src, dst)
    print("nestbox: data/%s from %s" % (os.path.basename(dst), os.path.dirname(src)))


def fill_data(data, sources, names):
    """Copies the wanted eggs into `data`; returns the names copied."""
    kept = []
    for name in names:
        found = [os.path.join(d, name + EGG) for d in sources if os.path.isfile(os.path.join(d, name + EGG))]
        if not found:
            print("nestbox: no %s%s in %s" % (name, EGG, " or ".join(sources)))
            continue
        reason = why_private(egg_description(found[0]))
        if reason and not INCLUDE_PRIVATE:
            print("nestbox: left out %s%s: %s. For your own unit only: NESTBOX_INCLUDE_PRIVATE=1" % (name, EGG, reason))
            continue
        copy_if_changed(found[0], os.path.join(data, name + EGG))
        kept.append(name)
    return kept


def remove_others(data, kept):
    for f in os.listdir(data):
        if f.endswith(EGG) and f[:-len(EGG)] not in kept:
            os.remove(os.path.join(data, f))
            print("nestbox: removed data/%s (not in custom_pets / NESTBOX_PETS)" % f)


data_dir = env.subst("$PROJECT_DATA_DIR")  # noqa: F821
wanted = os.environ.get("NESTBOX_PETS") or " ".join(default_pets())
os.makedirs(data_dir, exist_ok=True)
copied = fill_data(data_dir, egg_sources(env.subst("$PROJECT_DIR")), wanted.replace(",", " ").split())  # noqa: F821
remove_others(data_dir, copied)
