#!/usr/bin/python3

def clamp(v, a, b):
    return max(a, min(b, v))

def rgbi24_to_rgbf24(r, g, b):
    return r/255, g/255, b/255

def rgbf24_to_rgb16(r, g, b):
    r = int(clamp(r*31, 0, 31))
    g = int(clamp(g*63, 0, 63))
    b = int(clamp(b*31, 0, 31))

    return (r << 11) | (g << 5) | b

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-f", "--float", "--decimal",
        action="store_true",
        dest="float",
        default=False,
        help="Use floating point numbers as input"
    )
    parser.add_argument(
        "red",
        type=float
    )
    parser.add_argument(
        "green",
        type=float
    )
    parser.add_argument(
        "blue",
        type=float
    )

    args = parser.parse_args()

    colourtuple = args.red, args.green, args.blue

    if not args.float:
        colourtuple = rgbi24_to_rgbf24(*colourtuple)

    print(hex(rgbf24_to_rgb16(*colourtuple)))
