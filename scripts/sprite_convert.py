#!/usr/bin/env python3

import sys
from PIL import Image
import struct

if len(sys.argv) != 3:
  print("PicoVision sprite converter.  Converts image files to sprite data in PicoVision sprite format")
  print("Files produced using this tool can be loaded with load_sprite() using PicoVision's version of PicoGraphics")
  print()
  print("Usage: %s input.png output.pvs" % (sys.argv[0],))
  sys.exit(-1)

out_file = open(sys.argv[2], "wb")

img = Image.open(sys.argv[1])
width, height = img.size

out_file.write(struct.pack('<B', width))
out_file.write(struct.pack('<B', height))

data = img.load()

data_buf = bytearray()
offsets = []
widths = []

for y in range(height):
  line_buf = bytearray()
  found_offset = False
  last_opaque = -1
  for x in range(width):
    pix = data[x, y]
    if not found_offset and pix[3] > 64:
      offsets.append(x)
      found_offset = True
    if found_offset:
      val =  (pix[0] << 7) & 0x7C00
      val |= (pix[1] << 2) & 0x03E0
      val |= (pix[2] >> 3) & 0x001F
      if pix[3] > 64:
        val |= 0x8000
        last_opaque = x
      line_buf.extend(struct.pack('<H', val))
  if not found_offset:
    offsets.append(0)
    widths.append(0)
  else:
    line_width = last_opaque + 1 - offsets[-1]
    widths.append(line_width)
    data_buf.extend(line_buf[:line_width * 2])

for y in range(height):
  out_file.write(struct.pack('<B', offsets[y]))
  out_file.write(struct.pack('<B', widths[y]))

if (height & 1) == 0:
  out_file.write(struct.pack('<H', 0))

if len(data_buf) > 32*32*2:
  print("Warning, sprite {} too large ({} bytes)".format(img_file, len(data_buf)))
out_file.write(data_buf)

while out_file.tell() & 3 != 0:
  out_file.write(struct.pack('<B', 0))
