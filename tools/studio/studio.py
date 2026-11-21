"""Helpers for making a pet (https://nestbox.pet/docs/pet-folder/): see a scene's layout, split its desk out, make
its night, keep the colours under the egg's limit, and review the pet frame by frame as the device shows it. Needs
Pillow.

    studio.py layout FOLDER|PICTURE [-o layout.png]      the scene with its floor, blocked parts, spots and a grid
    studio.py workspot PICTURE --outline "x,y ..." [--ground BOX] [--keep BOX] -o FOLDER
                                                          a desk or laptop drawn in the scene, as work spot pictures,
                                                          and workspot_preview.png here to check it
    studio.py night PICTURE -o NIGHT.png                  a moonlit version, by brightness
    studio.py colours FOLDER                              the colours of the pictures it uses (an egg holds 255)
    studio.py merge N PICTURE...                          one shared palette of N colours for these pictures
    studio.py sheet FRAMES [-o sheet.png]                 contact sheets of a petrecord recording, every frame
    studio.py gif FRAMES [-o out.gif]                     an animated GIF of it
    studio.py review FOLDER|EGG -o DIR                    pack, record every look and pastime, sheets, GIFs, a page

FRAMES is a folder petrecord saved frames into (--out), with its timeline written next to them as timeline.csv.
Positions are scene pixels: at scene scale 1 they are screen pixels too. (A pastime step's "at" is a percent of the
scene: 100 * x / width, 100 * y / height.)"""
import argparse
import colorsys
import csv
import glob
import html
import json
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PACKER = os.path.join(ROOT, 'tools', 'eggpack', 'eggpack.py')


# ---- layout

def points(s):
    return [tuple(int(v) for v in p.split(',')) for p in s.split()]


def layout(args):
    target = args.target
    if os.path.isdir(target):
        meta = json.load(open(os.path.join(target, 'pet.json')))
        sc = meta.get('scene') or {}
        if not sc.get('background'):
            sys.exit(f'{target}: no scene in pet.json')
        pic = Image.open(os.path.join(target, sc['background'])).convert('RGBA')
        ws = sc.get('workspot')
        if ws and ws.get('on'):
            pic.alpha_composite(Image.open(os.path.join(target, ws['on'])).convert('RGBA'), tuple(ws['at']))
    else:
        sc, ws, pic = {}, None, Image.open(target).convert('RGBA')
    k = args.scale
    out = pic.resize((pic.width * k, pic.height * k), Image.NEAREST)
    d = ImageDraw.Draw(out, 'RGBA')
    for x in range(0, pic.width, 10):
        d.line((x * k, 0, x * k, out.height), fill=(255, 0, 0, 160 if x % 50 == 0 else 50))
    for y in range(0, pic.height, 10):
        d.line((0, y * k, out.width, y * k), fill=(255, 0, 0, 160 if y % 50 == 0 else 50))
    for x in range(0, pic.width, 50):
        d.text((x * k + 2, 2), str(x), fill=(0, 0, 0, 255))
    for y in range(50, pic.height, 50):
        d.text((2, y * k + 2), str(y), fill=(0, 0, 0, 255))
    poly = lambda pts, colour, width=2: d.line([(x * k, y * k) for x, y in list(pts) + [pts[0]]], fill=colour,
                                               width=width) if len(pts) > 2 else None
    spot = lambda p, colour, label: (d.ellipse((p[0] * k - 4, p[1] * k - 4, p[0] * k + 4, p[1] * k + 4), fill=colour),
                                     d.text((p[0] * k + 6, p[1] * k - 6), label, fill=colour))
    if sc.get('floor'):
        poly(sc['floor'], (255, 40, 40, 255), 3)
    for hole in sc.get('blocked') or []:
        poly(hole, (40, 80, 255, 255))
    if sc.get('water'):
        poly(sc['water'], (0, 200, 220, 255))
    if sc.get('keep'):
        x0, y0, x1, y1 = sc['keep']
        d.rectangle((x0 * k, y0 * k, x1 * k, y1 * k), outline=(255, 220, 0, 255))
    for a in sc.get('ambient') or []:
        x, y = a['at']
        d.text((x * k + 2, y * k + 2), a.get('name', 'part'), fill=(80, 80, 80, 255))
    if ws:
        x, y = ws['at']
        im = Image.open(os.path.join(target, ws['on']))
        d.rectangle((x * k, y * k, (x + im.width) * k, (y + im.height) * k), outline=(0, 170, 0, 255))
    for key, colour in (('home', (230, 0, 230, 255)), ('watch', (0, 150, 0, 255)), ('target', (255, 120, 0, 255))):
        if sc.get(key):
            spot(sc[key], colour, key)
    out.save(args.o)
    print(args.o)


# ---- work spot

def specks(im, smallest=6):
    """Clears islands of fewer than `smallest` opaque pixels (8-connected): ground left loose round an object."""
    px, seen = im.load(), set()
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y][3] and (x, y) not in seen:
                island, todo = [], [(x, y)]
                seen.add((x, y))
                while todo:
                    a, b = todo.pop()
                    island.append((a, b))
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            c = (a + dx, b + dy)
                            if 0 <= c[0] < im.width and 0 <= c[1] < im.height and c not in seen and px[c][3]:
                                seen.add(c)
                                todo.append(c)
                if len(island) < smallest:
                    for c in island:
                        px[c] = (0, 0, 0, 0)


def split_workspot(pic, outline, grounds=(), keeps=(), tolerance=8):
    """(on, off, (x, y)): the object inside `outline` (a polygon traced round it, scene pixels) cut out of picture
    `pic`. Ground still inside the outline is cleaned off where it shows at the outline's edge: from the edge inwards,
    over colours seen in the `grounds` boxes (x0, y0, x1, y1) that are not also seen in the `keeps` boxes (the
    object's own colours: its screen, its keys). A desk and what it stands on can share colours, so trace the
    outline tight and keep what belongs to the desk. `off` has the lit screen turned to dark glass."""
    mask = Image.new('L', pic.size)
    ImageDraw.Draw(mask).polygon(outline, fill=255)
    colours = lambda boxes: {pic.getpixel((x, y))[:3] for x0, y0, x1, y1 in boxes
                             for x in range(x0, x1) for y in range(y0, y1)}
    kept = colours(keeps)
    ground = colours(grounds) - kept
    near_ground = lambda c: c not in kept and any(sum((a - b) ** 2 for a, b in zip(c, g)) <= tolerance ** 2
                                                  for g in ground)
    inside = lambda x, y: 0 <= x < pic.width and 0 <= y < pic.height and mask.getpixel((x, y))
    edge = [(x, y) for y in range(pic.height) for x in range(pic.width) if inside(x, y) and
            any(not inside(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    seen, todo = set(), [p for p in edge if near_ground(pic.getpixel(p)[:3])]
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or not inside(x, y) or not near_ground(pic.getpixel((x, y))[:3]):
            continue
        seen.add((x, y))
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    obj = Image.new('RGBA', pic.size)
    for y in range(pic.height):
        for x in range(pic.width):
            p = pic.getpixel((x, y))
            if inside(x, y) and p[3] and (x, y) not in seen:
                obj.putpixel((x, y), p)
    specks(obj)
    box = obj.getbbox()
    if not box:
        raise ValueError('nothing inside the outline differs from the ground')
    on = obj.crop(box)
    off = on.copy()
    for x, y in screen(on):
        p = on.getpixel((x, y))
        lum = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]
        off.putpixel((x, y), (52, 66, 98, 255) if lum > 150 else (36, 46, 74, 255))
    return on, off, (box[0], box[1])


def screen(im):
    """The lit screen's pixels: the biggest patch of bright blue to violet (and any patch at least half its size), so
    bluish keys or a blue mug elsewhere on the desk stay as they are."""
    lit = set()
    for y in range(im.height):
        for x in range(im.width):
            p = im.getpixel((x, y))
            h, s, v = colorsys.rgb_to_hsv(*(c / 255 for c in p[:3]))
            if p[3] and 170 <= h * 360 <= 300 and s > 0.2 and v > 0.5:
                lit.add((x, y))
    patches, seen = [], set()
    for start in lit:
        if start in seen:
            continue
        patch, todo = [], [start]
        seen.add(start)
        while todo:
            x, y = todo.pop()
            patch.append((x, y))
            for c in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if c in lit and c not in seen:
                    seen.add(c)
                    todo.append(c)
        patches.append(patch)
    if not patches:
        return []
    biggest = max(len(p) for p in patches)
    return [c for p in patches if len(p) * 2 >= biggest for c in p]


def preview(pictures, path, zoom=8):
    """The pictures side by side on magenta, zoomed: holes and stray ground show at once."""
    w = sum(p.width + 4 for p in pictures)
    out = Image.new('RGBA', (w, max(p.height for p in pictures)), (255, 0, 255, 255))
    x = 0
    for p in pictures:
        out.alpha_composite(p.convert('RGBA'), (x, 0))
        x += p.width + 4
    out.resize((out.width * zoom, out.height * zoom), Image.NEAREST).save(path)


def workspot(args):
    pic = Image.open(args.picture).convert('RGBA')
    box = lambda g: tuple(int(v) for v in g.split(','))
    seen_in = lambda boxes: {pic.getpixel((x, y))[:3] for x0, y0, x1, y1 in (box(b) for b in boxes or [])
                             for x in range(x0, x1) for y in range(y0, y1)}
    both = seen_in(args.ground) & seen_in(args.keep)
    if both:
        print(f'note: {len(both)} colours are both in --keep and --ground; they stay (a --keep box may reach past the '
              f'desk)')
    try:
        on, off, at = split_workspot(pic, points(args.outline), [box(g) for g in args.ground or []],
                                     [box(k) for k in args.keep or []], args.tolerance)
    except ValueError as e:
        sys.exit(str(e))
    os.makedirs(args.o, exist_ok=True)
    on.save(os.path.join(args.o, 'workspot_on.png'))
    off.save(os.path.join(args.o, 'workspot_off.png'))
    preview([on, off], args.preview)
    lit = sum(1 for a, b in zip(on.getdata(), off.getdata()) if a != b)
    print(json.dumps({'workspot': {'at': list(at), 'on': 'workspot_on.png', 'off': 'workspot_off.png',
                                   'night': 'workspot_night.png'}}))
    print(f'{on.width}x{on.height}, {lit} screen pixels dark in "off"' + ('' if lit else ': none found, darken the '
          'screen by hand') + f'. Check {args.preview}: no holes, no ground left, only the screen dark')


# ---- night

RAMP = [(0.0, (9, 14, 30)), (0.3, (20, 38, 58)), (0.55, (40, 74, 92)), (0.78, (96, 132, 160)), (1.0, (196, 214, 232))]


def moonlit(p, keep):
    if p[3] == 0:
        return p
    lum = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 255
    t = lum ** 1.25
    for (a, ca), (b, cb) in zip(RAMP, RAMP[1:]):
        if t <= b:
            f = (t - a) / (b - a)
            base = tuple(x + (y - x) * f for x, y in zip(ca, cb))
            break
    g = lum * 255
    return tuple(max(0, min(255, round(b + keep * (c - g) * (0.35 + 0.65 * lum))))
                 for b, c in zip(base, p[:3])) + (p[3],)


def night(args):
    pic = Image.open(args.picture).convert('RGBA')
    pic.putdata([moonlit(p, args.keep) for p in pic.getdata()])
    pic.save(args.o)
    print(args.o)


# ---- colours

def used_pictures(folder):
    """The pictures a pet folder uses: every .png pet.json names, and the sheets of the animations in AnimData.xml."""
    meta = json.load(open(os.path.join(folder, 'pet.json')))
    named = set()

    def walk(v):
        if isinstance(v, str) and v.lower().endswith('.png'):
            named.add(v)
        elif isinstance(v, dict):
            for x in v.values():
                walk(x)
        elif isinstance(v, list):
            for x in v:
                walk(x)
    walk(meta)
    data = os.path.join(folder, 'AnimData.xml')
    if os.path.exists(data):
        import re
        named |= {f'{n}-Anim.png' for n in re.findall(r'<Name>(\w+)</Name>', open(data).read())}
    return meta, sorted(os.path.join(folder, n) for n in named if os.path.exists(os.path.join(folder, n)))


def colours(args):
    meta, pictures = used_pictures(args.folder)
    total, per = set(), []
    for path in pictures:
        im = Image.open(path).convert('RGBA')
        cs = {c[:3] for _, c in im.getcolors(1 << 24) if c[3] >= 128}
        per.append((len(cs), os.path.basename(path)))
        total |= cs
    for n, name in sorted(per, reverse=True)[:args.top]:
        print(f'{n:5d}  {name}')
    print(f'{len(total)} colours in the {len(pictures)} pictures {meta.get("name", args.folder)} uses (the packer '
          f'leaves out animations no look plays, so the egg can have fewer; at most 255)')


def merge(args):
    ims = [Image.open(p).convert('RGBA') for p in args.pictures]
    sheet = Image.new('RGB', (max(i.width for i in ims), sum(i.height for i in ims)))
    y = 0
    for im in ims:
        sheet.paste(im.convert('RGB'), (0, y), im)
        y += im.height
    palette = sheet.quantize(args.n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    for im, path in zip(ims, args.pictures):
        q = im.convert('RGB').quantize(palette=palette, dither=Image.Dither.NONE).convert('RGBA')
        q.putalpha(im.getchannel('A').point(lambda a: 255 if a >= 128 else 0))
        q.save(path)
    print(f'{len(ims)} pictures share {args.n} colours')


# ---- recordings

def recording(frames):
    path = os.path.join(frames, 'timeline.csv')
    rows = list(csv.DictReader(open(path))) if os.path.exists(path) else []
    rows = [r for r in rows if r.get('file') and os.path.exists(os.path.join(frames, r['file']))]
    if not rows:
        rows = [{'file': os.path.basename(f), 'anim': '', 'ms': str(i * 33), 'feet_x': '', 'feet_y': '', 'part': ''}
                for i, f in enumerate(sorted(glob.glob(os.path.join(frames, '*.ppm'))))]
    return rows


def framing(rows, args):
    """The crop for every frame: a fixed box, or one that follows the pet's feet."""
    if args.box:
        box = tuple(int(v) for v in args.box.split(','))
        return lambda r: box
    if args.follow and rows and rows[0].get('feet_x'):
        w, h = (int(v) for v in args.follow.split(','))
        return lambda r: (int(float(r['feet_x'])) - w // 2, int(float(r['feet_y'])) - h + h // 5,
                          int(float(r['feet_x'])) + w - w // 2, int(float(r['feet_y'])) + h // 5)
    return lambda r: None


def sheet(args):
    rows = recording(args.frames)[::args.every]
    crop = framing(rows, args)
    tiles = []
    for i, r in enumerate(rows):
        im = Image.open(os.path.join(args.frames, r['file'])).convert('RGB')
        box = crop(r)
        tiles.append((im.crop(box) if box else im, f"{r['file'][1:5]} {r['anim'][:14]}"))
    if not tiles:
        sys.exit(f'{args.frames}: no frames')
    w, h = tiles[0][0].size
    cols = args.cols or max(1, min(len(tiles), 1600 // (w * args.scale)))
    per = args.per_page or len(tiles)
    stem, ext = os.path.splitext(args.o)
    written = []
    for page in range(0, len(tiles), per):
        part = tiles[page:page + per]
        out = Image.new('RGB', (cols * w, ((len(part) + cols - 1) // cols) * h), (0, 0, 0))
        d = ImageDraw.Draw(out)
        for i, (im, label) in enumerate(part):
            x, y = (i % cols) * w, (i // cols) * h
            out.paste(im, (x, y))
            d.rectangle((x, y, x + min(w, 6 * len(label) + 4), y + 10), fill=(255, 255, 255))
            d.text((x + 2, y), label, fill=(200, 0, 0))
        out = out.resize((out.width * args.scale, out.height * args.scale), Image.NEAREST)
        name = args.o if per >= len(tiles) else f'{stem}-{page // per + 1}{ext}'
        out.save(name)
        written.append(name)
    print(f'{", ".join(written)}: {len(tiles)} frames')
    return written


def gif(args):
    rows = recording(args.frames)[::args.every]
    crop = framing(rows, args)
    if not rows:
        sys.exit(f'{args.frames}: no frames')
    frames, ms = [], []
    for i, r in enumerate(rows):
        im = Image.open(os.path.join(args.frames, r['file'])).convert('RGB')
        box = crop(r)
        im = im.crop(box) if box else im
        frames.append(im.resize((im.width * args.scale, im.height * args.scale), Image.NEAREST))
        nxt = rows[i + 1]['ms'] if i + 1 < len(rows) else None
        ms.append(int(nxt) - int(r['ms']) if nxt else 33 * args.every)
    frames[0].save(args.o, save_all=True, append_images=frames[1:], duration=ms, loop=0, optimize=True)
    print(f'{args.o}: {len(frames)} frames')


# ---- review

def moving_box(frames, pad=10):
    """x0,y0,x1,y1 round everything that changes during a recording (the pet and its parts), padded."""
    from PIL import ImageChops
    rows = recording(frames)
    first = Image.open(os.path.join(frames, rows[0]['file'])).convert('RGB')
    box = None
    for r in rows[1:]:
        b = ImageChops.difference(first, Image.open(os.path.join(frames, r['file'])).convert('RGB')).getbbox()
        if b:
            box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    if box is None:
        return None
    return (f'{max(0, box[0] - pad)},{max(0, box[1] - pad)},'
            f'{min(first.width, box[2] + pad)},{min(first.height, box[3] + pad)}')


def find_petrecord(build):
    for d in [build, os.environ.get('NESTBOX_BUILD_DIR'), os.path.join(ROOT, 'build')]:
        if d and os.path.exists(os.path.join(d, 'petrecord')):
            return os.path.join(d, 'petrecord')
    sys.exit('petrecord not found: build it (cmake --build BUILD --target petrecord) and pass --build BUILD')


def review(args):
    os.makedirs(args.o, exist_ok=True)
    egg = args.target
    if os.path.isdir(egg):
        egg = os.path.join(args.o, 'pet.egg')
        env = dict(os.environ, NESTBOX_BUILD_DIR=args.build) if args.build else None
        r = subprocess.run([sys.executable, PACKER, args.target, '-o', egg], capture_output=True, text=True, env=env)
        print((r.stdout + r.stderr).strip())
        if r.returncode:
            sys.exit('the folder does not pack: fix it first (eggpack.py FOLDER --check)')
    rec = find_petrecord(args.build)
    listing = subprocess.run([rec, egg, 'list'], capture_output=True, text=True, check=True).stdout.splitlines()
    looks = listing[2].split()
    pastimes = [l.strip() for l in listing[listing.index('pastimes:') + 1:]]
    pick = lambda chosen, every: every if chosen is None else [] if chosen == 'none' else chosen.split(',')
    items = [('look', l, ['--seconds', str(args.seconds)]) for l in pick(args.looks, looks)]
    items += [('pastime', p, []) for p in pick(args.pastimes, pastimes)]
    page = [f'<!doctype html><meta charset="utf-8"><title>{html.escape(os.path.basename(args.target))}</title>',
            '<style>body{font:14px system-ui;background:#222;color:#eee;margin:16px}img{image-rendering:pixelated;'
            'max-width:100%}figure{display:inline-block;margin:6px;vertical-align:top}h2{margin-top:28px}</style>']
    for kind, name, extra in items:
        slug = f'{kind}-{name}'.replace(' ', '_').replace('/', '_')
        frames = os.path.join(args.o, slug)
        shutil.rmtree(frames, ignore_errors=True)
        out = subprocess.run([rec, egg, kind, name, '--out', frames, '--hour', str(args.hour), '--every',
                              str(args.every_ms)] + extra, capture_output=True, text=True)
        if out.returncode:
            print(f'{kind} {name}: {out.stderr.strip()}')
            continue
        open(os.path.join(frames, 'timeline.csv'), 'w').write(out.stdout)
        ns = argparse.Namespace(frames=frames, every=1, box=None, follow=None, scale=2, o=f'{frames}.gif', cols=0)
        gif(ns)
        # every frame: a look round the pet with room for its sign; a pastime round all that moves (its parts too)
        look = kind == 'look'
        ns = argparse.Namespace(frames=frames, every=1, box=None if look else moving_box(frames),
                                follow='120,140' if look else None, scale=2, o=f'{frames}-sheet.png',
                                cols=0 if not look else 12, per_page=60 if look else 36)
        sheets = sheet(ns)
        page.append(f'<h2>{kind}: {html.escape(name)}</h2><figure><img src="{slug}.gif"></figure>' +
                    ''.join(f'<figure><img src="{os.path.basename(s)}"></figure>' for s in sheets))
    open(os.path.join(args.o, 'index.html'), 'w').write('\n'.join(page))
    print(os.path.join(args.o, 'index.html'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('layout'); p.add_argument('target'); p.add_argument('-o', default='layout.png')
    p.add_argument('--scale', type=int, default=3); p.set_defaults(fn=layout)
    p = sub.add_parser('workspot'); p.add_argument('picture'); p.add_argument('--outline', required=True)
    p.add_argument('--ground', action='append', help='x0,y0,x1,y1: ground round the desk (repeat for each kind)')
    p.add_argument('--keep', action='append', help="x0,y0,x1,y1: part of the desk whose colours always stay")
    p.add_argument('--tolerance', type=int, default=8,
                   help='how far (RGB distance) a colour may be from a ground colour and still count as ground')
    p.add_argument('--preview', default='workspot_preview.png', help='where the zoomed check picture goes')
    p.add_argument('-o', required=True); p.set_defaults(fn=workspot)
    p = sub.add_parser('night'); p.add_argument('picture'); p.add_argument('-o', required=True)
    p.add_argument('--keep', type=float, default=0.22, help='how much of the day colour stays (0-1)')
    p.set_defaults(fn=night)
    p = sub.add_parser('colours'); p.add_argument('folder'); p.add_argument('--top', type=int, default=12)
    p.set_defaults(fn=colours)
    p = sub.add_parser('merge'); p.add_argument('n', type=int); p.add_argument('pictures', nargs='+')
    p.set_defaults(fn=merge)
    for name, fn, default in (('sheet', sheet, 'sheet.png'), ('gif', gif, 'out.gif')):
        p = sub.add_parser(name); p.add_argument('frames'); p.add_argument('-o', default=default)
        p.add_argument('--every', type=int, default=1, help='every Nth frame')
        p.add_argument('--box', help='x0,y0,x1,y1: crop every frame to this')
        p.add_argument('--follow', help='w,h: a box this big round the pet, following it')
        p.add_argument('--scale', type=int, default=2); p.add_argument('--cols', type=int, default=0)
        if name == 'sheet':
            p.add_argument('--per-page', type=int, default=0, help='frames a sheet, then sheet-2.png and on')
        p.set_defaults(fn=fn)
    p = sub.add_parser('review'); p.add_argument('target'); p.add_argument('-o', required=True)
    p.add_argument('--build'); p.add_argument('--hour', type=int, default=14)
    p.add_argument('--seconds', type=float, default=4); p.add_argument('--every-ms', type=int, default=33)
    p.add_argument('--looks', help='only these looks, comma separated ("none": no looks)')
    p.add_argument('--pastimes', help='only these pastimes, comma separated ("none": no pastimes)')
    p.set_defaults(fn=review)
    args = ap.parse_args()
    args.fn(args)


if __name__ == '__main__':
    main()
