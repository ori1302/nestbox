"""New animations made from a pet's own: an effect drawn round an animation it has (sparkles drawn into its Eat, a
splash beside its Flick), saved as a 1-row animation in the folder under the name a look plays first (Busy, Dancing,
Celebrate, Eating, Fighting, WorkFast...; the look table is in https://nestbox.pet/docs/egg-format/):

    from bake import Folder, put, frames_of
    pet = Folder('pets/retro/nibble')
    eat, sparkle = pet.anim('Eat'), frames_of('effects/sparkle.png', 4)
    def draw(canvas, i, t, cx, cy):                    # frame i at tick t, (cx, cy) the canvas middle
        put(canvas, eat.at(t, row=0), cx, cy)          # the pet stays where its own frames put it
        put(canvas, sparkle[i % 4], cx + 14, cy - 18)
    pet.make('Eating', 96, 96, ticks=4, count=eat.ticks // 4, draw=draw)
    pet.save()

Import it with tools/studio on the path (sys.path.insert(0, 'tools/studio')). Frames are drawn centred, as a pet's
frames are, so a bigger canvas leaves the pet where it stands. The canvas is in the pet's sprite pixels (the device
scales the whole animation by the pet's `scale`), so draw effect frames 1:1 here, unscaled, and smaller than the pet.
Needs Pillow."""
import os
import re

from PIL import Image

from parts import frames_of   # noqa: F401  (one helper for both)


class Anim:
    def __init__(self, sheet, w, h, durations, hit):
        self.sheet, self.w, self.h, self.durations, self.hit = sheet, w, h, durations, hit
        self.rows = sheet.height // h
        self.ticks = sum(durations)

    def frame(self, i, row=0):
        r = min(row, self.rows - 1)
        return self.sheet.crop((i * self.w, r * self.h, i * self.w + self.w, r * self.h + self.h))

    def at(self, tick, row=0):
        """The frame showing `tick` ticks in, looping."""
        t = tick % self.ticks
        for i, d in enumerate(self.durations):
            if t < d:
                return self.frame(i, row)
            t -= d
        return self.frame(len(self.durations) - 1, row)

    def hit_tick(self):
        """Ticks to its HitFrame (where a throw or a strike lands), or None."""
        return None if self.hit is None else sum(self.durations[:self.hit])

    def bounds(self, row=0):
        """(left, top, right, bottom) of its first frame's pixels, from the frame's middle."""
        x0, y0, x1, y1 = self.frame(0, row).getbbox()
        return x0 - self.w / 2, y0 - self.h / 2, x1 - self.w / 2, y1 - self.h / 2


class Folder:
    def __init__(self, folder):
        self.folder = folder
        self.path = os.path.join(folder, 'AnimData.xml')
        self.xml = open(self.path).read()

    def anim(self, name):
        m = re.search(r'<Anim>\s*<Name>' + re.escape(name) + r'</Name>(.*?)</Anim>', self.xml, re.S)
        if not m:
            raise KeyError(f'{self.folder} has no animation {name}')
        body = m.group(1)
        copy = re.search(r'<CopyOf>(\w+)</CopyOf>', body)
        if copy:
            return self.anim(copy.group(1))
        num = lambda tag: int(re.search(f'<{tag}>(\\d+)</{tag}>', body).group(1))
        hit = re.search(r'<HitFrame>(\d+)</HitFrame>', body)
        sheet = Image.open(os.path.join(self.folder, f'{name}-Anim.png')).convert('RGBA')
        return Anim(sheet, num('FrameWidth'), num('FrameHeight'),
                    [int(d) for d in re.findall(r'<Duration>(\d+)</Duration>', body)],
                    int(hit.group(1)) if hit else None)

    def make(self, name, w, h, ticks, count, draw):
        """count frames of w x h, each held `ticks`; draw(canvas, i, tick, cx, cy) paints frame i."""
        sheet = Image.new('RGBA', (w * count, h))
        for i in range(count):
            canvas = Image.new('RGBA', (w, h))
            draw(canvas, i, i * ticks, w / 2, h / 2)
            sheet.alpha_composite(canvas, (i * w, 0))
        sheet.save(os.path.join(self.folder, f'{name}-Anim.png'))
        self.xml = re.sub(r'\s*<Anim>\s*<Name>' + re.escape(name) + r'</Name>.*?</Anim>', '', self.xml, flags=re.S)
        index = len(re.findall(r'<Anim>', self.xml))
        entry = (f'\n\t\t<Anim>\n\t\t\t<Name>{name}</Name>\n\t\t\t<Index>{index}</Index>\n'
                 f'\t\t\t<FrameWidth>{w}</FrameWidth>\n\t\t\t<FrameHeight>{h}</FrameHeight>\n\t\t\t<Durations>\n' +
                 ''.join(f'\t\t\t\t<Duration>{ticks}</Duration>\n' for _ in range(count)) +
                 '\t\t\t</Durations>\n\t\t</Anim>')
        self.xml = self.xml.replace('</Anims>', entry + '\n\t</Anims>', 1)

    def save(self):
        open(self.path, 'w').write(self.xml)


def put(canvas, image, cx, cy, flip=False):
    """image with its middle at (cx, cy) of the canvas."""
    if flip:
        image = image.transpose(Image.FLIP_LEFT_RIGHT)
    layer = Image.new('RGBA', canvas.size)
    layer.paste(image, (round(cx - image.width / 2), round(cy - image.height / 2)))
    canvas.alpha_composite(layer)


if __name__ == '__main__':
    print(__doc__)
