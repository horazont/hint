#!/usr/bin/python3
import ast
import math

import lcdencode

def load_strings(sfile):
    for line in sfile:
        line = line.split("#", 1)[0].strip()
        if not line:
            continue

        name, value = line.split(None, 1)
        yield name, ast.literal_eval(value)

def byteparts(items, slicelen=12):
    all_bytes = b"".join(
        value
        for _, value, _ in items
    )

    for i in range(math.ceil(len(all_bytes)/slicelen)):
        yield all_bytes[i*slicelen:(i+1)*slicelen]

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-c", "--codec",
        default="HD44780A00")
    parser.add_argument(
        "stringfile",
        type=argparse.FileType("r")
    )

    args = parser.parse_args()

    total_len = 0
    items = []
    for key, value in load_strings(args.stringfile):
        value = value.encode(args.codec)
        items.append((key, value, total_len))
        total_len += len(value)


    print(
"""#ifndef ALL_STRINGS_H
#define ALL_STRINGS_H

static const __flash char all_strings[] = {{
    {}
}};
""".format(
    ",\n    ".join(
        ", ".join("0x{:02x}".format(ch) for ch in slice)
        for slice in byteparts(items))
))

    for key, value, offset in items:
        print("""#define STR_{key}_FLASHBUF (&all_strings[{offset}])""".format(
            key=key,
            offset=offset))
        print("""#define STR_{key}_LEN ({len})""".format(
            key=key,
            len=len(value)))

    print(
"""
#define STR_lcd_write(strname) lcd_write_textbuf_from_flash(\\
    STR_ ## strname ## _FLASHBUF, \\
    STR_ ## strname ## _LEN)

#endif
""")
