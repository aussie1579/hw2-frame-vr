"""Turns the smoke test's VR panel images (ui_*.raw: int width, int height, BGRA rows top-down) into PNGs, composited
over a dark grey so the panels' transparency shows. Usage: python -I tools/ui_preview.py <dir with ui_*.raw> <out dir>"""
import struct
import sys
import zlib
from pathlib import Path


def png(path, w, h, rgb_rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + row for row in rgb_rows)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    Path(path).write_bytes(data)


src, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
for f in sorted(src.glob("ui_*.raw")):
    b = f.read_bytes()
    w, h = struct.unpack_from("<ii", b, 0)
    px = b[8:]
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            bb, gg, rr, aa = px[(y * w + x) * 4:(y * w + x) * 4 + 4]
            a = aa / 255.0
            bg = 60 if ((x // 16 + y // 16) % 2) else 45  # checkerboard shows transparency
            row += bytes(int(c * a + bg * (1 - a)) for c in (rr, gg, bb))
        rows.append(bytes(row))
    png(out / (f.stem + ".png"), w, h, rows)
    print(f"{f.name} -> {f.stem}.png ({w}x{h})")
