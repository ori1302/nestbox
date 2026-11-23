"""Records the README's animation (assets/nestbox.gif): a short scripted day on a live simulator, shown on the
screen of the orange wedge case. Goose types along with you, calls you over when a coding agent needs an answer,
celebrates a merged pull request, dances to a song and goes to sleep at night. Every screen frame comes from the
engine over the simulator's HTTP API, exactly as the board would draw it.

    python3 tools/demo_gif.py [--build DIR] [--egg pets/goose.egg] [--font Barlow-Medium.ttf] [-o assets/nestbox.gif]

Needs the simulator (macOS): cmake --build build --target nestbox-sim. Needs Pillow.
"""
import argparse
import io
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.request

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
CASE = os.path.join(ROOT, 'assets', 'wedge-orange.png')
# The screen's corners in CASE (pixels), clockwise from top left: where the 280 x 240 picture is drawn.
CORNERS = [(330.42, 202.32), (977.52, 127.53), (1071.24, 685.75), (419.69, 779.79)]
WIDTH = 720                      # the GIF's width; the screen shows at about 1.2 pixels per screen pixel
PAPER = (0xF3, 0xEC, 0xDD)
INK = (0x1D, 0x24, 0x33)
FONT = '/System/Library/Fonts/Helvetica.ttc'   # the captions' default; the docs use Barlow Medium
FRAME_S = 0.08                   # 12.5 frames a second
PREROLL_S = 5                    # before recording: the pet appears and settles
CLAUDE = {'from': 'claude', 'id': 'demo', 'color': 'orange'}

# (from second, caption): the words under the device.
CAPTIONS = [(0, 'You type. It types along.'), (4.0, 'Your agent needs an answer.'),
            (6.8, 'A pull request merges.'), (9.4, 'A song starts.'), (12.2, 'Night. It goes to sleep.')]
LENGTH_S = 18.0
TYPING_S, NIGHT_S = 4.0, 12.2
# Sent during the preroll, so the recording opens with the pet across the meadow from its desk.
AWAY = {'from': 'demo', 'do': {'at': [69.4, 78.3], 'face': 'right', 'for': 1}}
# (second, message): what a coding agent, GitHub and a music player send; `over` clears each one's part.
TIMELINE = [
    (4.0, {**CLAUDE, 'state': 'asking', 'say': 'Claude needs your OK'}),
    (6.6, {**CLAUDE, 'state': 'over'}),
    (6.8, {'from': 'github', 'state': 'done', 'say': '#212 merged', 'effect': 'confetti'}),
    (9.2, {'from': 'github', 'state': 'over'}),
    (9.4, {'from': 'music', 'color': 'green', 'state': 'on', 'do': 'dance', 'effect': 'notes',
           'line': 'Lo-fi Beats - Rain', 'gauge': 40}),
    (12.0, {'from': 'music', 'state': 'over'}),
]


def user(t):
    """What the Mac app reports every quarter second at recording second t: keys pressed, idle time, lock, clock."""
    if t < 0:
        return {'from': 'demo-mac', 'keys': 0, 'idle': 4, 'state': 'awake', 'time': '09:10', 'next': 2}
    if t < TYPING_S:
        return {'from': 'demo-mac', 'keys': 2, 'idle': 0, 'state': 'awake', 'time': '09:10', 'next': 2}
    if t < NIGHT_S:
        return {'from': 'demo-mac', 'keys': 0, 'idle': 4, 'state': 'awake', 'time': '15:30', 'next': 2}
    return {'from': 'demo-mac', 'keys': 0, 'idle': 4, 'state': 'locked', 'time': '22:40', 'next': 2}


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def post(port, body, path='/v1/tell'):
    req = urllib.request.Request(f'http://127.0.0.1:{port}{path}', json.dumps(body).encode(),
                                 {'Content-Type': 'application/json', 'Host': f'127.0.0.1:{port}'})
    urllib.request.urlopen(req, timeout=2).read()


def screen(port):
    req = urllib.request.Request(f'http://127.0.0.1:{port}/screen.bmp', headers={'Host': f'127.0.0.1:{port}'})
    return Image.open(io.BytesIO(urllib.request.urlopen(req, timeout=2).read())).convert('RGB')


def record(sim, egg):
    """[(seconds, screen picture)] over LENGTH_S, one every FRAME_S."""
    home = tempfile.mkdtemp()
    os.makedirs(os.path.join(home, 'pets'))
    shutil.copy(egg, os.path.join(home, 'pets'))
    port = free_port()
    proc = subprocess.Popen([sim, '--headless', '--port', str(port), '--name', f'gif-{port}',
                             '--pets', os.path.join(home, 'pets')], env={'HOME': home, 'PATH': '/usr/bin:/bin'},
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    frames = []
    try:
        for _ in range(50):
            try:
                screen(port)
                break
            except OSError:
                time.sleep(0.1)
        settle, sent = time.monotonic(), False
        while (p := time.monotonic() - settle) < PREROLL_S:
            post(port, user(-1), '/v1/user')
            if not sent and p > 1:
                post(port, AWAY)
                sent = True
            time.sleep(0.25)
        pending = list(TIMELINE)
        start, last_beat = time.monotonic(), -1.0
        while (t := time.monotonic() - start) < LENGTH_S:
            if t - last_beat >= 0.25:
                post(port, user(t), '/v1/user')
                last_beat = t
            while pending and t >= pending[0][0]:
                post(port, pending.pop(0)[1])
            frames.append((t, screen(port)))
            time.sleep(max(0.0, FRAME_S - (time.monotonic() - start - t)))
    finally:
        proc.terminate()
        proc.wait()
        shutil.rmtree(home, ignore_errors=True)
    return frames


def perspective(dst, src):
    """Coefficients for Image.transform(PERSPECTIVE): maps output points dst to input points src."""
    rows = []
    for (x, y), (u, v) in zip(dst, src):
        rows.append([x, y, 1, 0, 0, 0, -u * x, -u * y, u])
        rows.append([0, 0, 0, x, y, 1, -v * x, -v * y, v])
    n = 8
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(rows[r][c]))
        rows[c], rows[p] = rows[p], rows[c]
        for r in range(n):
            if r != c:
                f = rows[r][c] / rows[c][c]
                rows[r] = [a - f * b for a, b in zip(rows[r], rows[c])]
    return [rows[r][n] / rows[r][r] for r in range(n)]


class Stage:
    """The case on the paper colour, with a screen picture warped into its window and a caption under it."""

    def __init__(self, font=FONT):
        case = Image.open(CASE).convert('RGBA')
        k = WIDTH / case.width
        self.case = case.resize((WIDTH, round(case.height * k)), Image.LANCZOS)
        self.corners = [(x * k, y * k) for x, y in CORNERS]
        # the window: where the case shows its dark, switched-off screen inside the corners
        quad = Image.new('L', self.case.size, 0)
        ImageDraw.Draw(quad).polygon(self.corners, fill=255)
        dark = self.case.convert('L').point(lambda v: 255 if v < 45 else 0)
        self.window = Image.composite(dark, quad.point(lambda v: 0), quad)
        self.font = ImageFont.truetype(font, 28)
        self.size = (WIDTH, self.case.height + 64)

    def frame(self, picture, caption):
        big = picture.resize((picture.width * 4, picture.height * 4), Image.NEAREST)
        src = [(0, 0), (big.width, 0), (big.width, big.height), (0, big.height)]
        warped = big.transform(self.case.size, Image.PERSPECTIVE, perspective(self.corners, src), Image.BILINEAR)
        out = Image.new('RGB', self.size, PAPER)
        out.paste(self.case, (0, 0), self.case)
        out.paste(warped, (0, 0), self.window)
        draw = ImageDraw.Draw(out)
        w = draw.textlength(caption, font=self.font)
        draw.text(((WIDTH - w) / 2, self.case.height + 6), caption, font=self.font, fill=INK)
        return out


def caption_at(t):
    return [c for s, c in CAPTIONS if s <= t][-1]


def main():
    parser = argparse.ArgumentParser(description='Record the README animation from the simulator.')
    parser.add_argument('--build', default=os.environ.get('NESTBOX_BUILD_DIR') or os.path.join(ROOT, 'build'))
    parser.add_argument('--egg', default=os.path.join(ROOT, 'pets', 'goose.egg'), help='the pet to record')
    parser.add_argument('--font', default=FONT, help="the captions' typeface (a .ttf or .ttc)")
    parser.add_argument('-o', '--out', default=os.path.join(ROOT, 'assets', 'nestbox.gif'))
    args = parser.parse_args()
    sim = os.path.join(args.build, 'nestbox-sim')
    if not os.path.exists(sim):
        raise SystemExit(f'no simulator in {args.build}: cmake --build {args.build} --target nestbox-sim')
    shots = record(sim, args.egg)
    stage = Stage(args.font)
    frames = [stage.frame(pic, caption_at(t)) for t, pic in shots]
    # One palette for every frame, so the case never shimmers: drawn from the first frame (the case and the day
    # scene) and a strip of screens from across the recording (the effects and the night).
    samples = [pic for _, pic in shots[::max(1, len(shots) // 12)]]
    source = Image.new('RGB', (stage.size[0] + 280, max(stage.size[1], 240 * len(samples))), PAPER)
    source.paste(frames[0], (0, 0))
    for i, pic in enumerate(samples):
        source.paste(pic, (stage.size[0], 240 * i))
    palette = source.quantize(256, method=Image.Quantize.MEDIANCUT)
    gif = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    ms = round(FRAME_S * 1000)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    gif[0].save(args.out, save_all=True, append_images=gif[1:], duration=ms, loop=0, optimize=False, disposal=1)
    print(f'{len(gif)} frames over {shots[-1][0]:.1f} s, {os.path.getsize(args.out) // 1024} KB -> '
          f'{os.path.relpath(args.out)}')


if __name__ == '__main__':
    main()
