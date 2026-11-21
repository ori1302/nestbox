"""Renders the README's animation (assets/nestbox.gif): Goose at its desk while you type, calling you with
a question, and asleep at night. Every frame comes from the engine (`nestbox-sim --shot`); the GIF is written here,
with the standard library only.

    python3 tools/demo_gif.py [--build DIR] [--egg pets/goose.egg] [-o assets/nestbox.gif] [--scale 2]

Needs the simulator (macOS): cmake --build build --target nestbox-sim.
"""
import argparse
import os
import shutil
import struct
import subprocess
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
FRAME_MS = 100
# (look, time of day, speech bubble, first ms in the look, frames); 'day' is the scene's main picture
SCENES = (
    ('type_fast', 'day', '', 3000, 24),
    ('alert', 'day', 'Claude needs your OK', 3000, 24),
    ('sleep', 'night', '', 3000, 24),
)
MAX_CODE = 4096          # GIF's LZW codes are at most 12 bits
LOOP_FOREVER = 0
DISPOSE_NONE = 1         # leave the frame in place; the next one draws over it


def read_ppm(path):
    """(width, height, [(r, g, b), ...]) from a binary PPM as nestbox-sim writes it."""
    with open(path, 'rb') as f:
        data = f.read()
    magic, size, depth, pixels = data.split(b'\n', 3)
    if magic != b'P6' or depth != b'255':
        raise ValueError(f'{path}: not an 8-bit binary PPM')
    w, h = map(int, size.split())
    return w, h, [tuple(pixels[i:i + 3]) for i in range(0, w * h * 3, 3)]


def scaled(w, h, px, k):
    """The image enlarged k times, each pixel a k x k block."""
    rows = [px[y * w:(y + 1) * w] for y in range(h)]
    out = []
    for row in rows:
        line = [c for c in row for _ in range(k)]
        for _ in range(k):
            out.extend(line)
    return w * k, h * k, out


def render(sim, pets, home, look, daypart, text, ms, out):
    args = [sim, '--pets', pets, '--shot', out, '0', look, str(ms)] + ([text] if text else [])
    env = {**os.environ, 'HOME': home, 'NESTBOX_SHOT_DAYPART': daypart}
    subprocess.run(args, check=True, capture_output=True, env=env)
    return read_ppm(out)


def changed_box(prev, cur, w, h):
    """The smallest (x, y, w, h) holding every pixel that differs, or None if none does."""
    xs, ys = [], []
    for i, (a, b) in enumerate(zip(prev, cur)):
        if a != b:
            xs.append(i % w)
            ys.append(i // w)
    if not xs:
        return None
    return min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1


def crop(px, w, box):
    x0, y0, bw, bh = box
    return [c for y in range(y0, y0 + bh) for c in px[y * w + x0:y * w + x0 + bw]]


def lzw(indices, min_size):
    """GIF's variable-length LZW, packed least significant bit first."""
    clear, end = 1 << min_size, (1 << min_size) + 1
    out, acc, nbits = bytearray(), 0, 0

    def emit(code, size):
        nonlocal acc, nbits
        acc |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(acc & 255)
            acc >>= 8
            nbits -= 8

    def reset():
        return {}, end + 1, min_size + 1

    table, next_code, size = reset()
    emit(clear, size)
    prefix = indices[0]
    for k in indices[1:]:
        key = (prefix, k)
        if key in table:
            prefix = table[key]
            continue
        emit(prefix, size)
        if next_code == MAX_CODE:
            emit(clear, size)
            table, next_code, size = reset()
        else:
            table[key] = next_code
            if next_code == 1 << size:
                size += 1
            next_code += 1
        prefix = k
    emit(prefix, size)
    emit(end, size)
    if nbits:
        out.append(acc & 255)
    return bytes(out)


def sub_blocks(data):
    return b''.join(bytes([len(data[i:i + 255])]) + data[i:i + 255] for i in range(0, len(data), 255)) + b'\0'


def image_block(px, box, delay_cs):
    """One frame: its control extension, descriptor, own colour table and pixels."""
    colours = sorted(set(px))
    if len(colours) > 256:
        raise ValueError(f'a frame has {len(colours)} colours; a GIF frame holds 256')
    bits = max(1, (len(colours) - 1).bit_length())
    index = {c: i for i, c in enumerate(colours)}
    table = b''.join(bytes(c) for c in colours) + b'\0' * 3 * ((1 << bits) - len(colours))
    x, y, w, h = box
    control = b'\x21\xf9\x04' + bytes([DISPOSE_NONE << 2]) + struct.pack('<H', delay_cs) + b'\0\0'
    descriptor = b'\x2c' + struct.pack('<4H', x, y, w, h) + bytes([0x80 | (bits - 1)])
    min_size = max(2, bits)
    return control + descriptor + table + bytes([min_size]) + sub_blocks(lzw([index[c] for c in px], min_size))


def merge_repeats(frames):
    """Consecutive identical frames as one, shown for their total time."""
    merged = []
    for px, delay_ms in frames:
        if merged and merged[-1][0] == px:
            merged[-1] = (px, merged[-1][1] + delay_ms)
        else:
            merged.append((px, delay_ms))
    return merged


def write_gif(path, w, h, frames):
    """frames: [(pixels, delay in ms)]. Each frame after the first stores only the box that changed."""
    out = bytearray(b'GIF89a' + struct.pack('<2H', w, h) + b'\0\0\0')
    out += b'\x21\xff\x0bNETSCAPE2.0\x03\x01' + struct.pack('<H', LOOP_FOREVER) + b'\0'
    prev = None
    for px, delay_ms in merge_repeats(frames):
        box = (0, 0, w, h) if prev is None else changed_box(prev, px, w, h)
        out += image_block(crop(px, w, box), box, delay_ms // 10)
        prev = px
    out += b'\x3b'
    with open(path, 'wb') as f:
        f.write(out)
    return len(out)


def main():
    parser = argparse.ArgumentParser(description='Render the README animation from the simulator.')
    parser.add_argument('--build', default=os.environ.get('NESTBOX_BUILD_DIR') or os.path.join(ROOT, 'build'))
    parser.add_argument('--egg', default=os.path.join(ROOT, 'pets', 'goose.egg'), help='the pet to record')
    parser.add_argument('-o', '--out', default=os.path.join(ROOT, 'assets', 'nestbox.gif'))
    parser.add_argument('--scale', type=int, default=2, help='pixels per screen pixel in the GIF')
    args = parser.parse_args()
    sim = os.path.join(args.build, 'nestbox-sim')
    if not os.path.exists(sim):
        raise SystemExit(f'no simulator in {args.build}: cmake --build {args.build} --target nestbox-sim')
    frames, size = [], None
    with tempfile.TemporaryDirectory() as tmp:
        pets = os.path.join(tmp, 'pets')
        os.makedirs(pets)
        shutil.copy(args.egg, pets)
        shot = os.path.join(tmp, 'shot.ppm')
        for look, daypart, text, start, count in SCENES:
            for i in range(count):
                w, h, px = render(sim, pets, tmp, look, daypart, text, start + i * FRAME_MS, shot)
                w, h, px = scaled(w, h, px, args.scale)
                size = (w, h)
                frames.append((px, FRAME_MS))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    n = write_gif(args.out, *size, frames)
    print(f'{len(frames)} frames, {n // 1024} KB -> {os.path.relpath(args.out)}')


if __name__ == '__main__':
    main()
