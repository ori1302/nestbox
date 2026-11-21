"""The egg packer (tools/eggpack/eggpack.py): writes a tiny pet folder (Idle and Walk in 8 directions, a pet.json),
checks and packs it, reads the egg's description back and, where the simulator is built, has it draw the pet. It
checks pet.json's pace, blocked parts, ambient parts and throw kit. Then packs pets/retro/nibble/ and compares the
result with pets/retro/nibble.egg byte for byte.

    python3 tests/pack_test.py [--build DIR]
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

from harness import ROOT, build_dir, check, finish

sys.path.insert(0, os.path.join(ROOT, 'tools', 'eggpack'))
from png import write_png  # noqa: E402

BUILD = build_dir()
PACKER = os.path.join(ROOT, 'tools', 'eggpack', 'eggpack.py')
NIBBLE = os.path.join(ROOT, 'pets', 'retro', 'nibble')
FRAME = 24                                 # px, square frames
BODY = (230, 150, 60)                      # the blob's orange, looked for on the simulator's screen


def write_sheet(path, frames, rows=8):
    """A blob per frame: a body, an eye on the side it faces, and a foot that moves between frames."""
    def px(x, y):
        f, row, fx, fy = x // FRAME, y // FRAME, x % FRAME, y % FRAME
        body = (fx - 12) ** 2 + (fy - 13) ** 2 < 36
        eye = fy == 11 and fx == 12 + [0, 2, 3, 2, 0, -2, -3, -2][row] + (-1 if f % 2 else 1)
        foot = fy == 19 and fx in ((10, 11) if f % 2 else (13, 14))
        if eye:
            return (20, 20, 30, 255)
        if body:
            return (*BODY, 255)
        if foot:
            return (120, 70, 30, 255)
        return (0, 0, 0, 0)
    write_png(path, FRAME * frames, FRAME * rows, px)


def anim_xml(name, frames, ticks, hit=None):
    durations = ''.join(f'<Duration>{ticks}</Duration>' for _ in range(frames))
    marker = f'<HitFrame>{hit}</HitFrame>' if hit is not None else ''
    return (f'<Anim><Name>{name}</Name><FrameWidth>{FRAME}</FrameWidth><FrameHeight>{FRAME}</FrameHeight>'
            f'{marker}<Durations>{durations}</Durations></Anim>')


def write_pet_folder(folder):
    os.makedirs(folder)
    write_sheet(os.path.join(folder, 'Idle-Anim.png'), 2)
    write_sheet(os.path.join(folder, 'Walk-Anim.png'), 4)
    with open(os.path.join(folder, 'AnimData.xml'), 'w') as f:
        f.write('<AnimData><Anims>' + anim_xml('Idle', 2, 30) + anim_xml('Walk', 4, 8) + '</Anims></AnimData>')
    with open(os.path.join(folder, 'pet.json'), 'w') as f:
        json.dump({'name': 'Blob', 'theme': 'a small orange blob', 'credit': 'the test', 'license': 'CC0'}, f)


def run_packer(*args):
    return subprocess.run([sys.executable, '-I', PACKER, *args], capture_output=True, text=True,
                          env={**os.environ, 'NESTBOX_BUILD_DIR': BUILD})


def description(egg):
    """The egg's JSON description, after the NEGG magic and its length."""
    with open(egg, 'rb') as f:
        b = f.read()
    return json.loads(b[8:8 + int.from_bytes(b[4:8], 'little')])


def check_egg(egg):
    with open(egg, 'rb') as f:
        b = f.read()
    d = description(egg)
    check(b[:4] == b'NEGG', 'an egg file')
    check(d.get('name') == 'Blob' and d.get('credit') == 'the test' and d.get('license') == 'CC0',
          "pet.json's name, credit and licence are inside")
    check(sorted(a['name'] for a in d['anims']) == ['Idle', 'Walk'], 'Idle and Walk')


def simulator_draws(simulator, pets, tmp):
    shot = os.path.join(tmp, 'shot.ppm')
    r = subprocess.run([simulator, '--pets', pets, '--shot', shot, '0', 'idle', '500'],
                       capture_output=True, text=True, env={**os.environ, 'HOME': tmp})
    if r.returncode != 0 or not os.path.exists(shot):
        return False
    with open(shot, 'rb') as f:
        data = f.read()
    px = data[data.index(b'255\n') + 4:]
    # RGB565 rounds each channel, so look for a colour near the blob's, not equal to it.
    return any(all(abs(px[i + c] - BODY[c]) < 10 for c in range(3)) for i in range(0, len(px) - 2, 3))


def blob_packs(tmp):
    """A tiny folder checks out, packs, and (on macOS) draws."""
    folder = os.path.join(tmp, 'blob')
    write_pet_folder(folder)
    r = run_packer(folder, '--check')
    check(r.returncode == 0 and '  ok' in r.stdout,
          'the folder checks out (' + (r.stdout + r.stderr).strip().splitlines()[-1] + ')')
    pets = os.path.join(tmp, 'pets')
    egg = os.path.join(pets, 'blob.egg')
    r = run_packer(folder, '-o', egg)
    if check(r.returncode == 0 and os.path.exists(egg), 'it packs (' + (r.stdout + r.stderr).strip()[:120] + ')'):
        check_egg(egg)
        simulator = os.path.join(BUILD, 'nestbox-sim')
        if os.path.exists(simulator):
            check(simulator_draws(simulator, pets, tmp), 'the simulator loads it and draws it')
        else:
            print('  skip the simulator is not built on this platform')


def zoomies_reported(tmp):
    """A run cycle named Zoomies with all 8 rows runs the party's laps; with one row it is left unused."""
    for rows in (8, 1):
        folder = os.path.join(tmp, f'zoomies{rows}')
        write_pet_folder(folder)
        write_sheet(os.path.join(folder, 'Zoomies-Anim.png'), 4, rows)
        with open(os.path.join(folder, 'AnimData.xml'), 'w') as f:
            f.write('<AnimData><Anims>' + anim_xml('Idle', 2, 30) + anim_xml('Walk', 4, 8) +
                    anim_xml('Zoomies', 4, 4) + '</Anims></AnimData>')
        r = run_packer(folder, '--check')
        party = next((l for l in r.stdout.splitlines() if l.strip().startswith('party ')), '')
        if rows == 8:
            check('Zoomies laps' in party, 'an 8-row Zoomies runs laps for the party (' + party.strip() + ')')
        else:
            check('Zoomies' not in party and 'laps need all 8 directions' in r.stdout,
                  'a one-row Zoomies is left unused, with a warning')


def pace_carried(tmp):
    """pet.json's walking pace goes into the egg and --check reports it; one that is not a speed is refused, one out of
    range is warned about; without it the description has no pace at all."""
    def packed_with(pace, name):
        folder = os.path.join(tmp, name)
        write_pet_folder(folder)
        meta_path = os.path.join(folder, 'pet.json')
        meta = json.load(open(meta_path))
        if pace is not None:
            meta['pace'] = pace
        json.dump(meta, open(meta_path, 'w'))
        egg = os.path.join(tmp, name + '.egg')
        return folder, egg, run_packer(folder, '-o', egg)

    folder, egg, r = packed_with(40, 'paced')
    check(r.returncode == 0 and description(egg).get('pace') == 40, "a pace of 40 is in the egg's description")
    check('walks at 40 px/s' in run_packer(folder, '--check').stdout, '--check reports the pace')
    folder, egg, r = packed_with(500, 'fast')
    check(r.returncode == 0 and 'outside 20 to 120' in run_packer(folder, '--check').stdout,
          'a pace out of range packs, with a warning')
    folder, egg, r = packed_with('quick', 'wordy')
    check(r.returncode != 0 and 'not a walking speed' in (r.stdout + r.stderr),
          'a pace that is not a number is refused')
    folder, egg, r = packed_with(None, 'plain')
    check(r.returncode == 0 and 'pace' not in description(egg), 'without a pace the egg has none')


def blocked_carried(tmp):
    """A scene's blocked polygons (holes in its floor) go into the egg's scene; a polygon of fewer than three points
    is refused."""
    folder = os.path.join(tmp, 'holed')
    shutil.copytree(NIBBLE, folder)
    meta_path = os.path.join(folder, 'pet.json')
    meta = json.load(open(meta_path))
    hole = [[60, 90], [70, 90], [70, 98], [60, 98]]
    meta['scene']['blocked'] = [hole]
    json.dump(meta, open(meta_path, 'w'))
    egg = os.path.join(tmp, 'holed.egg')
    r = run_packer(folder, '-o', egg)
    d = description(egg) if r.returncode == 0 else {}
    check(d.get('scene', {}).get('blocked') == [hole], "a blocked polygon is in the egg's scene")
    meta['scene']['blocked'] = [[[60, 90], [70, 90]]]
    json.dump(meta, open(meta_path, 'w'))
    r = run_packer(folder, '--check')
    check('scene blocked 0: needs at least 3' in r.stdout, 'a blocked part of two points is refused')


def gauges_carried(tmp):
    """A scene's corner for its gauges goes into the egg's scene; a word that is not a corner is refused."""
    folder = os.path.join(tmp, 'cornered')
    shutil.copytree(NIBBLE, folder)
    meta_path = os.path.join(folder, 'pet.json')
    meta = json.load(open(meta_path))
    meta['scene']['gauges'] = 'bottom-right'
    json.dump(meta, open(meta_path, 'w'))
    egg = os.path.join(tmp, 'cornered.egg')
    r = run_packer(folder, '-o', egg)
    check(r.returncode == 0 and description(egg)['scene'].get('gauges') == 'bottom-right',
          "the gauges' corner is in the egg's scene")
    meta['scene']['gauges'] = 'middle'
    json.dump(meta, open(meta_path, 'w'))
    r = run_packer(folder, '--check')
    check('scene gauges:' in r.stdout, 'gauges in something that is not a corner are refused')


def ambient_checked(tmp):
    """A scene's moving parts go into the egg as the pixels each frame changes; a part may have night frames only;
    a strip that isn't whole frames, or durations that miss a frame, are refused."""
    folder = os.path.join(tmp, 'moving')
    shutil.copytree(NIBBLE, folder)
    meta_path = os.path.join(folder, 'pet.json')
    meta = json.load(open(meta_path))
    lit = (250, 220, 120, 255)
    write_png(os.path.join(folder, 'lamp.png'), 6, 4, lambda x, y: lit if x // 2 == 1 else (0, 0, 0, 0))
    meta['scene']['ambient'] = [{'at': [10, 20], 'frames': 3, 'durations': [5, 6, 7], 'night': 'lamp.png'}]
    meta.pop('free_time', None)                                  # its pastimes show parts this test replaces
    json.dump(meta, open(meta_path, 'w'))
    egg = os.path.join(tmp, 'moving.egg')
    r = run_packer(folder, '-o', egg)
    part = description(egg)['scene']['ambient'][0] if r.returncode == 0 else {}
    check({k: part.get(k) for k in ('w', 'h', 'x', 'y', 'frames', 'durations')} ==
          {'w': 2, 'h': 4, 'x': 10, 'y': 20, 'frames': 3, 'durations': [5, 6, 7]} and
          list(part.get('pictures', {})) == ['night'], "an ambient part's size, place, frames and durations")
    if part:
        with open(egg, 'rb') as f:
            b = f.read()
        at = part['pictures']['night']
        counts = struct.unpack('<3H', b[at:at + 6])
        pixels = [tuple(b[at + 6 + i * 3:at + 9 + i * 3]) for i in range(counts[1])]
        check(counts == (0, 8, 0) and sorted((x, y) for x, y, _ in pixels) == [(x, y) for x in (0, 1) for y in range(4)]
              and len({k for _, _, k in pixels}) == 1 and pixels[0][2] > 0,
              'only the pixels its frames change, frame by frame')
    meta['scene']['ambient'][0]['day'] = 'lamp.png'
    json.dump(meta, open(meta_path, 'w'))
    alone = os.path.getsize(egg)
    r = run_packer(folder, '-o', egg)
    part = description(egg)['scene']['ambient'][0] if r.returncode == 0 else {}
    pics = part.get('pictures', {})
    check(pics.get('day') is not None and pics.get('day') == pics.get('night') and os.path.getsize(egg) - alone < 64,
          'the same frames for day and night are stored once, both pointing at them')
    del meta['scene']['ambient'][0]['day']
    meta['scene']['ambient'][0].update(cue='once', name='lamp')
    json.dump(meta, open(meta_path, 'w'))
    r = run_packer(folder, '-o', egg)
    part = description(egg)['scene']['ambient'][0] if r.returncode == 0 else {}
    check(part.get('cue') == 'once' and part.get('name') == 'lamp', "a cued part's name and cue go into the egg")
    meta['scene']['ambient'][0]['fill'] = [12, 34, 56]
    json.dump(meta, open(meta_path, 'w'))
    r = run_packer(folder, '-o', egg)
    part = description(egg)['scene']['ambient'][0] if r.returncode == 0 else {}
    check(isinstance(part.get('fill'), int) and part['fill'] > 0,
          f"a part's fill goes into the egg as a palette colour ({part.get('fill')})")
    meta['scene']['ambient'][0]['during'] = 'type_fast'
    json.dump(meta, open(meta_path, 'w'))
    r = run_packer(folder, '-o', egg)
    part = description(egg)['scene']['ambient'][0] if r.returncode == 0 else {}
    check(part.get('during') == 'type_fast', "a part's look goes into the egg")
    del meta['scene']['ambient'][0]['during']
    for bad, words in (({'fill': 'navy'}, '"fill" must be a colour [r, g, b]'),
                       ({'during': 'typing'}, '"during" must be a look'),
                       ({'during': 'type_fast', 'cue': None}, 'a part shown "during" a look needs a "cue"'),
                       ({'cue': 'sometimes'}, '"cue" must be "loop" or "once"'),
                       ({'cue': 'loop', 'name': ''}, 'a part with a "cue" needs a "name"')):
        meta['scene']['ambient'][0].update(bad)
        json.dump(meta, open(meta_path, 'w'))
        r = run_packer(folder, '--check')
        check(r.returncode != 0 and words in r.stdout, f'a part with {bad} is refused ({words})')
    meta['scene']['ambient'][0].update(cue='once', name='lamp')
    meta['scene']['ambient'][0].pop('fill', None)
    meta['scene']['ambient'][0].pop('during', None)
    meta['free_time'] = [{'name': 'a look', 'steps': [{'do': 'stand', 'for': 2, 'show': 'lantern'}]}]
    json.dump(meta, open(meta_path, 'w'))
    r = run_packer(folder, '--check')
    check(r.returncode != 0 and "shows 'lantern', which is not a cued ambient part" in r.stdout,
          'a pastime that shows a part its scene has no cue for is refused')
    del meta['free_time']
    meta['scene']['ambient'][0].pop('cue')
    meta['scene']['ambient'][0]['frames'] = 4
    json.dump(meta, open(meta_path, 'w'))
    r = run_packer(folder, '--check')
    check('scene ambient 0: "durations"' in r.stdout and 'it must be 4 frames side by side' in r.stdout,
          'durations that miss a frame and a strip that is not whole frames are refused')


def write_thrower(folder, kit, scene=None):
    """The blob with a throw (Fling, 8 rows, released on frame 2), a one-row throw (Flick, released on frame 2), an
    item in the air (Pebble, one row), a catch (Snatch, one row) and Lob, a throw without a HitFrame; none of them is
    on any look's list. `kit` is pet.json's "throw"."""
    write_pet_folder(folder)
    write_sheet(os.path.join(folder, 'Fling-Anim.png'), 4)
    write_sheet(os.path.join(folder, 'Lob-Anim.png'), 4)
    write_sheet(os.path.join(folder, 'Flick-Anim.png'), 4, 1)
    write_sheet(os.path.join(folder, 'Pebble-Anim.png'), 2, 1)
    write_sheet(os.path.join(folder, 'Snatch-Anim.png'), 2, 1)
    with open(os.path.join(folder, 'AnimData.xml'), 'w') as f:
        f.write('<AnimData><Anims>' + anim_xml('Idle', 2, 30) + anim_xml('Walk', 4, 8) +
                anim_xml('Fling', 4, 6, hit=2) + anim_xml('Flick', 4, 6, hit=2) + anim_xml('Lob', 4, 6) +
                anim_xml('Pebble', 2, 4) + anim_xml('Snatch', 2, 6) + '</Anims></AnimData>')
    meta_path = os.path.join(folder, 'pet.json')
    meta = json.load(open(meta_path))
    if kit is not None:
        meta['throw'] = kit
    if scene is not None:
        meta['scene'] = scene
    json.dump(meta, open(meta_path, 'w'))


def throw_kit_checked(tmp):
    """pet.json's throw kit: it needs a throw and an item, each one of the folder's animations; the item and the catch
    have one row; the throw marks where the item leaves (HitFrame); the flight is hoop, splash or toss, and hoop and
    splash aim at the scene's target."""
    n = [0]

    def checked(kit, scene=None):
        n[0] += 1
        folder = os.path.join(tmp, f'thrower{n[0]}')
        write_thrower(folder, kit, scene)
        r = run_packer(folder, '--check')
        return r.returncode, r.stdout + r.stderr

    good = {'anim': 'Fling', 'item': 'Pebble', 'catch': 'Snatch', 'flight': 'toss'}
    code, out = checked(good)
    check(code == 0 and 'ERROR' not in out, 'a full toss kit checks out' + ('' if code == 0 else f' ({out.strip()})'))
    code, out = checked({'anim': 'Fling', 'item': 'Pebble', 'flight': 'toss'})
    check(code == 0, 'the catch is optional')
    code, out = checked({**good, 'anim': 'Flick', 'release': [14, 9]})
    check(code == 0,
          'a release point in a one-row throw\'s frame checks out' + ('' if code == 0 else f' ({out.strip()})'))
    target = {'target': [120, 100]}
    for flight in ('hoop', 'splash', None):
        kit = {'anim': 'Fling', 'item': 'Pebble', **({'flight': flight} if flight else {})}
        name = flight or 'no flight (hoop by default)'
        code, out = checked(kit)
        check(code != 0 and f'throw flight {flight or "hoop"}: needs a "target" in the scene' in out,
              f'{name} without a scene target is refused')
        code, out = checked(kit, target)
        check(code == 0, f'{name} with a scene target checks out')
        code, out = checked(kit, {'target': 'pond'})
        check(code != 0 and 'an [x, y] point' in out, f'{name} with a target that is not a point is refused')
    cases = [
        ({'item': 'Pebble', 'flight': 'toss'}, 'throw: needs "anim"', 'a kit without its throw'),
        ({'anim': 'Fling', 'flight': 'toss'}, 'throw: needs "item"', 'a kit without its item'),
        ({**good, 'anim': 'Yeet'}, 'throw anim: Yeet is not one of its animations', 'a throw it does not have'),
        ({**good, 'item': 'Rock'}, 'throw item: Rock is not one of its animations', 'an item it does not have'),
        ({**good, 'catch': 'Grab'}, 'throw catch: Grab is not one of its animations', 'a catch it does not have'),
        ({**good, 'item': 'Walk'}, 'throw item: Walk must have 1 row', 'an item drawn in 8 directions'),
        ({**good, 'catch': 'Fling'}, 'throw catch: Fling must have 1 row', 'a catch drawn in 8 directions'),
        ({**good, 'anim': 'Lob'}, 'throw anim: Lob needs a HitFrame', 'a throw without a HitFrame'),
        ({**good, 'flight': 'lob'}, "throw flight: 'lob' is not one of hoop, splash, toss", 'an unknown flight'),
        ('Fling', 'throw: must be {', 'a kit that is not an object'),
        ({**good, 'item': ['Pebble']}, "throw item: ['Pebble'] is not an animation's name", 'a name that is not text'),
        ({**good, 'release': [14, 9]}, 'throw release: Fling must have 1 row', 'a release point in an 8-row throw'),
        ({**good, 'plays': 'yes'}, 'throw plays: must be true or false', 'plays that is not true or false'),
        ({**good, 'anim': 'Flick', 'release': 'beak'}, 'throw release: must be an [x, y] point',
         'a release that is not a point'),
        ({**good, 'anim': 'Flick', 'release': [FRAME, 2]}, f"throw release: [{FRAME}, 2] is outside Flick's",
         'a release point outside the frame'),
    ]
    for kit, words, what in cases:
        code, out = checked(kit)
        check(code != 0 and words in out, f'{what} is refused ({words})')


def free_time_checked(tmp):
    """pet.json's "free_time": pastimes whose steps name behaviours or the folder's own animations, with optional
    hours; the animations they name are packed though no look plays them, and the pastimes go into the egg."""
    n = [0]

    def checked(free_time, pack=False):
        n[0] += 1
        folder = os.path.join(tmp, f'pastimes{n[0]}')
        write_thrower(folder, None)
        meta_path = os.path.join(folder, 'pet.json')
        meta = json.load(open(meta_path))
        meta['free_time'] = free_time
        json.dump(meta, open(meta_path, 'w'))
        if pack:
            egg = os.path.join(tmp, f'pastimes{n[0]}.egg')
            r = run_packer(folder, '-o', egg)
            return r.returncode, r.stdout + r.stderr, description(egg) if r.returncode == 0 else {}
        r = run_packer(folder, '--check')
        return r.returncode, r.stdout + r.stderr

    good = [{'name': 'a lob', 'hours': [9, 17], 'steps': [{'at': [40, 60]}, {'do': 'Lob', 'times': 2}, 'stand']}]
    code, out, d = checked(good, pack=True)
    if check(code == 0, 'pastimes check out and pack' + ('' if code == 0 else f' ({out.strip()[-200:]})')):
        names = sorted(a['name'] for a in d['anims'])
        check('Lob' in names, f'the animation a pastime plays is packed though no look plays it ({names})')
        check(d.get('free_time') == good, f"the pastimes are in the egg's description ({d.get('free_time')})")
    cases = [
        ([{'steps': [{'do': 'Yeet'}]}], "free_time 1 step 1: 'Yeet' is neither a behaviour", 'an unknown word'),
        ([{'name': 'x', 'steps': []}], 'free_time x steps: must be a list of 1 to 16 steps', 'no steps'),
        ([{'name': 'x', 'steps': ['stand'] * 17}], 'free_time x steps: must be a list of 1 to 16 steps', '17 steps'),
        ([{'name': 'x', 'hours': [9], 'steps': ['stand']}], 'free_time x hours: must be [from, to]', 'half an hours'),
        ({'steps': ['stand']}, 'free_time: must be a list of at most 16 pastimes', 'not a list'),
        ([{'steps': ['stand']}] * 17, 'free_time: must be a list of at most 16 pastimes', 'seventeen pastimes'),
    ]
    for free_time, words, what in cases:
        code, out = checked(free_time)
        check(code != 0 and words in out, f'{what} is refused ({words})')


def water_checked(tmp):
    """A scene's water and how deep it swims go into the egg with its Swim and Jump, though no look plays them; water
    that is not a polygon, or a depth that is not a whole number of pixels, is refused."""
    folder = os.path.join(tmp, 'swimmer')
    shutil.copytree(NIBBLE, folder)
    write_sheet(os.path.join(folder, 'Swim-Anim.png'), 2)
    write_sheet(os.path.join(folder, 'Jump-Anim.png'), 2)
    data = open(os.path.join(folder, 'AnimData.xml')).read()
    data = data.replace('</Anims>', anim_xml('Swim', 2, 8) + anim_xml('Jump', 2, 6) + '</Anims>')
    open(os.path.join(folder, 'AnimData.xml'), 'w').write(data)
    meta_path = os.path.join(folder, 'pet.json')
    meta = json.load(open(meta_path))
    meta['scene']['water'] = [[200, 200], [260, 200], [260, 230], [200, 230]]
    meta['scene']['water_depth'] = 6
    json.dump(meta, open(meta_path, 'w'))
    egg = os.path.join(tmp, 'swimmer.egg')
    r = run_packer(folder, '-o', egg)
    why = '' if r.returncode == 0 else f' ({(r.stdout + r.stderr)[-200:]})'
    if check(r.returncode == 0, 'a scene with water packs' + why):
        d = description(egg)
        names = [a['name'] for a in d['anims']]
        check(d['scene'].get('water') == meta['scene']['water'] and d['scene'].get('water_depth') == 6,
              "the water and its depth are in the egg's scene")
        check('Swim' in names and 'Jump' in names, f'its Swim and Jump are packed though no look plays them ({names})')
    for bad, words in (({'water': [[1, 2], [3, 4]]}, 'scene water: must be a polygon'),
                       ({'water_depth': 2.5}, 'scene water_depth: must be a whole number')):
        meta['scene'].update(bad)
        json.dump(meta, open(meta_path, 'w'))
        r = run_packer(folder, '--check')
        check(r.returncode != 0 and words in r.stdout, f'{bad} is refused ({words})')
        meta['scene'].update(water=[[200, 200], [260, 200], [260, 230], [200, 230]], water_depth=6)


def throw_kit_packed(tmp):
    """The kit's animations go into the egg although no look plays them, and the kit goes into its description;
    without a kit they are left out. The simulator loads it."""
    folder = os.path.join(tmp, 'tosser')
    kit = {'anim': 'Flick', 'item': 'Pebble', 'catch': 'Snatch', 'flight': 'toss', 'release': [14, 9], 'plays': True}
    write_thrower(folder, kit)
    pets = os.path.join(tmp, 'tosser-pets')
    egg = os.path.join(pets, 'tosser.egg')
    r = run_packer(folder, '-o', egg)
    if not check(r.returncode == 0, 'a pet with a throw kit packs (' + (r.stdout + r.stderr).strip()[-200:] + ')'):
        return
    d = description(egg)
    names = sorted(a['name'] for a in d['anims'])
    check(names == ['Flick', 'Idle', 'Pebble', 'Snatch', 'Walk'],
          f'the throw, item and catch are packed though no look plays them; Lob and Fling are left out ({names})')
    check(d.get('throw') == kit, f"the kit is in the egg's description ({d.get('throw')})")
    plain = os.path.join(tmp, 'no-kit')
    write_thrower(plain, None)
    plain_egg = os.path.join(tmp, 'no-kit.egg')
    r = run_packer(plain, '-o', plain_egg)
    d = description(plain_egg) if r.returncode == 0 else {'anims': []}
    names = sorted(a['name'] for a in d['anims'])
    check(names == ['Idle', 'Walk'] and not d.get('throw'),
          f'without a kit they are left out ({names})')
    simulator = os.path.join(BUILD, 'nestbox-sim')
    if os.path.exists(simulator):
        check(simulator_draws(simulator, pets, tmp), 'the simulator loads the egg with its kit and draws it')


def nibble_is_reproducible(tmp):
    """The shipped Nibble egg is what its source folder packs into."""
    egg = os.path.join(tmp, 'nibble.egg')
    r = run_packer(NIBBLE, '-o', egg)
    with open(os.path.join(ROOT, 'pets', 'retro', 'nibble.egg'), 'rb') as f:
        shipped = f.read()
    packed = b''
    if r.returncode == 0:
        with open(egg, 'rb') as f:
            packed = f.read()
    check(packed == shipped, 'pets/retro/nibble/ packs into pets/retro/nibble.egg exactly (else repack it: python3 '
          'tools/eggpack/eggpack.py pets/retro/nibble -o pets/retro/nibble.egg)')


def main():
    with tempfile.TemporaryDirectory() as tmp:
        blob_packs(tmp)
        zoomies_reported(tmp)
        pace_carried(tmp)
        blocked_carried(tmp)
        gauges_carried(tmp)
        ambient_checked(tmp)
        throw_kit_checked(tmp)
        throw_kit_packed(tmp)
        free_time_checked(tmp)
        water_checked(tmp)
        nibble_is_reproducible(tmp)
    finish()


if __name__ == '__main__':
    main()
