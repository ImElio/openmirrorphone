"""Draws OpenMirrorPhone's 16x16 pixel-art icons (Windows 98 style) and writes
src/icons_data.inc. Shapes are drawn with a few primitives in the classic 16-colour
palette; a black outline is then added around every shape automatically.

    python tools/make_icons.py [preview.png]

With a path, also saves an enlarged preview sheet (needs nothing but the standard
library: the PNG is written by hand)."""
import math
import struct
import sys
import zlib
from pathlib import Path

PALETTE = {
    'k': (0, 0, 0), 'w': (255, 255, 255), 'g': (192, 192, 192), 'G': (128, 128, 128),
    'd': (64, 64, 64), 'r': (255, 0, 0), 'R': (128, 0, 0), 'l': (0, 255, 0), 'L': (0, 128, 0),
    'b': (0, 0, 255), 'B': (0, 0, 128), 'c': (0, 255, 255), 't': (0, 128, 128),
    'y': (255, 255, 0), 'o': (128, 128, 0), 'f': (255, 255, 160), 'p': (255, 160, 160),
}
N = 16


class Canvas:
    def __init__(self):
        self.px = [['.'] * N for _ in range(N)]

    def set(self, x, y, c):
        if 0 <= x < N and 0 <= y < N:
            self.px[y][x] = c

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < N and 0 <= y < N else '.'

    def disc(self, cx, cy, r, c):
        for y in range(N):
            for x in range(N):
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                    self.set(x, y, c)

    def rect(self, x0, y0, x1, y1, c):  # inclusive
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def poly(self, pts, c):
        for y in range(N):
            for x in range(N):
                px, py = x + 0.5, y + 0.5
                inside = False
                j = len(pts) - 1
                for i in range(len(pts)):
                    xi, yi = pts[i]
                    xj, yj = pts[j]
                    if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
                        inside = not inside
                    j = i
                if inside:
                    self.set(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.set(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def bevel(self, base, light, dark):
        """Classic 3D edge: light on the top-left edge of `base` pixels, dark bottom-right."""
        src = [row[:] for row in self.px]
        for y in range(N):
            for x in range(N):
                if src[y][x] != base:
                    continue
                g = lambda xx, yy: src[yy][xx] if 0 <= xx < N and 0 <= yy < N else '.'
                if g(x - 1, y) == '.' or g(x, y - 1) == '.':
                    self.px[y][x] = light
                elif g(x + 1, y) == '.' or g(x, y + 1) == '.':
                    self.px[y][x] = dark

    def outline(self):
        src = [row[:] for row in self.px]
        for y in range(N):
            for x in range(N):
                if src[y][x] != '.':
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < N and 0 <= yy < N and src[yy][xx] not in '.k':
                        self.px[y][x] = 'k'
                        break

    def rows(self):
        return [''.join(r) for r in self.px]


def gear():
    c = Canvas()
    c.disc(8, 8, 5.0, 'g')
    c.bevel('g', 'w', 'G')
    # Eight solid 2x2 teeth, placed symmetrically around the body.
    for x, y in ((7, 1), (7, 13), (1, 7), (13, 7), (3, 3), (11, 3), (3, 11), (11, 11)):
        c.rect(x, y, x + 1, y + 1, 'g')
    c.disc(8, 8, 2.0, '.')
    c.outline()
    return c


def clock():
    c = Canvas()
    c.disc(8, 8, 6.6, 'B')
    c.disc(8, 8, 5.4, 'w')
    for x, y in ((7, 3), (12, 7), (7, 12), (3, 7)):
        c.set(x, y, 'G')
    c.line(7, 7, 7, 4, 'k')
    c.line(7, 7, 10, 7, 'k')
    c.set(7, 7, 'r')
    c.outline()
    return c


def globe():
    c = Canvas()
    c.disc(8, 8, 6.6, 'b')
    for x, y in ((5, 4), (6, 4), (4, 5), (5, 5), (6, 5), (7, 5), (5, 6), (6, 6), (6, 7), (7, 8),
                 (10, 6), (11, 6), (10, 7), (11, 7), (12, 8), (10, 9), (11, 9), (9, 10), (10, 10),
                 (5, 10), (6, 11), (7, 11)):
        c.set(x, y, 'L')
    c.set(4, 3, 'c'), c.set(3, 4, 'c'), c.set(4, 4, 'c')
    c.outline()
    return c


def info():
    c = Canvas()
    c.disc(8, 8, 6.6, 'b')
    c.bevel('b', 'c', 'B')
    c.rect(7, 4, 8, 5, 'w')
    c.rect(7, 7, 8, 11, 'w')
    c.rect(6, 7, 6, 7, 'w')
    c.rect(6, 11, 9, 11, 'w')
    c.outline()
    return c


def check():
    c = Canvas()
    for i, (x, y) in enumerate([(2, 8), (3, 9), (4, 10), (5, 11), (6, 10), (7, 9), (8, 8), (9, 7),
                                (10, 6), (11, 5), (12, 4), (13, 3)]):
        c.rect(x, y, x + 1, y + 1, 'l')
    c.bevel('l', 'l', 'L')
    c.outline()
    return c


def monitor():
    c = Canvas()
    c.rect(1, 2, 14, 11, 'g')
    c.bevel('g', 'w', 'G')
    c.rect(3, 4, 12, 9, 't')
    c.rect(4, 5, 5, 5, 'c'), c.rect(4, 6, 4, 6, 'c')
    c.rect(10, 8, 11, 8, 'c'), c.rect(11, 7, 11, 7, 'c')
    c.rect(6, 12, 9, 12, 'G')
    c.rect(4, 13, 11, 13, 'g')
    c.outline()
    return c


def record():
    c = Canvas()
    c.disc(8, 8, 5.6, 'r')
    c.bevel('r', 'p', 'R')
    c.set(6, 5, 'w'), c.set(5, 6, 'w')
    c.outline()
    return c


def stop():
    c = Canvas()
    c.rect(3, 3, 12, 12, 'G')
    c.bevel('G', 'g', 'd')
    c.outline()
    return c


def folder():
    c = Canvas()
    c.rect(1, 3, 6, 4, 'o')
    c.rect(1, 4, 14, 13, 'o')
    c.poly([(1, 13.99), (3, 7), (15.99, 7), (14, 13.99)], 'y')
    c.bevel('y', 'f', 'o')
    c.outline()
    return c


def pencil():
    c = Canvas()
    for i in range(9):
        x, y = 3 + i, 12 - i
        c.set(x, y, 'y'), c.set(x + 1, y, 'o'), c.set(x, y - 1, 'f')
    c.set(12, 3, 'p'), c.set(13, 3, 'p'), c.set(12, 2, 'p'), c.set(13, 2, 'p')
    c.set(2, 13, 'k'), c.set(2, 12, 'G'), c.set(3, 13, 'G')
    c.outline()
    return c


def play():
    c = Canvas()
    c.poly([(4, 2), (4, 14), (13, 8)], 'l')
    c.bevel('l', 'l', 'L')
    c.outline()
    return c


def cross():
    c = Canvas()
    for i in range(10):
        c.rect(3 + i, 3 + i, 4 + i, 4 + i, 'r')
        c.rect(12 - i, 3 + i, 13 - i, 4 + i, 'r')
    c.bevel('r', 'r', 'R')
    c.outline()
    return c


def download():
    c = Canvas()
    c.rect(6, 1, 9, 7, 'l')
    c.poly([(2.5, 7), (13.5, 7), (8, 12.5)], 'l')
    c.bevel('l', 'l', 'L')
    c.rect(2, 13, 13, 14, 'G')
    c.outline()
    return c


def skip():
    c = Canvas()
    c.poly([(2, 3), (2, 13), (8, 8)], 'b')
    c.poly([(8, 3), (8, 13), (14, 8)], 'b')
    c.bevel('b', 'c', 'B')
    c.outline()
    return c


def camera():
    c = Canvas()
    c.rect(1, 4, 14, 13, 'g')
    c.rect(4, 2, 8, 4, 'g')
    c.bevel('g', 'w', 'G')
    c.disc(8, 9, 3.5, 'B')
    c.disc(8, 9, 2.0, 'c')
    c.set(12, 5, 'r')
    c.outline()
    return c


def keyboard():
    c = Canvas()
    c.rect(1, 3, 14, 12, 'g')
    c.bevel('g', 'w', 'G')
    for x, y in ((3, 5), (6, 5), (9, 5), (12, 5), (3, 8), (6, 8), (9, 8), (12, 8)):
        c.rect(x, y, x + 1, y + 1, 'B')
    c.rect(5, 10, 10, 10, 'B')
    c.outline()
    return c


ICONS = [('Gear', gear), ('Clock', clock), ('Globe', globe), ('Info', info), ('Check', check),
         ('Monitor', monitor), ('Record', record), ('Stop', stop), ('Folder', folder),
         ('Pencil', pencil), ('Play', play), ('Cross', cross), ('Download', download), ('Skip', skip),
         ('Camera', camera), ('Keyboard', keyboard)]


def write_png(path, width, height, rgba_rows):
    raw = b''.join(b'\x00' + bytes(row) for row in rgba_rows)
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    drawn = [(name, fn().rows()) for name, fn in ICONS]
    for name, rows in drawn:
        assert len(rows) == N and all(len(r) == N for r in rows), name

    out = ['// Generated by tools/make_icons.py: 16x16 pixel-art icons, one string per row.',
           '// Letters are PALETTE entries, "." is transparent. Edit the script, not this file.', '']
    out += [f"PALETTE('{ch}', {r}, {g}, {b})" for ch, (r, g, b) in PALETTE.items()] + ['']
    for name, rows in drawn:
        out.append(f'ICON({name},')
        out += [f'    "{r}"' for r in rows]
        out.append(')')
    Path(__file__).resolve().parent.parent.joinpath('src', 'icons_data.inc').write_text('\n'.join(out) + '\n')

    if len(sys.argv) > 1:  # preview: icons enlarged 6x on light and dark backgrounds
        scale, pad = 6, 8
        cell = N * scale + pad
        width, height = cell * len(drawn) + pad, cell * 2 + pad
        rows = []
        for py in range(height):
            row = []
            band = 0 if py < height // 2 else 1
            bg = (240, 240, 240) if band == 0 else (32, 32, 32)
            for px in range(width):
                i, ox = divmod(px - pad, cell)
                oy = (py - pad) % cell
                color = bg
                if 0 <= i < len(drawn) and px >= pad and ox < N * scale and py >= pad and oy < N * scale:
                    ch = drawn[i][1][oy // scale][ox // scale]
                    if ch != '.':
                        color = PALETTE[ch]
                row += [*color, 255]
            rows.append(row)
        write_png(sys.argv[1], width, height, rows)
    print(f'{len(drawn)} icons written')


if __name__ == '__main__':
    main()
