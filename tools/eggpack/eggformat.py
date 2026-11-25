"""Writes NestBox eggs.

An egg is "NEGG", the length of its description (4 bytes, little-endian), the description (JSON), then the
palette, pictures and frames. The description gives each block's offset from the start of the file. The engine's
reader is engine/egg/egg.cpp; https://nestbox.pet/docs/egg-format/ describes the format.

A pet here is a dict:
  name, theme, personality, style, about, color, credit, license   text (may be empty)
  scale        screen pixels per art pixel it was drawn for; 0 = the screen's own
  pace         its walking speed, screen pixels per second; 0 = the engine's own
  private      True: the art may not be redistributed (firmware builds leave it out unless asked)
  notes        {animation or look name: what it looks like}
  throw        None, or {anim, item, catch (optional), flight: hoop, splash or toss, typing_fast: false (optional),
               catch_height (optional, 0-1, default 0.25), release (optional, [x, y] in the throw's frame),
               plays: true (optional)}
  free_time    [{name, hours: [from, to] (optional), steps: [step, ...]}, ...] (optional): its own pastimes
  palette      [RGB565, ...]   index 0 is transparent (its value is unused)
  feet, tall   from Idle
  scene        None, or {w, h, scale (screen pixels per picture pixel, default 1),
                         pictures: {day, morning, afternoon, evening, night: bytes or None},
                         floor: [[x, y], ...], blocked: [[[x, y], ...], ...] or None, home: [x, y],
                         keep: [x0, y0, x1, y1] or None, target: [x, y] or None, watch: [x, y] or None,
                         water: [[x, y], ...] or None, water_depth: 0-64, water_shallow: 0-64,
                         gauges: "top-left", "top-right", "bottom-left", "bottom-right" or None,
                         workspot: None or {w, h, x, y, pictures: {on, off, night}},
                         swish: None or {w, h, x, y, pictures: {day, night}},
                         ambient: [{name, cue: "loop" or "once", during: a look's key,
                                    fill: palette index (all four optional),
                                    w, h, x, y, frames,
                                    durations: [...],
                                    pictures: {day, morning, afternoon, evening, night: bytes}}]}
                                    (each ambient picture is its frames' palette indices, w x h each, one
                                    after another; 0 leaves the scene as it is. Written coded, as frames are)
  anims        [{name, w, h, rows, frames, rush, hit, return, dash, durations: [...], data: bytes}, ...]
"""
import json
import struct

MAGIC = b'NEGG'                   # NestBox egg
HEADER_SIZE = 8                   # magic and description length
MAX_TICKS = 255
DAYPARTS = ('day', 'morning', 'afternoon', 'evening', 'night')
WORDS = ('theme', 'personality', 'style', 'about', 'color', 'credit', 'license')
STRIKE_MARKS = ('rush', 'hit', 'return')
EMPTY_BOX = struct.pack('<4H', 0, 0, 0, 0)


def crop_box(frame, w, h):
    """(x, y, w, h) of the pixels in a frame, or None if it is empty."""
    ys = [y for y in range(h) if any(frame[y * w:(y + 1) * w])]
    if not ys:
        return None
    xs = [x for x in range(w) if any(frame[y * w + x] for y in ys)]
    return xs[0], ys[0], xs[-1] - xs[0] + 1, ys[-1] - ys[0] + 1


# A coded frame's pixels, row by row through its box, as runs. Each run starts with one byte:
# 0x00-0x3F: n + 1 clear pixels; 0x40-0x7F: n + 1 pixels as the row's first frame has them there (not in a row's
# first frame); 0x80-0xBF: n + 2 times the next byte; 0xC0-0xFF: the next n + 1 bytes as they are.
RUN = 64


def code(px, base=None):
    """The run codes for `px` (a box's pixels); `base` holds the row's first frame's pixels at the same places."""
    out, i, n = bytearray(), 0, len(px)
    keep = (lambda j: px[j] and base[j] == px[j]) if base is not None else (lambda j: False)
    while i < n:
        j = i
        if px[i] == 0:
            while j < n and px[j] == 0 and j - i < RUN: j += 1
            out.append(j - i - 1)
        elif keep(i):
            while j < n and keep(j) and j - i < RUN: j += 1
            out.append(0x40 | (j - i - 1))
        else:
            while j < n and px[j] == px[i] and j - i < RUN + 1: j += 1
            if j - i >= 2:
                out += bytes((0x80 | (j - i - 2), px[i]))
            else:
                j = i
                while (j < n and px[j] and not keep(j) and j - i < RUN and
                       not (j + 1 < n and px[j + 1] == px[j] and (j + 2 >= n or px[j + 2] == px[j]))):
                    j += 1
                j = max(j, i + 1)
                out.append(0xC0 | (j - i - 1))
                out += bytes(px[i:j])
        i = j
    return bytes(out)


def coded(a):
    """Frames (an animation's, or a scene part's as one row), each cut to the box that has pixels in it and stored
    as run codes: a table of (x, y, w, h) as 16-bit numbers, one per frame row by row, then each frame's offset in the
    block (32-bit), then the codes. A frame after a row's first is coded against that first frame where that is
    shorter, and frames that come out the same share their codes."""
    w, h, data, rows, frames = a['w'], a['h'], a['data'], a['rows'], a['frames']
    n = rows * frames
    table, offsets, body = bytearray(), [], bytearray()
    start = n * 8 + n * 4
    shared = {}
    for r in range(rows):
        first = data[r * frames * w * h:(r * frames + 1) * w * h]
        for f in range(frames):
            frame = data[(r * frames + f) * w * h:(r * frames + f + 1) * w * h]
            box = crop_box(frame, w, h)
            if box is None:
                table += EMPTY_BOX
                offsets.append(0)
                continue
            x0, y0, bw, bh = box
            table += struct.pack('<4H', x0, y0, bw, bh)
            px = b''.join(frame[y * w + x0:y * w + x0 + bw] for y in range(y0, y0 + bh))
            runs = code(px)
            if f:
                base = b''.join(first[y * w + x0:y * w + x0 + bw] for y in range(y0, y0 + bh))
                patched = code(px, base)
                if len(patched) < len(runs):
                    runs = patched
            key = (r if any(0x40 <= c < 0x80 for c in runs) else -1, box, runs)
            if key not in shared:
                shared[key] = start + len(body)
                body += runs
            offsets.append(shared[key])
    return bytes(table + b''.join(struct.pack('<I', o) for o in offsets) + body)


class Blocks:
    """The blocks after the description, in file order; place() returns a block's offset. A block the same as one
    already placed (a scene part with the same frames for day and night) is stored once, both pointing at it."""

    def __init__(self, base):
        self.end = base
        self.order = []
        self.placed = {}

    def place(self, data):
        data = bytes(data)
        if data in self.placed:
            return self.placed[data]
        offset = self.end
        self.end += len(data)
        self.order.append(data)
        self.placed[data] = offset
        return offset


def describe_pictures(pictures, keys, blocks):
    return {key: blocks.place(pictures[key]) for key in keys if pictures.get(key)}


def describe_overlay(overlay, keys, blocks):
    """A work spot or swish: its size, place and pictures."""
    return {'w': overlay['w'], 'h': overlay['h'], 'x': overlay['x'], 'y': overlay['y'],
            'pictures': describe_pictures(overlay['pictures'], keys, blocks)}


def describe_scene(sc, blocks):
    s = {'w': sc['w'], 'h': sc['h'], 'pictures': {}}
    if sc.get('scale', 1) != 1:
        s['scale'] = sc['scale']
    s['pictures'] = describe_pictures(sc['pictures'], DAYPARTS, blocks)
    s['floor'] = sc.get('floor', [])
    if sc.get('blocked'):
        s['blocked'] = sc['blocked']
    if sc.get('water'):
        s['water'] = sc['water']
        s['water_depth'] = sc.get('water_depth', 0)
        if sc.get('water_shallow'):
            s['water_shallow'] = sc['water_shallow']
    s['home'] = sc.get('home', [-1, -1])
    if sc.get('keep'):
        s['keep'] = sc['keep']
    if sc.get('target'):
        s['target'] = sc['target']
    if sc.get('watch'):
        s['watch'] = sc['watch']
    if sc.get('gauges'):
        s['gauges'] = sc['gauges']
    if sc.get('workspot'):
        s['workspot'] = describe_overlay(sc['workspot'], ('on', 'off', 'night'), blocks)
    if sc.get('swish'):
        s['swish'] = describe_overlay(sc['swish'], ('day', 'night'), blocks)
    if sc.get('ambient'):
        s['ambient'] = [{**({'name': a['name']} if a.get('name') else {}),
                         **({'cue': a['cue']} if a.get('cue') else {}),
                         **({'fill': a['fill']} if a.get('fill') else {}),
                         **({'during': a['during']} if a.get('during') else {}),
                         'w': a['w'], 'h': a['h'], 'x': a['x'], 'y': a['y'], 'frames': a['frames'],
                         'durations': a['durations'],
                         'pictures': {key: blocks.place(coded({'w': a['w'], 'h': a['h'], 'rows': 1,
                                                               'frames': a['frames'], 'data': a['pictures'][key]}))
                                      for key in DAYPARTS if a['pictures'].get(key)}}
                        for a in sc['ambient']]
    return s


def describe_anim(a, blocks):
    e = {'name': a['name'], 'w': a['w'], 'h': a['h'], 'rows': a['rows'], 'frames': a['frames'],
         'durations': [min(MAX_TICKS, t) for t in a['durations']]}
    if a.get('dash'):
        e['dash'] = 1
    for k in STRIKE_MARKS:
        if a.get(k, -1) >= 0:
            e[k] = a[k]
    e['at'] = blocks.place(coded(a))
    return e


def describe(pet, base):
    """The description, as JSON bytes, with every block placed from offset `base`; and the blocks in order."""
    blocks = Blocks(base)
    d = {'name': pet['name']}
    for k in WORDS:
        if pet.get(k):
            d[k] = pet[k]
    if pet.get('notes'):
        d['notes'] = pet['notes']
    if pet.get('scale'):
        d['scale'] = pet['scale']
    if pet.get('pace'):
        d['pace'] = pet['pace']
    if pet.get('private'):
        d['private'] = True
    if pet.get('free_time'):
        d['free_time'] = pet['free_time']
    if pet.get('screen_palette'):
        d['screen_palette'] = pet['screen_palette']
    if pet.get('throw'):
        d['throw'] = {k: pet['throw'][k] for k in ('anim', 'item', 'catch', 'flight') if pet['throw'].get(k)}
        if pet['throw'].get('typing_fast') is False:
            d['throw']['typing_fast'] = False
        if 'catch_height' in pet['throw']:
            d['throw']['catch_height'] = pet['throw']['catch_height']
        if 'release' in pet['throw']:
            d['throw']['release'] = list(pet['throw']['release'])
        if pet['throw'].get('plays') is True:
            d['throw']['plays'] = True
    d['feet'], d['tall'] = pet['feet'], pet['tall']
    palette = b''.join(struct.pack('<H', c or 0) for c in pet['palette'])
    d['palette'] = {'count': len(pet['palette']), 'at': blocks.place(palette)}
    if pet.get('scene'):
        d['scene'] = describe_scene(pet['scene'], blocks)
    d['anims'] = [describe_anim(a, blocks) for a in pet['anims']]
    return json.dumps(d, ensure_ascii=False, separators=(',', ':')).encode(), blocks.order


def write(pet, out_path):
    """Writes the pet as an egg; returns its size in bytes."""
    # The offsets are part of the description, so its length depends on them: place the blocks until it settles.
    text, _ = describe(pet, 0)
    for _ in range(5):
        settled, order = describe(pet, HEADER_SIZE + len(text))
        if len(settled) == len(text):
            break
        text = settled
    else:
        raise RuntimeError('the description length did not settle')
    data = MAGIC + struct.pack('<I', len(settled)) + settled + b''.join(order)
    with open(out_path, 'wb') as f:
        f.write(data)
    return len(data)
