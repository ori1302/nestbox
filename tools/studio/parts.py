"""Cued scene parts drawn along a pet's recorded pastime: a ball that lands where its mouth will be, a fly that circles
its head, leaves that fall where it looks. Record the pastime with petrecord, then draw the part against the timeline,
so the part and the pet stay in step:

    from parts import Timeline, Part, cue
    rec = Timeline('seed.csv', 'seed')                # the rows while the part "seed" shows; t: seconds into it
    shoot = rec.first('Shoot')
    part = Part('seed', box=(28, 99, 170, 200))       # where the part may draw, scene pixels
    def draw(frame, t):
        if t > shoot:
            part.stamp(frame, sprout, *rec.at(t).feet)
    part.draw(rec.end, draw)
    cue('pet-folder', [part.save('pet-folder')])      # the picture, and its entry in pet.json

Import it with tools/studio on the path (sys.path.insert(0, 'tools/studio')). A part's frames hold for `tick` ticks
(1/60 s); a frame the same as the one before only lengthens it. If the part needs more than 64 frames, the tick grows
until it fits. A part shows only while the scene shows a picture of the day it has frames for: save it for 'day'
(the default) and give its pastime daytime hours, or save a night version too. Needs Pillow."""
import csv
import json
import math
import os
from dataclasses import dataclass

from PIL import Image

MAX_FRAMES = 64          # a part's frames, at most
MAX_SIDE = 256           # a part's box, either side
MAX_TICKS = 240          # one frame's hold, kept under the format's 255


@dataclass
class Row:
    t: float             # seconds since the part began showing
    feet: tuple          # (x, y) screen pixels, as petrecord records them (see Part: a part draws in scene pixels)
    head: tuple          # the top of the head as drawn, screen pixels
    row: int             # facing, 0-7 (0 front, 2 right, 4 back, 6 left)
    phase: str           # walking, turning, acting, free
    anim: str            # what plays: "Walk", "Shoot", "Idle turning", "Flick frames 2-2", ...
    frame: int           # the animation's frame


class Timeline:
    """A pastime as petrecord recorded it, from when part `name` began showing."""

    def __init__(self, path, name):
        with open(path, newline='') as f:
            self.rows = [Row(int(r['part_ms']) / 1000, (float(r['feet_x']), float(r['feet_y'])),
                             (float(r['head_x']), float(r['head_y'])), int(r['row']), r['phase'], r['anim'],
                             int(r['frame']))
                         for r in csv.DictReader(f) if r['part'] == name and r['part_ms']]
        if not self.rows:
            raise ValueError(f'{path}: part "{name}" never shows')
        self.end = self.rows[-1].t + 1 / 30

    def at(self, t):
        """The recorded row nearest t."""
        return min(self.rows, key=lambda r: abs(r.t - t))

    def first(self, anim, after=0.0):
        """When an animation first plays (its name starts with anim), at or after `after` seconds."""
        return next(r.t for r in self.rows if r.anim.startswith(anim) and r.t >= after)

    def last(self, anim):
        return max(r.t for r in self.rows if r.anim.startswith(anim))

    def spans(self, anim):
        """[(start, end)] of each run of rows playing anim."""
        out, start, prev = [], None, None
        for r in self.rows:
            if r.anim.startswith(anim):
                if start is None:
                    start = r.t
                prev = r.t
            elif start is not None:
                out.append((start, prev + 1 / 30))
                start = None
        if start is not None:
            out.append((start, prev + 1 / 30))
        return out


def percent(x, y, width=280, height=240):
    """A pastime step's "at" for scene pixel (x, y): steps place the pet in percent of the scene."""
    return [round(100 * x / width, 1), round(100 * y / height, 1)]


def frames_of(path, count, scale=1):
    """The frames of a strip picture (count frames side by side), each scaled by `scale` (whole pixels)."""
    im = Image.open(path).convert('RGBA')
    w = im.width // count
    return [im.crop((i * w, 0, i * w + w, im.height)).resize((w * scale, im.height * scale), Image.NEAREST)
            for i in range(count)]


class Part:
    """A cued part: its box and every position it draws at are scene pixels (the scene picture's own). A recording's
    positions are screen pixels; they are the same for a 280x240 scene at scene scale 1 on a 280x240 screen. For
    another size or scale, turn a screen point into the scene's with (x + offset) / scale (the offset centres the
    scene on the screen). Effects drawn on a part are scene pixels too: scale them by the pet's own scale
    (frames_of(..., scale=PET_SCALE)) so their pixels match the pet's."""

    def __init__(self, name, box, tick=4):
        x0, y0, x1, y1 = box
        if not (0 < x1 - x0 <= MAX_SIDE and 0 < y1 - y0 <= MAX_SIDE):
            raise ValueError(f'part {name}: box {box} must be 1 to {MAX_SIDE} pixels a side')
        self.name, self.box, self.tick = name, tuple(box), tick
        self.frames, self.ticks = [], []

    def stamp(self, frame, image, x, y, flip=False):
        """image with its middle at scene pixel (x, y); anything outside the box is cut."""
        if flip:
            image = image.transpose(Image.FLIP_LEFT_RIGHT)
        left, top = round(x - image.width / 2) - self.box[0], round(y - image.height / 2) - self.box[1]
        layer = Image.new('RGBA', frame.size)
        layer.paste(image, (left, top))
        frame.alpha_composite(layer)

    def dot(self, frame, x, y, colour):
        x, y = round(x) - self.box[0], round(y) - self.box[1]
        if 0 <= x < frame.width and 0 <= y < frame.height:
            frame.putpixel((x, y), tuple(colour[:3]) + (255,))

    def draw(self, seconds, draw):
        """draw(frame, t) for each frame: t the seconds since the part began, at the frame's middle."""
        tick = self.tick
        while True:
            frames, ticks = [], []
            for i in range(math.ceil(seconds * 60 / tick)):
                f = Image.new('RGBA', (self.box[2] - self.box[0], self.box[3] - self.box[1]))
                draw(f, (i + 0.5) * tick / 60)
                if frames and ticks[-1] + tick <= MAX_TICKS and f.tobytes() == frames[-1].tobytes():
                    ticks[-1] += tick
                else:
                    frames.append(f)
                    ticks.append(tick)
            if len(frames) <= MAX_FRAMES:
                break
            tick += 1
        self.frames, self.ticks, self.tick = frames, ticks, tick
        return self

    def save(self, folder, pictures=('day',)):
        """Writes NAME.png into the pet folder; returns its ambient entry, cued once, with frames for these pictures of
        the day ('day', 'morning', 'afternoon', 'evening', 'night'; the scene's own names for its pictures)."""
        if not self.frames:
            raise ValueError(f'part {self.name}: nothing drawn')
        w, h = self.frames[0].size
        strip = Image.new('RGBA', (w * len(self.frames), h))
        for i, f in enumerate(self.frames):
            strip.alpha_composite(f, (i * w, 0))
        strip.save(os.path.join(folder, f'{self.name}.png'))
        entry = {'name': self.name, 'cue': 'once', 'at': [self.box[0], self.box[1]], 'frames': len(self.frames),
                 'durations': self.ticks}
        entry.update({p: f'{self.name}.png' for p in ([pictures] if isinstance(pictures, str) else pictures)})
        return entry


def cue(folder, entries):
    """Puts cued parts into pet.json's scene, replacing any of the same names; other parts stay."""
    path = os.path.join(folder, 'pet.json')
    meta = json.load(open(path))
    names = {e['name'] for e in entries}
    scene = meta.setdefault('scene', {})
    scene['ambient'] = [a for a in scene.get('ambient', []) if a.get('name') not in names] + list(entries)
    json.dump(meta, open(path, 'w'), indent=2, ensure_ascii=False)


def placeholder(folder, name):
    """A blank picture for a part not drawn yet, so the folder packs and the pastime can be recorded first."""
    path = os.path.join(folder, f'{name}.png')
    if not os.path.exists(path):
        Image.new('RGBA', (2, 1)).save(path)
    return {'name': name, 'cue': 'once', 'at': [0, 0], 'frames': 2, 'durations': [6, 6], 'day': f'{name}.png'}


if __name__ == '__main__':
    print(__doc__)
