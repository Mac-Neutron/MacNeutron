"""Prints W or N for eight pixels of a 1280x720 PPM frame: whether each is white (all channels > 200).

The samples sit in the middle of present_loop's checkerboard squares and gaps as they appear after a 2x upscale
of a 640x360 swap chain. The last two are near the bottom-right, so only a whole-frame upscale gets them right.

`pixels.py <ppm> edges` prints how hard the frame's edges are instead: the total difference between horizontally adjacent
red values, over the frame (a sum, not a mean: a frame that is mostly flat would floor a mean to 0). Sharpening raises
it and blurring lowers it, so the same frame presented with and without MACNEUTRON_SHARPEN can be told apart."""
import sys

SAMPLES = [(16, 16), (48, 16), (80, 16), (16, 48), (48, 80), (16, 80), (1232, 656), (1264, 656)]

with open(sys.argv[1], "rb") as f:
    data = f.read()
magic, width, height, maxval, pixels = data.split(maxsplit=4)
width, height = int(width), int(height)

if len(sys.argv) > 2 and sys.argv[2] == "edges":
    total = 0
    for y in range(height):
        row = pixels[y * width * 3:(y + 1) * width * 3]
        total += sum(abs(row[i] - row[i + 3]) for i in range(0, len(row) - 3, 3))
    print(total)
else:
    print("".join("W" if min(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) > 200 else "N" for x, y in SAMPLES))
