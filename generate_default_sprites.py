"""Generate the default Square.png and Circle.png sprites used by SpriteRendererComponent.

The engine ships a unit-square and a unit-circle as starting sprite assets so
2D users don't have to author their own before they can use SpriteRenderer.
The images are 64x64 RGBA, with the actual shape drawn in opaque white against
a fully transparent background.
"""
import os
import struct
import zlib


SIZE = 64  # sprite is 64x64 — small enough to be cheap, large enough to be crisp


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    """Build a single PNG chunk: length(4) + type(4) + data + crc32(4)."""
    length = struct.pack(">I", len(data))
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return length + tag + data + struct.pack(">I", crc)


def _make_png(width: int, height: int, pixels: bytes) -> bytes:
    """Wrap a flat RGBA pixel buffer (length = w*h*4) as a valid PNG file."""
    sig = b"\x89PNG\r\n\x1a\n"

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA

    # Each scanline must be prefixed with a filter byte (0 = none).
    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * stride:(y + 1) * stride])
    idat = zlib.compress(bytes(raw), level=9)

    return sig + _png_chunk(b"IHDR", ihdr) + _png_chunk(b"IDAT", idat) + _png_chunk(b"IEND", b"")


def _square_pixels(size: int) -> bytes:
    """Solid white square with a transparent background."""
    px = bytearray(size * size * 4)
    for y in range(size):
        for x in range(size):
            i = (y * size + x) * 4
            px[i + 0] = 255  # R
            px[i + 1] = 255  # G
            px[i + 2] = 255  # B
            px[i + 3] = 255  # A
    return bytes(px)


def _circle_pixels(size: int) -> bytes:
    """Solid white disc; pixels outside the inscribed circle stay transparent."""
    px = bytearray(size * size * 4)
    cx = (size - 1) * 0.5
    cy = (size - 1) * 0.5
    r = size * 0.5
    r2 = r * r
    for y in range(size):
        for x in range(size):
            dx = x - cx
            dy = y - cy
            if dx * dx + dy * dy <= r2:
                i = (y * size + x) * 4
                px[i + 0] = 255
                px[i + 1] = 255
                px[i + 2] = 255
                px[i + 3] = 255
    return bytes(px)


def main():
    # The engine's template-assets directory. ProjectManager::CreateProject
    # copies anything here into a new project's Assets/ via CopyDefaultAsset.
    out_dir = os.path.join("Ditto", "Assets", "Sprites")
    os.makedirs(out_dir, exist_ok=True)

    square_path = os.path.join(out_dir, "Square.png")
    circle_path = os.path.join(out_dir, "Circle.png")

    with open(square_path, "wb") as f:
        f.write(_make_png(SIZE, SIZE, _square_pixels(SIZE)))
    print(f"Wrote {square_path} ({SIZE}x{SIZE})")

    with open(circle_path, "wb") as f:
        f.write(_make_png(SIZE, SIZE, _circle_pixels(SIZE)))
    print(f"Wrote {circle_path} ({SIZE}x{SIZE})")


if __name__ == "__main__":
    main()
