"""A test pet with moving scene parts: Nibble's folder plus four small ambient parts drawn here (a waterfall by
day and at night, lights and fireflies only at night, and "pop", shown once on cue over a filled box), packed for
egg_test.
python3 ambient_pet.py PETS_DIR OUT.egg"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'eggpack'))
from png import write_png  # noqa: E402

pets, out = sys.argv[1], sys.argv[2]
work = tempfile.mkdtemp()
folder = os.path.join(work, 'ambient-nibble')
shutil.copytree(os.path.join(pets, 'nibble'), folder)


def strip(name, w, h, frames, colour_at):
    """An RGBA PNG of frames side by side, each w x h; colour_at(frame, x, y) gives (r, g, b, a)."""
    write_png(os.path.join(folder, name), w * frames, h, lambda x, y: colour_at(x // w, x % w, y))


CLEAR = (0, 0, 0, 0)
strip('fall.png', 6, 10, 8, lambda f, x, y: (200, 230, 250, 255) if (y + f) % 4 == 0 else CLEAR)
strip('fall_night.png', 6, 10, 8, lambda f, x, y: (90, 110, 160, 255) if (y + f) % 4 == 0 else CLEAR)
strip('lights_night.png', 4, 2, 2, lambda f, x, y: (250, 210, 120, 255) if (x + f) % 2 == 0 else CLEAR)
strip('flies_night.png', 3, 3, 3, lambda f, x, y: (220, 250, 140, 255) if x == f and y == 1 else CLEAR)
strip('pop.png', 4, 4, 3, lambda f, x, y: (240, 80, 80, 255) if x <= f and y <= f else CLEAR)
meta_path = os.path.join(folder, 'pet.json')
meta = json.load(open(meta_path))
meta['scene']['ambient'] = [
    {'at': [20, 20], 'frames': 8, 'durations': 10, 'day': 'fall.png', 'night': 'fall_night.png'},
    {'at': [40, 40], 'frames': 2, 'durations': 30, 'night': 'lights_night.png'},
    {'at': [60, 60], 'frames': 3, 'durations': [20, 20, 40], 'night': 'flies_night.png'},
    {'name': 'pop', 'cue': 'once', 'during': 'type_fast', 'fill': [20, 30, 120], 'at': [80, 80], 'frames': 3,
     'durations': 6, 'day': 'pop.png'}]
meta.pop('free_time', None)                   # Nibble's pastimes show its own parts, which these replace
json.dump(meta, open(meta_path, 'w'))
packer = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'eggpack', 'eggpack.py')
r = subprocess.run([sys.executable, '-I', packer, folder, '-o', out, '--name', 'Ambient'],
                   capture_output=True, text=True)
shutil.rmtree(work)
if r.returncode:
    sys.exit(r.stdout + r.stderr)
