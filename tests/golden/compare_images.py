#!/usr/bin/env python3
import argparse
import struct
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_rgb(path):
    data = Path(path).read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise SystemExit(f"{path} is not a PNG file")

    pos = len(PNG_SIGNATURE)
    width = height = None
    bit_depth = color_type = interlace = None
    compressed = bytearray()

    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        chunk_type = data[pos + 4:pos + 8]
        chunk_data = data[pos + 8:pos + 8 + length]
        pos += 12 + length

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk_data)
        elif chunk_type == b"IDAT":
            compressed.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if width is None or height is None:
        raise SystemExit(f"{path} is missing IHDR")
    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise SystemExit(f"{path} must be 8-bit non-interlaced RGB/RGBA PNG")

    channels = 3 if color_type == 2 else 4
    row_len = width * channels
    raw = zlib.decompress(bytes(compressed))
    rows = []
    prev = [0] * row_len
    idx = 0

    for _ in range(height):
        filter_type = raw[idx]
        idx += 1
        cur = list(raw[idx:idx + row_len])
        idx += row_len

        for i, value in enumerate(cur):
            left = cur[i - channels] if i >= channels else 0
            up = prev[i]
            upper_left = prev[i - channels] if i >= channels else 0
            if filter_type == 1:
                cur[i] = (value + left) & 0xff
            elif filter_type == 2:
                cur[i] = (value + up) & 0xff
            elif filter_type == 3:
                cur[i] = (value + ((left + up) // 2)) & 0xff
            elif filter_type == 4:
                cur[i] = (value + paeth(left, up, upper_left)) & 0xff
            elif filter_type != 0:
                raise SystemExit(f"{path} uses unsupported PNG filter {filter_type}")

        if channels == 3:
            rows.extend(cur)
        else:
            for i in range(0, row_len, channels):
                rows.extend(cur[i:i + 3])
        prev = cur

    return width, height, bytes(rows)


def compare(actual_path, expected_path, max_mean, max_pixel):
    if not Path(expected_path).exists():
        raise SystemExit(f"missing expected golden image: {expected_path}")
    actual = read_png_rgb(actual_path)
    expected = read_png_rgb(expected_path)
    if actual[:2] != expected[:2]:
        raise SystemExit(f"size mismatch: {actual[:2]} != {expected[:2]}")

    actual_pixels = actual[2]
    expected_pixels = expected[2]
    total = 0
    max_delta = 0
    for av, ev in zip(actual_pixels, expected_pixels):
        delta = abs(av - ev)
        total += delta
        max_delta = max(max_delta, delta)

    mean = total / len(actual_pixels)
    print(f"mean={mean:.4f} max={max_delta}")
    if mean > max_mean or max_delta > max_pixel:
        raise SystemExit(f"image diff too large: mean={mean:.4f}, max={max_delta}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--actual", required=True)
    parser.add_argument("--expected", required=True)
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--max-mean", type=float, default=1.0)
    parser.add_argument("--max-pixel", type=int, default=8)
    args = parser.parse_args()

    actual = Path(args.actual)
    expected = Path(args.expected)
    if args.update:
        expected.parent.mkdir(parents=True, exist_ok=True)
        expected.write_bytes(actual.read_bytes())
        print(f"updated {expected}")
        return

    compare(actual, expected, args.max_mean, args.max_pixel)


if __name__ == "__main__":
    main()
