#!/bin/sh
# Regenerate every README image from the app's own draw path.
#
# The point of this script is that a picture in the README cannot drift from
# the code that produced it: these are not screenshots taken once and forgotten,
# they are output. Run it after any change that alters the diagram.
set -e

OUT=docs/images
mkdir -p "$OUT"

if [ ! -x ./opticsim ]; then
    echo "build first: make" >&2
    exit 1
fi

for subject in scene lamp ambient ambientimage achromat singlet stopped blades image; do
    SDL_VIDEODRIVER=dummy ./opticsim --capture "$subject" "$OUT"
done

# BMP is what SDL_SaveBMP writes; the README wants PNG. Converted in Python so
# the script needs no ImageMagick, no ffmpeg, and no Pillow.
python3 - "$OUT" <<'PY'
import struct, zlib, sys, os
out = sys.argv[1]
for name in ("scene", "lamp", "ambient", "ambientimage",
             "achromat", "singlet", "stopped", "blades", "image"):
    src = os.path.join(out, name + ".bmp")
    if not os.path.exists(src):
        continue
    d = open(src, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w   = struct.unpack_from('<i', d, 18)[0]
    h   = struct.unpack_from('<i', d, 22)[0]
    bpp = struct.unpack_from('<H', d, 28)[0]
    flip = h > 0
    h = abs(h)
    rowsz = ((bpp * w + 31) // 32) * 4
    rows = []
    for y in range(h):
        sy = (h - 1 - y) if flip else y
        base = off + sy * rowsz
        row = bytearray([0])
        for x in range(w):
            px = base + x * (bpp // 8)
            row += bytes((d[px+2], d[px+1], d[px]))
        rows.append(bytes(row))
    raw = b''.join(rows)
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(os.path.join(out, name + ".png"), 'wb').write(png)
    os.remove(src)
    print("wrote", os.path.join(out, name + ".png"))
PY

# The two photographs in the README are batch renders rather than window
# captures, and they were the one pair this script did not own -- so they were
# the one pair that could go stale without anything saying so. They are here
# now, with the exact arguments that made them.
./opticsim still --stage rail --fstop 5 --focus 2.0 \
    --width 720 --height 480 --spp 1024 --out "$OUT/rail.ppm" >/dev/null
# The bokeh exposure is stated rather than left at the default 100x. Twelve
# small very bright sources against nothing at all put the brightest pixel
# about 1400x over white at that default, and a blown disc is a white circle
# whatever shape the iris is -- which is the one thing this picture is of.
./opticsim still --stage bokeh --blades 6 --focus 1.2 --exposure 0.2 \
    --width 720 --height 480 --spp 1024 --out "$OUT/bokeh.ppm" >/dev/null

python3 - "$OUT" <<'PY'
import struct, zlib, sys, os

def read_ppm(path):
    """P6 header: magic, then width, height, maxval, comments allowed."""
    d = open(path, 'rb').read()
    fields, i = [], 2
    while len(fields) < 3:
        while i < len(d) and d[i:i+1].isspace():
            i += 1
        if d[i:i+1] == b'#':
            while d[i:i+1] not in (b'\n', b''):
                i += 1
            continue
        j = i
        while j < len(d) and not d[j:j+1].isspace():
            j += 1
        fields.append(int(d[i:j]))
        i = j
    return fields[0], fields[1], d[i+1:]

out = sys.argv[1]
for name in ("rail", "bokeh"):
    src = os.path.join(out, name + ".ppm")
    if not os.path.exists(src):
        continue
    w, h, px = read_ppm(src)
    raw = b''.join(b'\x00' + px[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(os.path.join(out, name + ".png"), 'wb').write(png)
    os.remove(src)
    print("wrote", os.path.join(out, name + ".png"))
PY
