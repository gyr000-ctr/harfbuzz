#!/usr/bin/env python3
"""Generate deterministic EBDT/EBLC test fonts for HarfBuzz subset tests.

The fonts exercise both EBLC index subtable formats the HarfBuzz subsetter
can emit (1 with 32-bit offsets, 3 with 16-bit offsets), multiple strikes,
glyph gaps (glyphs without bitmaps) and non-consecutive retained glyphs so
that GID remapping and index/offset rebuilding are all covered.

Run with an isolated interpreter (python3 -I) from any directory; output
paths are passed as arguments.
"""

import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.ttLib import newTable
from fontTools.ttLib.tables import E_B_D_T_, E_B_L_C_
from fontTools.ttLib.tables.BitmapGlyphMetrics import SmallGlyphMetrics


# name -> codepoint; .notdef plus nine glyphs, some intentionally without
# bitmaps to create gaps inside the index subtable ranges.
UNICODES = {
    "A": 0x41,
    "B": 0x42,
    "C": 0x43,
    "D": 0x44,
    "E": 0x45,
    "F": 0x46,
    "G": 0x47,
    "H": 0x48,
    "I": 0x49,
}
GLYPH_ORDER = [".notdef"] + list(UNICODES.keys())

# Glyphs without bitmap data in each strike.
GAPS = {"C", "G"}

# (ppem, indexFormat, imageFormat, width, height)
STRIKES = [
    (8, 1, 1, 5, 7),    # narrow rows -> format-1 index (32-bit offsets)
    (16, 3, 1, 13, 15),  # wider rows, 16-bit offsets -> format-3 index
]


def make_metrics(ppem, width, height):
    m = SmallGlyphMetrics()
    m.height = height
    m.width = width
    m.BearingX = 0
    m.BearingY = height - 1
    m.Advance = width + 1
    return m


def make_image_data(name, ppem, width, height):
    """Byte-aligned, bitDepth=1 bitmap; each glyph gets a unique pattern."""
    row_bytes = (width + 7) // 8
    seed = (ord(name[0]) * 2654435761 + ppem) & 0xFF
    rows = []
    for r in range(height):
        row = bytearray(row_bytes)
        for x in range(width):
            bit = (seed >> (x % 8)) ^ (r * 7 + ord(name[0])) & 1
            if bit & 1:
                row[x // 8] |= 0x80 >> (x % 8)
        rows.append(bytes(row))
    return b"".join(rows)


def line_metrics():
    m = E_B_L_C_.SbitLineMetrics()
    m.ascender = 7
    m.descender = 0
    m.widthMax = 14
    m.caretSlopeNumerator = 0
    m.caretSlopeDenominator = 0
    m.caretOffset = 0
    m.minOriginSB = 0
    m.minAdvanceSB = 0
    m.maxBeforeBL = 7
    m.minAfterBL = 0
    m.pad1 = 0
    m.pad2 = 0
    return m


def build_font(path):
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(GLYPH_ORDER)
    cmap = {cp: name for name, cp in UNICODES.items()}
    fb.setupCharacterMap(cmap)
    # Empty glyf/loca outlines: bitmaps carry the shapes.
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    fb.setupGlyf({g: TTGlyphPen(None).glyph() for g in GLYPH_ORDER})
    fb.setupHorizontalMetrics({g: (500, 0) for g in GLYPH_ORDER})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": "EbdtTest", "styleName": "Regular"})
    fb.setupOS2()
    fb.setupPost()
    fb.setupMaxp()
    # Fixed timestamps so regenerating the font produces byte-identical output.
    fb.font["head"].created = 0
    fb.font["head"].modified = 0

    font = fb.font

    eblc = newTable("EBLC")
    eblc.version = 2.0
    eblc.strikes = []

    ebdt = newTable("EBDT")
    ebdt.version = 2.0
    ebdt.strikeData = []

    for ppem, index_format, image_format, width, height in STRIKES:
        strike = E_B_L_C_.Strike()
        bst = strike.bitmapSizeTable
        bst.hori = line_metrics()
        bst.vert = line_metrics()
        bst.ppemX = ppem
        bst.ppemY = ppem
        bst.bitDepth = 1
        bst.flags = 1
        # Offsets/counts are recalculated by the EBLC compiler.
        bst.indexSubTableArrayOffset = 0
        bst.indexTablesSize = 0
        bst.numberOfIndexSubTables = 1
        bst.colorRef = 0
        bst.startGlyphIndex = 0
        bst.endGlyphIndex = 0

        index_cls = E_B_L_C_.eblc_sub_table_classes[index_format]
        sub = index_cls(None, None)
        sub.indexFormat = index_format
        sub.imageFormat = image_format
        sub.imageDataOffset = 0
        sub.names = [g for g in GLYPH_ORDER if g not in GAPS]
        strike.indexSubTables = [sub]
        eblc.strikes.append(strike)

        glyphs = {}
        for name in sub.names:
            glyph = E_B_D_T_.ebdt_bitmap_classes[image_format](None, None)
            glyph.metrics = make_metrics(ppem, width, height)
            glyph.imageData = make_image_data(name, ppem, width, height)
            glyphs[name] = glyph
        ebdt.strikeData.append(glyphs)

    font["EBLC"] = eblc
    font["EBDT"] = ebdt

    font.save(path)
    font.close()


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: gen-ebdt-font.py <output.ttf>")
    build_font(sys.argv[1])


if __name__ == "__main__":
    main()
