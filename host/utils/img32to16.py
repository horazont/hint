#!/usr/bin/python3
import serial
import sys
import struct

imgfile = open(sys.argv[1], "rb")

def decode_raw(buf):
    i = 0
    s = struct.Struct("BBB")
    while i < len(buf):
        pixel_raw = buf[i:i+3]
        r, g, b = s.unpack(pixel_raw)
        rv = r/255.
        gv = g/255.
        bv = b/255.
        r16 = min(round(rv*31), 31)
        g16 = min(round(gv*63), 63)
        b16 = min(round(bv*31), 31)
        pixel16 = (r16 << 11) | (g16 << 5) | b16
        yield pixel16
        i += 3

pixels = list(decode_raw(imgfile.read()))
print("imgsize = {} pixels".format(len(pixels)))

with open(sys.argv[2], "wb") as f:
    for pixel in pixels:
        f.write(pixel.to_bytes(2, byteorder='little'))
