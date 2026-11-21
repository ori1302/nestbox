"""The pet-making tools: petrecord records Nibble's looks and a pastime frame by frame, with its timeline; where
Pillow is installed, tools/studio draws a layout, splits a work spot out of a scene, makes a night picture, a contact
sheet and a GIF, and draws a cued part along a recorded pastime.

    python3 tests/studio_test.py [--build DIR]
"""
import csv
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile

from harness import ROOT, build_dir, check, finish

BUILD = build_dir()
RECORD = os.path.join(BUILD, 'petrecord')
NIBBLE_EGG = os.path.join(ROOT, 'pets', 'retro', 'nibble.egg')
NIBBLE = os.path.join(ROOT, 'pets', 'retro', 'nibble')
STUDIO = os.path.join(ROOT, 'tools', 'studio')


def record(*args):
    r = subprocess.run([RECORD, NIBBLE_EGG, *args], capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


work = tempfile.mkdtemp()
try:
    code, out, _ = record('list')
    pastimes = out.split('pastimes:\n')[1].split('\n') if 'pastimes:' in out else []
    pastimes = [p.strip() for p in pastimes if p.strip()]
    check(code == 0 and 'type_fast' in out and pastimes, 'petrecord lists the looks and Nibble\'s pastimes')

    frames = os.path.join(work, 'look')
    code, out, _ = record('look', 'type_fast', '--seconds', '1', '--out', frames)
    look_timeline = out
    rows = list(csv.DictReader(io.StringIO(out)))
    check(code == 0 and len(rows) >= 25 and all(os.path.exists(os.path.join(frames, r['file'])) for r in rows),
          'a look is recorded as a frame and a timeline row every 33 ms')

    with_part = next((p for p in pastimes if p != 'a nap in the sun'), pastimes[0])
    code, out, _ = record('pastime', with_part)
    rows = list(csv.DictReader(io.StringIO(out)))
    shown = [r for r in rows if r['part']]
    check(code == 0 and rows and rows[-1]['phase'] in ('acting', 'turning', 'walking', 'free'),
          f'a pastime ("{with_part}") is recorded through to its end')
    check(bool(shown) and (int(shown[0]['part_ms']) < 100 and all(int(a['part_ms']) <= int(b['part_ms'])
                                                              for a, b in zip(shown, shown[1:]))),
          'the timeline counts the cued part from when it began')
    code, _, err = record('pastime', 'no such pastime')
    check(code == 2 and 'list' in err, 'an unknown pastime is refused, pointing at the list')

    def brightness(path):
        data = open(path, 'rb').read()
        head = data.split(b'\n', 3)
        pixels = head[3]
        return sum(pixels) / len(pixels)
    night = os.path.join(work, 'night')
    code, out, _ = record('look', 'idle', '--seconds', '3', '--hour', '23', '--out', night)
    shots = [r['file'] for r in csv.DictReader(io.StringIO(out))]
    first, last = brightness(os.path.join(night, shots[0])), brightness(os.path.join(night, shots[-1]))
    check(code == 0 and abs(first - last) < 0.05 * last,
          'a recording at night starts on the night picture, not a fade from day')

    try:
        import PIL  # noqa: F401
    except ImportError:
        check(not os.environ.get('CI'), 'CI has Pillow for the studio tools')
        print('studio tools skipped: Pillow is not installed')
        finish()

    sys.path.insert(0, STUDIO)
    from PIL import Image
    import parts

    studio = lambda *a: subprocess.run([sys.executable, os.path.join(STUDIO, 'studio.py'), *a], capture_output=True,
                                       text=True)
    r = studio('layout', NIBBLE, '-o', os.path.join(work, 'layout.png'))
    with open(os.path.join(NIBBLE, 'pet.json')) as f:
        scene_width = Image.open(os.path.join(NIBBLE, json.load(f)['scene']['background'])).width
    check(r.returncode == 0 and Image.open(os.path.join(work, 'layout.png')).width == scene_width * 3,
          'layout draws Nibble\'s scene with its floor and spots, three times its size')
    r = studio('layout', os.path.join(NIBBLE, json.load(open(os.path.join(NIBBLE, 'pet.json')))['scene']['background']),
               '-o', os.path.join(work, 'grid.png'))
    check(r.returncode == 0, 'layout draws a grid over a bare picture too')

    # a desk drawn on plain ground comes out alone, its screen dark in "off" and its small blue key left as it is
    scene = Image.new('RGBA', (60, 40), (120, 180, 90, 255))
    for x in range(20, 40):          # a dark frame, the screen above a divider, a grey base with one blue key
        for y in range(10, 30):
            edge = x in (20, 39) or y in (10, 20, 29)
            colour = (40, 40, 50, 255) if edge else (120, 190, 240, 255) if y < 20 else (90, 90, 100, 255)
            scene.putpixel((x, y), colour)
    for x in range(23, 25):
        for y in range(23, 25):
            scene.putpixel((x, y), (110, 170, 230, 255))
    scene.save(os.path.join(work, 'scene.png'))
    preview = os.path.join(work, 'desk-preview.png')
    r = studio('workspot', os.path.join(work, 'scene.png'), '--outline', '16,6 44,6 44,34 16,34',
               '--ground', '0,0,10,10', '-o', os.path.join(work, 'desk'), '--preview', preview)
    on = Image.open(os.path.join(work, 'desk', 'workspot_on.png'))
    off = Image.open(os.path.join(work, 'desk', 'workspot_off.png')).convert('RGBA')
    check(r.returncode == 0 and on.size == (20, 20) and '"at": [20, 10]' in r.stdout and
          off.getpixel((10, 5))[2] < 120 and on.getpixel((10, 5))[2] > 200,
          'workspot cuts the desk from its ground and darkens its screen for "off"')
    check(off.getpixel((3, 13))[:3] == (110, 170, 230),
          'only the screen goes dark: a small blue patch elsewhere on the desk stays')
    check(os.path.exists(preview) and sorted(os.listdir(os.path.join(work, 'desk'))) ==
          ['workspot_off.png', 'workspot_on.png'], 'the check picture goes where asked, not into the pet folder')
    r = studio('workspot', os.path.join(work, 'scene.png'), '--outline', '16,6 44,6 44,34 16,34',
               '--ground', '0,0,30,12', '--keep', '25,12,30,16', '-o', os.path.join(work, 'desk2'),
               '--preview', preview)
    check(r.returncode == 0 and 'both in --keep and --ground' in r.stdout,
          'workspot says when a colour is both ground and kept')
    r = studio('night', os.path.join(work, 'scene.png'), '-o', os.path.join(work, 'night.png'))
    day, dark = Image.open(os.path.join(work, 'scene.png')), Image.open(os.path.join(work, 'night.png'))
    check(r.returncode == 0 and sum(dark.convert('L').getdata()) < sum(day.convert('L').getdata()) * 0.7,
          'night darkens a picture by its brightness')

    open(os.path.join(frames, 'timeline.csv'), 'w').write(look_timeline)
    r1 = studio('sheet', frames, '-o', os.path.join(work, 'sheet.png'), '--follow', '96,96')
    r2 = studio('gif', frames, '-o', os.path.join(work, 'look.gif'))
    check(r1.returncode == 0 and r2.returncode == 0 and Image.open(os.path.join(work, 'look.gif')).n_frames >= 10,
          'a recording becomes a contact sheet and a GIF')
    r = studio('sheet', frames, '-o', os.path.join(work, 'paged.png'), '--per-page', '10')
    pages = sorted(f for f in os.listdir(work) if f.startswith('paged-'))
    recorded = len(list(csv.DictReader(io.StringIO(look_timeline))))
    check(r.returncode == 0 and len(pages) == -(-recorded // 10) and f'{recorded} frames' in r.stdout,
          'a sheet shows every frame, over as many pages as it takes')

    # a part drawn along a recording: frames merge, the tick grows to fit 64 frames, pet.json gets its entry
    timeline = os.path.join(work, 'pastime.csv')
    open(timeline, 'w').write(record('pastime', with_part)[1])
    folder = os.path.join(work, 'nibble')
    shutil.copytree(NIBBLE, folder)
    named = {r['part'] for r in csv.DictReader(open(timeline)) if r['part']}
    if check(bool(named), 'the pastime shows a scene part to draw along'):
        rec = parts.Timeline(timeline, sorted(named)[0])
        dot = Image.new('RGBA', (3, 3), (255, 0, 0, 255))
        part = parts.Part('testdot', (0, 0, 200, 200), tick=1)

        def draw(frame, t):                              # a dot crossing the box, so no two frames match
            part.stamp(frame, dot, (t * 60) % 200, rec.at(t).head[1] % 200)
        part.draw(rec.end, draw)
        parts.cue(folder, [part.save(folder)])
        meta = json.load(open(os.path.join(folder, 'pet.json')))
        entry = next(a for a in meta['scene']['ambient'] if a.get('name') == 'testdot')
        check(len(part.frames) <= parts.MAX_FRAMES and part.tick > 1 and len(entry['durations']) == entry['frames'],
              'a part drawn along a pastime fits 64 frames and is cued in pet.json')
        check(set(part.save(folder, ('day', 'night'))) >= {'day', 'night'} and parts.percent(140, 60) == [50.0, 25.0],
              'a part can be saved for several pictures of the day; percent() turns pixels into step positions')

        # a pastime that may play at night, showing a part with only day frames: the packer warns
        meta['free_time'] = [{'name': 'a test', 'steps': [{'at': [50, 70], 'do': 'Idle', 'for': 1,
                                                            'show': 'testdot'}]}]
        json.dump(meta, open(os.path.join(folder, 'pet.json'), 'w'))
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'eggpack', 'eggpack.py'), folder, '--check'],
                           capture_output=True, text=True, env=dict(os.environ, NESTBOX_BUILD_DIR=BUILD))
        night = bool(meta['scene'].get('night'))
        check(night and 'no night frames' in r.stdout,
              'the packer warns about a part that would not show at night')
        meta['free_time'][0]['hours'] = [9, 17]
        json.dump(meta, open(os.path.join(folder, 'pet.json'), 'w'))
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'eggpack', 'eggpack.py'), folder, '--check'],
                           capture_output=True, text=True, env=dict(os.environ, NESTBOX_BUILD_DIR=BUILD))
        check('no night frames' not in r.stdout, 'and not when the pastime keeps to daytime hours')

        # step points: a percent of the scene, on the floor
        packer = lambda: subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'eggpack', 'eggpack.py'), folder,
                                         '--check'], capture_output=True, text=True,
                                        env=dict(os.environ, NESTBOX_BUILD_DIR=BUILD)).stdout
        home = meta['scene']['home']
        w, h = Image.open(os.path.join(folder, meta['scene']['background'])).size
        good = parts.percent(home[0], home[1], w, h)
        for at, warned, what in ((good, False, 'a point on the floor passes'),
                                 ([150, 120], True, 'a step "at" given in pixels is caught'),
                                 ([1, 1], True, 'a step point off the floor is caught')):
            meta['free_time'][0]['steps'][0]['at'] = at
            json.dump(meta, open(os.path.join(folder, 'pet.json'), 'w'))
            out = packer()
            check(('is outside 0-100' in out or 'off the floor' in out) == warned, what)

        r = studio('review', folder, '-o', os.path.join(work, 'review'), '--build', BUILD, '--looks', 'idle',
                   '--pastimes', 'none', '--seconds', '1')
        page = open(os.path.join(work, 'review', 'index.html')).read() if r.returncode == 0 else ''
        check(page.count('<h2>') == 1 and 'look: idle' in page, 'review records only the looks and pastimes asked for')

        unused = os.path.join(folder, 'not_used.png')
        Image.new('RGBA', (4, 4), (1, 2, 3, 255)).save(unused)
        r = studio('colours', folder)
        check(r.returncode == 0 and 'not_used.png' not in r.stdout,
              'colours counts only the pictures the pet uses')
finally:
    shutil.rmtree(work, ignore_errors=True)
finish()
