"""eggpack: check a pet folder and pack it into one .egg the engine can load.

    python3 tools/eggpack/eggpack.py mypet --check                        what each look will play, and any errors
    python3 tools/eggpack/eggpack.py mypet -o ~/.nestbox/pets/mypet.egg   pack it (--name overrides pet.json's name)

The folder layout and pet.json are described in https://nestbox.pet/docs/pet-folder/, the egg in
https://nestbox.pet/docs/egg-format/. Which animations each look plays comes from the engine (`docgen spec` in
$NESTBOX_BUILD_DIR, default build/), so the packer and the device agree. PNG and XML are decoded here, once; the
device reads palette indices only.
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python3 -I` leaves the script's folder off sys.path
import eggformat  # noqa: E402
from eggformat import DAYPARTS as AMBIENT_PICTURES, MAX_TICKS, WORDS  # noqa: E402
from png import read_png  # noqa: E402

# Strikes that slide the pet forward between their rush and return frames, unless pet.json's "dash" lists others.
# The flag is written into each animation, so the device needs no list.
DEFAULT_DASH = ('Attack', 'Strike', 'Chop', 'Scratch', 'Punch', 'Slap', 'Slice', 'MultiScratch', 'MultiStrike',
                'Uppercut', 'Ricochet', 'Bite', 'Jab', 'Kick', 'Lick', 'Slam', 'Stomp', 'QuickStrike')
REQUIRED = ('Idle', 'Walk')
DIRECTIONS = 8
STRIKE_MARKS = ('RushFrame', 'HitFrame', 'ReturnFrame')
SCREEN_W, SCREEN_H = 280, 240
MAX_COLOURS = 255       # palette index 0 is transparent
MAX_BLOCKED = 16        # polygons inside a scene's floor the pet may not stand on
CORNERS = ('top-left', 'top-right', 'bottom-left', 'bottom-right')   # where a scene's gauges may go
PACE_RANGE = (20, 120)  # a pet's own walking speed, screen pixels per second (the device clamps to this)
OPAQUE_ALPHA = 128      # alpha at or above this is drawn
WHITE_LEVEL = 215       # every channel at or above this counts as white
SCENE_PICTURES = (('day', 'background'), ('morning', 'morning'), ('afternoon', 'afternoon'),
                  ('evening', 'evening'), ('night', 'night'))
WORKSPOT_PICTURES = ('on', 'off', 'night')
SWISH_PICTURES = ('day', 'night')
MAX_AMBIENT = 12        # moving parts of a scene
MAX_AMBIENT_FRAMES = 64
MAX_AMBIENT_PX = 256

SPEC = None


def build_dir():
    return os.environ.get('NESTBOX_BUILD_DIR') or os.path.join(HERE, '..', '..', 'build')


def load_spec():
    """The engine's looks and the animations each plays, in order, from the build's docgen."""
    docgen = os.path.join(build_dir(), 'docgen')
    try:
        return json.loads(subprocess.run([docgen, 'spec'], capture_output=True, text=True, timeout=10).stdout)
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    sys.exit(f'no engine build in {build_dir()}: build it first (cmake -S . -B build && cmake --build build); '
             'this tool asks it which animations each look plays')


def spec():
    global SPEC
    SPEC = SPEC or load_spec()
    return SPEC


def used(meta):
    """Animations the engine can use, in a fixed order, then the throw kit's; anything else in the folder is left out
    of the egg."""
    s = spec()
    names = list(s['required'])
    for look in s['looks']:
        for play in look['plays']:
            if play['anim'] not in names:
                names.append(play['anim'])
    kit = meta.get('throw') or {}
    names += [kit[k] for k in ('anim', 'item', 'catch') if kit.get(k) and kit[k] not in names]
    for name in pastime_anims(meta):
        if name not in names:
            names.append(name)
    if (meta.get('scene') or {}).get('water'):         # its pastimes swim: the swim and the jump in and out
        names += [n for n in ('Swim', 'Jump') if n not in names]
    return names


def pastime_anims(meta):
    """The animations its free-time pastimes play: a step's "do" that is not a behaviour (behaviours are lower case),
    and the animation a step swims or jumps with ("swim": "SwimBall", "jump": "JumpBall")."""
    out = []
    for p in meta.get('free_time') or []:
        for step in (p.get('steps') or []) if isinstance(p, dict) else []:
            word = step if isinstance(step, str) else step.get('do') if isinstance(step, dict) else None
            swim = step.get('swim') if isinstance(step, dict) else None
            jump = step.get('jump') if isinstance(step, dict) else None
            for w in (word, swim if isinstance(swim, str) else None, jump if isinstance(jump, str) else None):
                if isinstance(w, str) and w and w not in spec().get('words', []) and w not in out:
                    out.append(w)
    return out


def is_opaque(c):
    return c[3] >= OPAQUE_ALPHA


def is_white(c):
    return is_opaque(c) and min(c[:3]) >= WHITE_LEVEL


def whiteness(get, fw, fh, row, frame):
    """The share of a frame's opaque pixels that are white."""
    px = [get(frame * fw + x, row * fh + y) for y in range(fh) for x in range(fw)]
    px = [c for c in px if is_opaque(c)]
    return sum(1 for c in px if is_white(c)) / len(px) if px else 0


def flashes(fw, fh, rows, frames, get):
    """The white flash frames: far whiter than the animation's usual frame, as in hit frames where the whole body
    turns white for a tick. Looped, they strobe. A white pet is white in every frame, so it has none."""
    out = set()
    for r in range(rows):
        ws = [whiteness(get, fw, fh, r, f) for f in range(frames)]
        usual = sorted(ws)[len(ws) // 2]
        out |= {f for f, v in enumerate(ws) if v >= 0.5 and v >= usual + 0.3}
    return out if len(out) < frames else set()


def is_point(value):
    """An [x, y] pair of numbers."""
    return (isinstance(value, list) and len(value) == 2 and
            all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in value))


def point_in_polygon(point, polygon):
    x, y = point
    inside = False
    for i in range(len(polygon)):
        (xi, yi), (xj, yj) = polygon[i], polygon[i - 1]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
    return inside


def read_anim_data(folder):
    """{name: <Anim> element} from the folder's AnimData.xml."""
    root = ET.parse(os.path.join(folder, 'AnimData.xml')).getroot()
    return {a.findtext('Name'): a for a in root.iter('Anim')}


def read_meta(folder):
    path = os.path.join(folder, 'pet.json')
    if not os.path.exists(path):
        return {}
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def durations(anim):
    return [int(d.text) for d in anim.iter('Duration')]


def strike_mark(anim, tag):
    """A strike's frame marker, or -1 without one."""
    value = anim.findtext(tag)
    return int(value) if value is not None else -1


def sheet_path(folder, source):
    return os.path.join(folder, f'{source}-Anim.png')


def check_anim(folder, name, anims, errors, warns):
    """Checks one animation and its sheet; returns its row count, or None if it can't be used."""
    source = anims[name].findtext('CopyOf') or name
    if source not in anims:
        errors.append(f'{name}: CopyOf {source}, which is not in AnimData.xml')
        return None
    anim = anims[source]
    try:
        fw, fh = int(anim.findtext('FrameWidth')), int(anim.findtext('FrameHeight'))
    except (TypeError, ValueError):
        errors.append(f'{source}: FrameWidth/FrameHeight missing')
        return None
    durs = durations(anim)
    if not durs or min(durs) <= 0:
        errors.append(f'{source}: needs one Duration (1/60 s ticks, > 0) per frame')
        return None
    if max(durs) > MAX_TICKS:
        warns.append(f'{source}: a frame of {max(durs)} ticks; frames hold at most {MAX_TICKS} (4.25 s), '
                     'so it is cut to that')
    path = sheet_path(folder, source)
    if not os.path.exists(path):
        (errors if name in REQUIRED else warns).append(f'{name}: {source}-Anim.png is missing')
        return None
    w, h, get = read_png(path)
    if w != fw * len(durs):
        errors.append(f'{source}-Anim.png is {w} px wide; {len(durs)} frames of {fw} need {fw * len(durs)}')
    if h % fh or h // fh not in (1, DIRECTIONS):
        errors.append(f'{source}-Anim.png is {h} px high: needs 1 or {DIRECTIONS} rows of {fh}')
    for tag in STRIKE_MARKS:
        value = anim.findtext(tag)
        if value is not None and not 0 <= int(value) < len(durs):
            errors.append(f'{source}: {tag} {value} is not one of its {len(durs)} frames')
    rows = h // fh
    if name == source and not errors:
        found = flashes(fw, fh, rows, len(durs), get)
        if found:
            warns.append(f"{name}: frame(s) {', '.join(map(str, sorted(found)))} are white flashes; the body is drawn "
                         'as in the frame before (the sparks stay)')
    return rows


def check_pictures(folder, pictures, keys, what, errors):
    """Each named picture exists and all are one size; returns that size, or None."""
    size = None
    for key in keys:
        if not pictures.get(key):
            continue
        path = os.path.join(folder, pictures[key])
        if not os.path.exists(path):
            errors.append(f'{what} {key}: {pictures[key]} is missing')
            continue
        wh = tuple(read_png(path)[:2])
        if size and wh != size:
            errors.append(f'{what} {key} is {wh[0]}x{wh[1]}; the others are {size[0]}x{size[1]}')
        size = size or wh
    return size


def check_scene(folder, scene, errors, warns):
    size = check_pictures(folder, scene, [key for _, key in SCENE_PICTURES], 'scene', errors)
    k = scene.get('scale', 1)
    if size and (size[0] * k < SCREEN_W or size[1] * k < SCREEN_H):
        errors.append(f'scene pictures are {size[0]}x{size[1]} at scale {k}; they must cover {SCREEN_W}x{SCREEN_H}')
    floor = scene.get('floor') or []
    if floor and len(floor) < 3:
        errors.append('scene floor: needs at least 3 points')
    if 'home' in scene and not is_point(scene['home']):
        errors.append('scene home: must be an [x, y] point')
    elif floor and scene.get('home') and not point_in_polygon(scene['home'], floor):
        warns.append(f'scene home {scene["home"]} is outside the floor; the pet will use the nearest spot')
    watch = scene.get('watch')
    if watch is not None and not is_point(watch):
        errors.append('scene watch: must be an [x, y] point')
    elif watch is not None and floor and not point_in_polygon(watch, floor):
        warns.append(f'scene watch {watch} is outside the floor; the pet will watch from beside its home')
    water = scene.get('water')
    if water is not None and not (isinstance(water, list) and len(water) >= 3 and all(is_point(pt) for pt in water)):
        errors.append('scene water: must be a polygon, at least 3 [x, y] points')
    depth = scene.get('water_depth', 0)
    if isinstance(depth, bool) or not isinstance(depth, int) or not 0 <= depth <= 64:
        errors.append('scene water_depth: must be a whole number of picture pixels, 0 to 64')
    shallow = scene.get('water_shallow', 0)
    if isinstance(shallow, bool) or not isinstance(shallow, int) or not 0 <= shallow <= 64:
        errors.append('scene water_shallow: must be a whole number of picture pixels, 0 to 64')
    blocked = scene.get('blocked') or []
    if not isinstance(blocked, list):
        errors.append('scene blocked: must be a list of polygons, each a list of [x, y] points')
        blocked = []
    for i, poly in enumerate(blocked):
        if not isinstance(poly, list) or len(poly) < 3 or not all(is_point(p) for p in poly):
            errors.append(f'scene blocked {i}: needs at least 3 [x, y] points')
        elif is_point(scene.get('home')) and point_in_polygon(scene['home'], poly):
            warns.append(f'scene home {scene["home"]} is inside blocked part {i}; the pet will use the nearest spot')
        elif is_point(scene.get('watch')) and point_in_polygon(scene['watch'], poly):
            warns.append(f'scene watch {scene["watch"]} is inside blocked part {i}; '
                         'the pet will watch from beside its home')
    if len(blocked) > MAX_BLOCKED:
        errors.append(f'scene blocked: at most {MAX_BLOCKED} polygons')
    workspot = scene.get('workspot')
    if workspot:
        check_pictures(folder, workspot, WORKSPOT_PICTURES, 'workspot', errors)
        if not workspot.get('on'):
            errors.append('workspot needs an "on" image')
        if not is_point(workspot.get('at')):
            errors.append('scene workspot: needs "at", an [x, y] point')
    swish = scene.get('swish')
    if swish:
        check_pictures(folder, swish, SWISH_PICTURES, 'swish', errors)
        if not is_point(swish.get('at')):
            errors.append('scene swish: needs "at", an [x, y] point')
    if 'gauges' in scene and scene['gauges'] not in CORNERS:
        errors.append(f'scene gauges: {scene["gauges"]!r} is not one of {", ".join(CORNERS)}')
    check_ambient(folder, scene.get('ambient'), errors)


def ambient_durations(part):
    """An ambient part's frame durations: one number for every frame, or a list of one per frame."""
    d, n = part.get('durations'), part.get('frames')
    return [d] * n if isinstance(d, int) and not isinstance(d, bool) else d


def check_ambient(folder, ambient, errors):
    """The scene's moving parts: up to 12, each a strip of frames side by side per picture, with durations; a part
    with a "cue" shows only while a pastime's step shows it by its "name"."""
    if ambient is None:
        return
    if not isinstance(ambient, list):
        errors.append('scene ambient: must be a list of parts')
        return
    if len(ambient) > MAX_AMBIENT:
        errors.append(f'scene ambient: at most {MAX_AMBIENT} parts')
    for i, part in enumerate(ambient):
        what = f'scene ambient {i}'
        if not isinstance(part, dict) or not is_point(part.get('at')):
            errors.append(f'{what}: needs "at", an [x, y] point')
            continue
        n = part.get('frames')
        if not isinstance(n, int) or isinstance(n, bool) or not 1 <= n <= MAX_AMBIENT_FRAMES:
            errors.append(f'{what}: "frames" must be 1 to {MAX_AMBIENT_FRAMES}')
            continue
        fill = part.get('fill')
        if fill is not None and not (isinstance(fill, list) and len(fill) == 3 and
                                     all(isinstance(v, int) and not isinstance(v, bool) and 0 <= v <= 255
                                         for v in fill)):
            errors.append(f'{what}: "fill" must be a colour [r, g, b]')
        if part.get('cue') not in (None, 'loop', 'once'):
            errors.append(f'{what}: "cue" must be "loop" or "once"')
        during = part.get('during')
        if during is not None and during not in [look['look'] for look in spec()['looks']]:
            errors.append(f'{what}: "during" must be a look, such as "type_fast"')
        if during is not None and not part.get('cue'):
            errors.append(f'{what}: a part shown "during" a look needs a "cue"')
        if part.get('cue') and not (isinstance(part.get('name'), str) and part['name']):
            errors.append(f'{what}: a part with a "cue" needs a "name" for pastimes to show it by')
        durs = ambient_durations(part)
        if (not isinstance(durs, list) or len(durs) != n or
                not all(isinstance(t, int) and not isinstance(t, bool) and 1 <= t <= MAX_TICKS for t in durs)):
            errors.append(f'{what}: "durations" must be 1 to {MAX_TICKS} ticks (1/60 s), one number or one per frame')
        if not any(part.get(key) for key in AMBIENT_PICTURES):
            errors.append(f'{what}: needs a picture for at least one of {", ".join(AMBIENT_PICTURES)}')
        size = None
        for key in AMBIENT_PICTURES:
            if not part.get(key):
                continue
            path = os.path.join(folder, part[key])
            if not os.path.exists(path):
                errors.append(f'{what} {key}: {part[key]} is missing')
                continue
            w, h = read_png(path)[:2]
            if w % n or w // n > MAX_AMBIENT_PX or h > MAX_AMBIENT_PX:
                errors.append(f'{what} {key}: {part[key]} is {w}x{h}; it must be {n} frames side by side, '
                              f'each at most {MAX_AMBIENT_PX}x{MAX_AMBIENT_PX}')
            elif size and (w, h) != size:
                errors.append(f'{what} {key} is {w}x{h}; the others are {size[0]}x{size[1]}')
            size = (w, h)


def print_report(folder, anims, rows, warns, errors, pace=None):
    """What each look will play with this folder, then the warnings and errors."""
    print(f'{os.path.basename(os.path.normpath(folder))}: {len(anims)} animations in the folder')
    if pace:
        print(f'  walks at {pace:g} px/s (its own pace)')
    for look in spec()['looks']:
        pick = next((p for p in look['plays']
                     if p['anim'] in rows and (not p.get('laps') or rows[p['anim']] == DIRECTIONS)), None)
        if pick:
            how = pick['anim'] + (f" x{pick['speed']}" if pick.get('speed') else '')
            how += ' laps' if pick.get('laps') else ''
        else:
            how = 'Idle (nothing on its list)'
        print(f"  {look['look']:<12} {look['kind']:<8} {how}")
    for w in warns:
        print('  warning: ' + w)
    for e in errors:
        print('  ERROR: ' + e)
    print('  ok' if not errors else f'  {len(errors)} error(s)')


def check_pace(meta, errors, warns):
    """pet.json's optional walking speed: the number, or None when it has none."""
    if 'pace' not in meta:
        return None
    pace = meta['pace']
    if isinstance(pace, bool) or not isinstance(pace, (int, float)) or pace <= 0:
        errors.append(f'pace: {pace!r} is not a walking speed (screen pixels per second, above 0)')
        return None
    lo, hi = PACE_RANGE
    if not lo <= pace <= hi:
        warns.append(f'pace {pace:g} is outside {lo} to {hi} px/s; the device walks it at {min(max(pace, lo), hi):g}')
    return pace


FLIGHTS = ('hoop', 'splash', 'toss')


def check_throw(meta, rows, anims, errors):
    """pet.json's optional throw kit: its throw (released on HitFrame), the item in the air, an optional catch and
    the flight. Hoop and splash flights aim at the scene's target."""
    kit = meta.get('throw')
    if kit is None:
        return
    if not isinstance(kit, dict):
        errors.append('throw: must be {"anim", "item", "catch", "flight"}')
        return
    for key in ('anim', 'item'):
        if not kit.get(key):
            errors.append(f'throw: needs "{key}"')
    names = {}
    for key in ('anim', 'item', 'catch'):
        name = kit.get(key)
        if not name:
            continue
        if not isinstance(name, str):
            errors.append(f"throw {key}: {name!r} is not an animation's name")
        elif name not in rows:
            errors.append(f'throw {key}: {name} is not one of its animations')
        else:
            names[key] = name
    for key in ('item', 'catch'):
        if key in names and rows[names[key]] != 1:
            errors.append(f'throw {key}: {names[key]} must have 1 row')
    if 'anim' in names and strike_mark(anims[names['anim']], 'HitFrame') < 0:
        errors.append(f"throw anim: {names['anim']} needs a HitFrame, where the item leaves")
    if 'typing_fast' in kit and not isinstance(kit['typing_fast'], bool):
        errors.append('throw typing_fast: must be true or false')
    if 'plays' in kit and not isinstance(kit['plays'], bool):
        errors.append('throw plays: must be true or false')
    height = kit.get('catch_height', 0.25)
    if isinstance(height, bool) or not isinstance(height, (int, float)) or not 0 <= height <= 1:
        errors.append('throw catch_height: must be a number from 0 (its feet) to 1 (the top of it)')
    if 'release' in kit:
        at, anim = kit['release'], anims.get(names.get('anim'))
        size = None if anim is None else (int(anim.findtext('FrameWidth')), int(anim.findtext('FrameHeight')))
        if not is_point(at):
            errors.append('throw release: must be an [x, y] point in the throw\'s frame')
        elif 'anim' in names and rows[names['anim']] != 1:
            errors.append(f"throw release: {names['anim']} must have 1 row to name a point in its frame")
        elif size and not (0 <= at[0] < size[0] and 0 <= at[1] < size[1]):
            errors.append(f"throw release: {at} is outside {names['anim']}'s {size[0]}x{size[1]} frame")
    flight = kit.get('flight', 'hoop')
    if flight not in FLIGHTS:
        errors.append(f'throw flight: {flight!r} is not one of {", ".join(FLIGHTS)}')
    elif flight != 'toss' and not is_point((meta.get('scene') or {}).get('target')):
        errors.append(f'throw flight {flight}: needs a "target" in the scene, an [x, y] point')


MAX_PASTIMES, MAX_PASTIME_STEPS = 16, 16


def dark_hours(scene):
    """The whole hours the scene shows its night picture: from 20:30 to 6:00, and the evening (18:00) too when it has
    no evening picture of its own. Empty without a night picture."""
    if not scene.get('night'):
        return set()
    return set(range(21, 24)) | set(range(0, 6)) | (set() if scene.get('evening') else {18, 19, 20})


def pastime_hours(hours):
    if hours is None:
        return set(range(24))
    a, b = hours
    return set(range(a, b)) if a <= b else set(range(a, 24)) | set(range(0, b))


def scene_size(folder, scene):
    """(w, h) of the scene picture in its own pixels, or None."""
    path = os.path.join(folder, scene.get('background') or '')
    return read_png(path)[:2] if scene.get('background') and os.path.exists(path) else None


NEAR_FLOOR_PX = 4        # a step point this close to the floor's edge is as good as on it


def edge_distance(point, polygon):
    """How far a point is from the nearest edge of a polygon, picture pixels."""
    best = float('inf')
    for (x0, y0), (x1, y1) in zip(polygon, polygon[1:] + polygon[:1]):
        dx, dy = x1 - x0, y1 - y0
        t = max(0.0, min(1.0, ((point[0] - x0) * dx + (point[1] - y0) * dy) / (dx * dx + dy * dy or 1)))
        best = min(best, ((point[0] - x0 - t * dx) ** 2 + (point[1] - y0 - t * dy) ** 2) ** 0.5)
    return best


def check_step_point(what, step, scene, size, warns):
    """A step's "at" is a percent of the scene: warn when it cannot be one, or when a walk to it ends off the floor."""
    at = step.get('at')
    if not (isinstance(at, list) and len(at) == 2 and all(isinstance(v, (int, float)) for v in at)):
        return
    if not all(0 <= v <= 100 for v in at):
        warns.append(f'{what}: "at" {at} is outside 0-100: a step\'s "at" is a percent of the scene, not pixels')
        return
    floor = scene.get('floor')
    if not size or not floor or step.get('swim') or step.get('jump'):
        return
    point = [at[0] * size[0] / 100, at[1] * size[1] / 100]
    holes = [h for h in scene.get('blocked') or [] if isinstance(h, list) and len(h) >= 3]
    outside = not point_in_polygon(point, floor) and edge_distance(point, floor) > NEAR_FLOOR_PX
    inside_hole = any(point_in_polygon(point, h) and edge_distance(point, h) > NEAR_FLOOR_PX for h in holes)
    if outside or inside_hole:
        warns.append(f'{what}: "at" {at} is picture point {[round(v) for v in point]}, off the floor or inside a '
                     f'blocked part; the pet stops at the nearest spot it can stand on')


def check_free_time(meta, rows, errors, warns, size):
    """pet.json's optional "free_time": its own pastimes, each a few steps written as a sender's "do" steps, and the
    hours of the day it may happen in. Warns about a part a pastime shows that has no frames for the night picture
    while the pastime can play at night: it would not show then."""
    pastimes = meta.get('free_time')
    if pastimes is None:
        return
    if not isinstance(pastimes, list) or len(pastimes) > MAX_PASTIMES:
        errors.append(f'free_time: must be a list of at most {MAX_PASTIMES} pastimes')
        return
    words = spec().get('words', [])
    scene = meta.get('scene') or {}
    parts = {a.get('name'): a for a in scene.get('ambient') or [] if isinstance(a, dict) and a.get('cue')}
    cued = list(parts)
    dark = dark_hours(scene)
    night_warned = set()
    for i, p in enumerate(pastimes):
        what = f"free_time {p.get('name') or i + 1}" if isinstance(p, dict) else f'free_time {i + 1}'
        if not isinstance(p, dict):
            errors.append(f'{what}: must be {{"name", "steps", "hours"}}')
            continue
        hours = p.get('hours')
        if hours is not None and not (isinstance(hours, list) and len(hours) == 2 and
                                      all(isinstance(h, int) and not isinstance(h, bool) and 0 <= h <= 24
                                          for h in hours)):
            errors.append(f'{what} hours: must be [from, to], whole hours 0 to 24')
            hours = None
        steps = p.get('steps')
        if not isinstance(steps, list) or not 1 <= len(steps) <= MAX_PASTIME_STEPS:
            errors.append(f'{what} steps: must be a list of 1 to {MAX_PASTIME_STEPS} steps')
            continue
        for k, step in enumerate(steps):
            if not isinstance(step, (str, dict)):
                errors.append(f'{what} step {k + 1}: must be a word or {{"do", "at", ...}}')
                continue
            word = step if isinstance(step, str) else step.get('do')
            if word is not None and (not isinstance(word, str) or (word not in words and word not in rows)):
                errors.append(f'{what} step {k + 1}: {word!r} is neither a behaviour ({", ".join(words)}) nor one of '
                              'its animations')
            if isinstance(step, dict):
                check_step_point(f'{what} step {k + 1}', step, scene, size, warns)
            show = step.get('show') if isinstance(step, dict) else None
            if show is not None and show not in cued:
                errors.append(f'{what} step {k + 1}: shows {show!r}, which is not a cued ambient part of its scene')
            elif (show is not None and not parts[show].get('night') and (what, show) not in night_warned and
                  dark & pastime_hours(hours or None)):
                night_warned.add((what, show))
                warns.append(f'{what}: {show}: the part has no night frames, so at night it will not show; give the '
                             f'pastime daytime "hours" or the part a "night" picture')


def check(folder, quiet=False):
    """Checks a pet folder against the format and, unless quiet, prints what each look will play.
    Returns the list of errors."""
    if not os.path.exists(os.path.join(folder, 'AnimData.xml')):
        return [f'no AnimData.xml in {folder}']
    try:
        anims = read_anim_data(folder)
    except ET.ParseError as e:
        return [f'AnimData.xml does not parse: {e}']
    errors, warns = [], []
    rows = {}
    for name in anims:
        n = check_anim(folder, name, anims, errors, warns)
        if n is not None:
            rows[name] = n
    for look in spec()['looks']:
        for p in look['plays']:
            if p.get('laps') and rows.get(p['anim'], DIRECTIONS) != DIRECTIONS:
                msg = (f"{p['anim']} has {rows[p['anim']]} row(s); laps need all {DIRECTIONS} directions, "
                       f"so it is left unused")
                if msg not in warns:
                    warns.append(msg)
    for name in REQUIRED:
        if name not in rows:
            errors.append(f'every pet needs {name}')
        elif rows[name] != DIRECTIONS:
            (errors if name == 'Walk' else warns).append(
                f'{name} has {rows[name]} row(s); it should have all {DIRECTIONS} directions')
    if os.path.exists(os.path.join(folder, 'pet.json')):
        try:
            meta = read_meta(folder)
        except ValueError as e:
            errors.append(f'pet.json does not parse: {e}')
            meta = {}
        check_scene(folder, meta.get('scene') or {}, errors, warns)
        check_throw(meta, rows, anims, errors)
        check_free_time(meta, rows, errors, warns, scene_size(folder, meta.get('scene') or {}))
    else:
        meta = {}
    pace = check_pace(meta, errors, warns)
    if not quiet:
        print_report(folder, anims, rows, warns, errors, pace)
    return errors


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


class Palette:
    """The pet's colours as RGB565, shared by every frame and picture. Index 0 is transparent."""

    def __init__(self):
        self.colours = [None]
        self._index = {}

    def index(self, c, opaque=True):
        """The palette index for an (r, g, b, a) pixel; 0 for a see-through pixel unless `opaque`."""
        if not opaque and not is_opaque(c):
            return 0
        key = rgb565(c[0], c[1], c[2])
        if key not in self._index:
            if len(self.colours) > MAX_COLOURS:
                sys.exit(f'more than {MAX_COLOURS} colours across the folder')
            self._index[key] = len(self.colours)
            self.colours.append(key)
        return self._index[key]


class FlashFix:
    """Draws a white flash frame's body as it is in the frame before, keeping the flash's sparks."""

    def __init__(self, get, fw, fh, frames, flash):
        self.get, self.fw, self.fh, self.frames, self.flash = get, fw, fh, frames, flash
        self._silhouettes = {}

    def before(self, frame):
        """The nearest frame before `frame` that is not a flash."""
        while frame in self.flash:
            frame = (frame - 1) % self.frames
        return frame

    def at(self, frame, row, x, y):
        return self.get(frame * self.fw + x, row * self.fh + y)

    def silhouette(self, frame, row):
        """The white patches (4-connected) that mostly cover the body as it is in the frame before. White sparks
        away from the body are not part of it."""
        key = (frame, row)
        if key not in self._silhouettes:
            self._silhouettes[key] = self._find_silhouette(frame, row)
        return self._silhouettes[key]

    def _find_silhouette(self, frame, row):
        def white(x, y):
            return is_white(self.at(frame, row, x, y))

        def body(x, y):
            return is_opaque(self.at(self.before(frame), row, x, y))

        seen, keep = set(), set()
        for y0 in range(self.fh):
            for x0 in range(self.fw):
                if (x0, y0) in seen or not white(x0, y0):
                    continue
                seen.add((x0, y0))
                patch, todo = [], [(x0, y0)]
                while todo:
                    x, y = todo.pop()
                    patch.append((x, y))
                    for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                        if 0 <= nx < self.fw and 0 <= ny < self.fh and (nx, ny) not in seen and white(nx, ny):
                            seen.add((nx, ny))
                            todo.append((nx, ny))
                if sum(body(x, y) for x, y in patch) * 2 >= len(patch):
                    keep.update(patch)
        return keep

    def pixel(self, frame, row, x, y):
        if frame in self.flash and (x, y) in self.silhouette(frame, row):
            return self.at(self.before(frame), row, x, y)
        return self.at(frame, row, x, y)


def pack_anim(name, anim, sheet, palette):
    """One animation as eggformat.write() takes it, its pixels as palette indices (row, frame, y, x)."""
    fw, fh = int(anim.findtext('FrameWidth')), int(anim.findtext('FrameHeight'))
    durs = durations(anim)
    _, h, get = sheet
    rows, frames = h // fh, len(durs)
    if rows not in (1, DIRECTIONS):
        sys.exit(f"{anim.findtext('Name')}: {rows} rows; a sheet needs 1 or {DIRECTIONS}")
    fix = FlashFix(get, fw, fh, frames, flashes(fw, fh, rows, frames, get))
    data = bytearray()
    for r in range(rows):
        for f in range(frames):
            for y in range(fh):
                for x in range(fw):
                    data.append(palette.index(fix.pixel(f, r, x, y), opaque=False))
    return {'name': name, 'w': fw, 'h': fh, 'rows': rows, 'frames': frames,
            'rush': strike_mark(anim, 'RushFrame'), 'hit': strike_mark(anim, 'HitFrame'),
            'return': strike_mark(anim, 'ReturnFrame'), 'durations': durs, 'data': bytes(data)}


def pack_anims(folder, anims, meta, palette):
    packed, sheets = [], {}
    for name in used(meta):
        if name not in anims:
            continue
        source = anims[name].findtext('CopyOf') or name
        if name == 'Strike' and source != name:
            continue  # the engine falls back from Strike to Attack by itself
        if source not in sheets:
            path = sheet_path(folder, source)
            if not os.path.exists(path):
                continue
            sheets[source] = read_png(path)
        packed.append(pack_anim(name, anims[source], sheets[source], palette))
    return packed


def pack_pictures(folder, files, keys, what, palette, opaque):
    """The named pictures as palette indices, row by row: ({key: bytes}, (w, h)). All must be one size."""
    pictures, size = {}, None
    for key in keys:
        if not files.get(key):
            continue
        w, h, get = read_png(os.path.join(folder, files[key]))
        if size and (w, h) != size:
            sys.exit(f'{what} {key} is {w}x{h}; the others are {size[0]}x{size[1]}')
        size = (w, h)
        pictures[key] = bytes(palette.index(get(x, y), opaque) for y in range(h) for x in range(w))
    return pictures, size


def pack_overlay(folder, overlay, keys, what, palette):
    """A picture placed in the scene at overlay['at'] (the work spot, the swish): {w, h, x, y, pictures}."""
    pictures, size = pack_pictures(folder, overlay, keys, what, palette, opaque=False)
    if not pictures:
        return None
    return {'w': size[0], 'h': size[1], 'x': overlay['at'][0], 'y': overlay['at'][1], 'pictures': pictures}


def pack_scene(folder, scene, palette):
    """The pet's own scene from pet.json, or None without a background."""
    if not scene or not scene.get('background'):
        return None
    files = {part: scene.get(key) for part, key in SCENE_PICTURES}
    pictures, size = pack_pictures(folder, files, [part for part, _ in SCENE_PICTURES], 'scene', palette,
                                   opaque=True)
    packed = {'w': size[0], 'h': size[1], 'scale': scene.get('scale', 1), 'pictures': pictures,
              'floor': scene.get('floor', []), 'blocked': scene.get('blocked') or None,
              'home': scene.get('home', [-1, -1]), 'keep': scene.get('keep'),
              'target': scene.get('target'), 'watch': scene.get('watch'), 'workspot': None,
              'water': scene.get('water'), 'water_depth': scene.get('water_depth', 0),
              'water_shallow': scene.get('water_shallow', 0)}
    if scene.get('gauges'):
        packed['gauges'] = scene['gauges']
    if scene.get('workspot'):
        packed['workspot'] = pack_overlay(folder, scene['workspot'], WORKSPOT_PICTURES, 'workspot', palette)
        if not packed['workspot'] or 'on' not in packed['workspot']['pictures']:
            sys.exit('workspot needs an "on" image')
    if scene.get('swish'):
        swish = pack_overlay(folder, scene['swish'], SWISH_PICTURES, 'swish', palette)
        if swish and 'day' in swish['pictures']:
            packed['swish'] = swish
    packed['ambient'] = [pack_ambient(folder, part, palette) for part in scene.get('ambient') or []]
    return packed


def pack_ambient(folder, part, palette):
    """A moving part of the scene: {w, h, x, y, frames, durations, pictures}. Each picture is the pixels its
    frames change: a count per frame (16-bit), then each frame's pixels as x, y and palette index."""
    n = part['frames']
    strips, (w, h) = pack_pictures(folder, part, AMBIENT_PICTURES, 'scene ambient', palette, opaque=False)
    fw = w // n
    pictures = {}
    for key, strip in strips.items():
        frames = [[(x, y, strip[y * w + f * fw + x]) for y in range(h) for x in range(fw) if strip[y * w + f * fw + x]]
                  for f in range(n)]
        pictures[key] = (b''.join(struct.pack('<H', len(f)) for f in frames) +
                         bytes(v for f in frames for px in f for v in px))
    fill = palette.index(tuple(part['fill']) + (255,)) if part.get('fill') else 0
    return {'name': part.get('name', ''), 'cue': part.get('cue', ''), 'during': part.get('during', ''), 'fill': fill,
            'w': fw, 'h': h,
            'x': part['at'][0], 'y': part['at'][1], 'frames': n, 'durations': ambient_durations(part),
            'pictures': pictures}


def feet_and_height(idle):
    """From Idle's first row: pixels from the frame's centre down to the feet, and the visible height."""
    fw, fh, data = idle['w'], idle['h'], idle['data']
    top, bottom = fh, -1
    for f in range(idle['frames']):
        base = f * fw * fh
        for y in range(fh):
            if any(data[base + y * fw + x] for x in range(fw)):
                top, bottom = min(top, y), max(bottom, y)
    return bottom + 1 - fh // 2, bottom + 1 - top


def pack(folder, out, name=None):
    """Packs a folder into the egg `out`; exits with the errors if the folder has any."""
    errors = check(folder, quiet=True)
    if errors:
        sys.exit('not packed:\n  ' + '\n  '.join(errors) + '\n(run with --check for the full report)')
    anims = read_anim_data(folder)
    meta = read_meta(folder)
    palette = Palette()
    packed = pack_anims(folder, anims, meta, palette)
    scene = pack_scene(folder, meta.get('scene'), palette)
    feet, tall = feet_and_height(next(a for a in packed if a['name'] == 'Idle'))
    dash = meta.get('dash', DEFAULT_DASH)
    if not isinstance(dash, (list, tuple)) or not all(isinstance(d, str) for d in dash):
        sys.exit('pet.json "dash" must be a list of animation names')
    for a in packed:
        a['dash'] = a['name'] in dash
    name = name or meta.get('name') or os.path.basename(os.path.normpath(folder))
    pet = {'name': name, 'notes': meta.get('notes', {}), 'palette': palette.colours, 'feet': feet, 'tall': tall,
           'scene': scene, 'anims': packed, 'scale': meta.get('scale', 0), 'pace': meta.get('pace', 0),
           'private': bool(meta.get('private')), 'throw': meta.get('throw'),
           'free_time': meta.get('free_time')}
    if meta.get('screen_palette'):
        sp = meta['screen_palette']
        if (not isinstance(sp, list) or not 2 <= len(sp) <= 32
                or not all(isinstance(c, str) and len(c) == 7 and c[0] == '#' for c in sp)):
            sys.exit('pet.json "screen_palette" must be 2 to 32 "#rrggbb" colours')
        pet['screen_palette'] = sp
    for k in WORDS:
        pet[k] = meta.get(k, '')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    size = eggformat.write(pet, out)
    print(f'{name}: {len(packed)} animations, {len(palette.colours) - 1} colours, feet {feet}, tall {tall}, '
          + ('own scene, ' if scene else '') + f'{size / 1024:.0f} KB -> {out}')


def main():
    parser = argparse.ArgumentParser(description='Check a pet folder and pack it into an egg for NestBox.')
    parser.add_argument('folder')
    parser.add_argument('-o', '--out', help='the egg to write')
    parser.add_argument('--name', help="the pet's name, instead of pet.json's")
    parser.add_argument('--check', action='store_true', help='check the folder and show what each look will play')
    args = parser.parse_args()
    if args.check:
        sys.exit(1 if check(args.folder) else 0)
    if not args.out:
        parser.error('-o/--out is required to pack (or use --check)')
    pack(args.folder, args.out, args.name)


if __name__ == '__main__':
    main()
