#!/usr/bin/python2
# encoding=utf8
import cairo
from gi.repository import Pango
from gi.repository import PangoCairo
import struct
import itertools
import textwrap
import logging

def split_to_parts(l, chunksize):
    while len(l) >= chunksize:
        part = l[:chunksize]
        l = l[chunksize:]
        yield part
    if l:
        yield l

class GlyphStruct(object):
    codepoint = 0
    width = 0
    height = 0
    y0 = 0
    data_offset = 0
    data = None

    def to_c_source(self):
        return """{{
    .w           = {width},
    .h           = {height},
    .y0          = {baseline},
    .data_offset = 0x{data_offset:04x}
}}""".format(
            codepoint=self.codepoint,
            width=self.width,
            height=self.height,
            baseline=self.y0,
            data_offset=self.data_offset)

class GlyphRange(object):
    start = 0
    count = 0

    def to_c_source(self):
        return """{{
    .start = 0x{start:08x},
    .count =     0x{count:04x}
}}""".format(
            start=self.start,
            count=self.count)

class FontStruct(object):
    def __init__(self):
        super(FontStruct, self).__init__()
        self.name = "font";
        self.space_width = 0
        self.glyphs = []
        self.datamap = {}
        self.data = bytearray()
        self.ranges = None

    def add_data(self, data):
        data = str(data)
        try:
            offset = self.datamap[data]
        except KeyError:
            pass
        else:
            logging.debug("optimized duplicate data at %d", offset)
            return offset

        offset = len(self.data)
        self.datamap[data] = offset
        self.data += data
        return offset

    def add_glyph(self, glyph):
        self.glyphs.append(glyph)
        glyph.data_offset = self.add_data(glyph.data)
        glyph.data = None
        # invalidate ranges
        self.ranges = None

    def calculate_ranges(self):
        glyphiter = iter(sorted(self.glyphs, key=lambda x: x.codepoint))

        ranges = []
        curr_range = GlyphRange()
        try:
            curr_range.start = next(glyphiter).codepoint
            curr_range.count = 1
        except StopIteration:
            pass
        for glyph in glyphiter:
            end = curr_range.start + curr_range.count
            if glyph.codepoint == end and curr_range.count < 0xffff:
                curr_range.count += 1
            else:
                ranges.append(curr_range)
                curr_range = GlyphRange()
                curr_range.start = glyph.codepoint
                curr_range.count = 1

        if curr_range.count > 0:
            ranges.append(curr_range)

        ranges.append(GlyphRange())

        self.ranges = ranges

    @staticmethod
    def _add_indent(text, indent):
        return "\n".join(
            indent+line
            for line in (text.split("\n") if isinstance(text, basestring) else text))

    def _c_data(self, indent="        "):
        return self._add_indent(
            textwrap.wrap(
                ", ".join(
                    r"'\x{:02x}'".format(byte)
                    for byte in self.data),
                width=72-len(indent)
                ),
                indent)

    def _c_ranges(self, indent="        "):
        return ",\n".join(
            self._add_indent(range.to_c_source(), indent)
            for range in self.ranges)

    def _c_glyph_name(self, glyph):
        return "glyph__{name}_{codepoint}".format(
            name=self.name,
            codepoint=glyph.codepoint)

    def _c_glyphs(self, indent="        "):
        return ",\n".join(
            self._add_indent(glyph.to_c_source(), indent)
            for glyph in sorted(self.glyphs, key=lambda x: x.codepoint))

    def _c_glyph_decl(self, glyph):
        return "struct glyph_t {name} = {glyph};".format(
            name=self._c_glyph_name(glyph),
            glyph=glyph.to_c_source()
        );

    def _c_glyph_decls(self):
        return "\n".join(
            self._c_glyph_decl(glyph) for glyph in self.glyphs)

    def _c_glyph_refs(self, indent="        "):
        return ",\n".join(
            "{indent}&{name}".format(
                indent=indent,
                name=self._c_glyph_name(glyph))
            for glyph in self.glyphs)

    def to_c_source(self):
        if self.ranges is None:
            self.calculate_ranges()
        return """
uint8_t {name}__data[] = {{
{data}
}};
struct glyph_range_t {name}__ranges[] = {{
{ranges}
}};
struct font_t {name} = {{
    .glyph_count = {glyph_count},
    .space_width = {space_width},
    .data = {name}__data,
    .ranges = {name}__ranges,
    .glyphs = {{
{glyphs}
    }}
}};""".format(
            name=self.name,
            glyph_count=len(self.glyphs),
            space_width=self.space_width,
            glyphs=self._c_glyphs(),
            ranges=self._c_ranges(),
            data=self._c_data(indent="    ")
            )

class Renderer:
    WIDTH = 128
    HEIGHT = 128

    def __init__(self, font_family, font_size):
        font_descr = Pango.FontDescription()
        font_descr.set_family(font_family)
        font_descr.set_size(font_size * Pango.SCALE)

        self._nullbuffer = bytearray(self.WIDTH*self.HEIGHT)
        self._buffer = bytearray(self.WIDTH*self.HEIGHT)
        self._surface = cairo.ImageSurface.create_for_data(
            self._buffer, cairo.FORMAT_A8, self.WIDTH, self.HEIGHT,
            self.WIDTH)
        self._cairo = cairo.Context(self._surface)
        self._pango = PangoCairo.create_context(self._cairo)
        # self._pango.set_resolution(72.)
        self._layout = Pango.Layout(self._pango)
        self._layout.set_font_description(font_descr)

    def render_ustr(self, ustr):
        self._buffer[:] = self._nullbuffer[:]
        self._layout.set_text(ustr.encode("utf8"), -1)

        _, logical = self._layout.get_pixel_extents()

        x0, y0 = logical.x, logical.y
        assert x0 >= 0
        assert y0 >= 0
        x1, y1 = x0 + logical.width, y0 + logical.height

        self._cairo.set_source_rgba(0, 0, 0, 1)
        PangoCairo.show_layout(self._cairo, self._layout)

        self._surface.write_to_png(open("/tmp/test.png", "wb"))

        # each pixel is one byte, let's slice it
        width = logical.width
        stride = width
        new_buffer = bytearray(width * logical.height)
        for dstrow, srcrow in enumerate(range(y0, y1)):
            dsti = dstrow*stride
            srci = x0+srcrow*self.WIDTH
            new_buffer[dsti:dsti+width] = \
                self._buffer[srci:srci+width]

        baseline = int(self._layout.get_baseline() / Pango.SCALE)

        return width, logical.height, baseline, new_buffer

    def compress_glyph_to_mono(self, width, height, baseline, data):
        if width == 0:
            return width, height, baseline, data
        logging.debug("compressing glyph (w=%d, h=%d)", width, height)
        rows = list(split_to_parts(data, width))
        rowiter = iter(rows)
        skip_above = 0
        skip_below = 0
        for row in rowiter:
            if all(byte < 127 for byte in row):
                skip_above += 1
            else:
                break
        for row in rowiter:
            if all(byte < 127 for byte in row):
                skip_below += 1
            else:
                skip_below = 0

        height -= skip_above
        baseline -= skip_above
        height -= skip_below

        if skip_below == 0:
            data = itertools.chain(*rows[skip_above:])
        else:
            data = itertools.chain(*rows[skip_above:-skip_below])
        data = bytearray(data)

        return width, height, baseline, data

    def compress_alpha(self, buf):
        byteblocks = list(split_to_parts(buf, 8))
        final = bytearray(len(byteblocks))
        for i, block in enumerate(byteblocks):
            curr_byte = 0x00
            bitmask = 0x80
            for bit in block:
                if bit >= 127:
                    curr_byte |= bitmask
                bitmask = bitmask >> 1
            final[i] = curr_byte
        return final

    def struct_ustr(self, codepoint):
        result = GlyphStruct()
        result.codepoint = codepoint
        w, h, baseline, data = self.compress_glyph_to_mono(
            *self.render_ustr(unichr(codepoint)))
        result.width = w
        result.height = h
        result.y0 = baseline
        result.data = self.compress_alpha(data)
        return result

    def struct_font(self, codepoints):
        codepoints = set(codepoints)
        if 0x20 in codepoints:
            # space is handled specially
            codepoints.remove(0x20)

        result = FontStruct()
        result.space_width, _, _, _ = self.render_ustr(u' ')

        for codepoint in codepoints:
            logging.debug("rendering glyph for codepoint %d", codepoint)
            result.add_glyph(self.struct_ustr(codepoint))

        return result

def extint(v):
    v = v.lower()
    if v.startswith("0x"):
        return int(v[2:], 16)
    elif v.startswith("0b"):
        return int(v[2:], 2)
    else:
        return int(v)

if __name__ == "__main__":
    import argparse
    import os
    import sys

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "font",
        metavar="FONT",
        help="Use font family FONT"
    )
    parser.add_argument(
        "size",
        type=float,
        metavar="POINTS",
        help="Font size in pt"
    )
    parser.add_argument(
        "structname",
        metavar="IDENTIFIER",
        help="A valid C11 identifier for the struct"
    )
    parser.add_argument(
        "-r", "--add-range",
        nargs=2,
        action="append",
        metavar=("FROM", "TO"),
        default=[],
        type=extint,
        dest="cp_ranges",
        help="Add a range of codepoints to render. All glyphs from FROM"
             " to TO (inclusively) will be rendered."
    )
    parser.add_argument(
        "-c", "--add-codepoints",
        nargs="+",
        action="append",
        metavar="CODEPOINT",
        default=[],
        type=extint,
        dest="cp_sequences",
        help="Add a sequence of codepoints to render."
    )
    parser.add_argument(
        "-x", "--exclude",
        nargs="+",
        action="append",
        metavar="CODEPOINT",
        default=[],
        type=extint,
        dest="cp_exclude_sequences",
        help="Exclude a sequence from the selections"
    )
    parser.add_argument(
        "-v",
        action="count",
        default=0,
        help="Increase verbosity",
        dest="verbosity"
    )

    args = parser.parse_args()

    logging.basicConfig(level=logging.ERROR, format='{0}:%(levelname)-8s %(message)s'.format(os.path.basename(sys.argv[0])))

    if args.verbosity >= 3:
        logging.getLogger().setLevel(logging.DEBUG)
    elif args.verbosity >= 2:
        logging.getLogger().setLevel(logging.INFO)
    elif args.verbosity >= 1:
        logging.getLogger().setLevel(logging.WARNING)

    codepoints = set()
    for start, end in args.cp_ranges:
        codepoints |= frozenset(range(start, end+1))
    for sequence in args.cp_sequences:
        codepoints |= frozenset(sequence)
    for sequence in args.cp_exclude_sequences:
        codepoints -= frozenset(sequence)

    renderer = Renderer(args.font, args.size)
    font = renderer.struct_font(codepoints)
    font.name = args.structname

    print(font.to_c_source())
