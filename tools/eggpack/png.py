"""A minimal PNG reader (8-bit RGBA, RGB or palette) and RGBA writer, so the tools need no extra packages."""
import struct
import zlib

SIGNATURE_SIZE = 8
BYTES_PER_PIXEL = {6: 4, 2: 3, 3: 1}  # by colour type: RGBA, RGB, palette


def read_chunks(data):
    """{chunk type: bytes}, with the IDAT chunks joined."""
    chunks, i = {}, SIGNATURE_SIZE
    while i < len(data):
        n = struct.unpack('>I', data[i:i + 4])[0]
        kind, body = data[i + 4:i + 8], data[i + 8:i + 8 + n]
        chunks[kind] = chunks.get(kind, b'') + body if kind == b'IDAT' else body
        i += 12 + n
    return chunks


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def unfilter(raw, height, stride, bpp):
    """The image's rows with the PNG filters undone."""
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        kind = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif kind == 4:
                line[x] = (line[x] + paeth(a, b, c)) & 255
        rows.append(bytes(line))
        prev = line
    return rows


def read_png(path):
    """Returns (width, height, get), where get(x, y) -> (r, g, b, a)."""
    with open(path, 'rb') as f:
        data = f.read()
    w, h, depth, ctype = struct.unpack('>IIBB', data[16:26])
    if depth != 8 or ctype not in BYTES_PER_PIXEL:
        raise ValueError(f'{path}: only 8-bit RGB, RGBA or palette PNGs are supported')
    if data[28] != 0:
        raise ValueError(f'{path}: interlaced PNGs are not supported; save it without interlacing')
    chunks = read_chunks(data)
    bpp = BYTES_PER_PIXEL[ctype]
    rows = unfilter(zlib.decompress(chunks[b'IDAT']), h, w * bpp, bpp)
    plte, trns = chunks.get(b'PLTE'), chunks.get(b'tRNS')

    def get(x, y):
        r = rows[y]
        if ctype == 6:
            o = x * 4
            return r[o], r[o + 1], r[o + 2], r[o + 3]
        if ctype == 2:
            o = x * 3
            return r[o], r[o + 1], r[o + 2], 255
        k = r[x]
        alpha = trns[k] if trns and k < len(trns) else 255
        return plte[k * 3], plte[k * 3 + 1], plte[k * 3 + 2], alpha

    return w, h, get


def write_png(path, w, h, px):
    """Writes an 8-bit RGBA PNG; px(x, y) -> (r, g, b, a)."""
    raw = b''.join(b'\0' + bytes(c for x in range(w) for c in px(x, y)) for y in range(h))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
