"""Writes a copy of a TrueType font that Chrome's font sanitizer (OTS) accepts, with the same outlines.

The engine's DroidSansFallback.ttf sets bit 7 of some simple-glyph flags. That bit is reserved by the
OpenType spec and FreeType ignores it, but OTS rejects the whole font ("glyf: Bad glyph flag (183),
reserved bit 7 must be set to zero"), so Chrome falls back to a system font and the CJK cases compare
DreamGUI and Slate against a different typeface. Clearing the bit changes no outline, metric or
length, so the copy draws exactly what DreamGUI and Slate draw from the original.

Usage: python chrome_safe_font.py <in.ttf> <out.ttf>
Prints how many flag bytes it cleared; exits 0 with an unchanged copy for fonts that need nothing
(collections, CFF fonts and fonts without glyf are copied as they are).
"""
import shutil
import struct
import sys


def read_tables(data):
    num_tables = struct.unpack_from('>H', data, 4)[0]
    tables = {}
    for i in range(num_tables):
        tag, _checksum, offset, length = struct.unpack_from('>4sIII', data, 12 + 16 * i)
        tables[tag.decode('latin-1')] = (offset, length)
    return tables


def clear_reserved_flag_bits(data):
    tables = read_tables(data)
    if 'glyf' not in tables or 'loca' not in tables or 'head' not in tables or 'maxp' not in tables:
        return 0
    head_offset = tables['head'][0]
    long_loca = struct.unpack_from('>h', data, head_offset + 50)[0] == 1
    num_glyphs = struct.unpack_from('>H', data, tables['maxp'][0] + 4)[0]
    loca_offset = tables['loca'][0]
    glyf_offset = tables['glyf'][0]
    cleared = 0
    for glyph in range(num_glyphs):
        if long_loca:
            start, end = struct.unpack_from('>II', data, loca_offset + 4 * glyph)
        else:
            start, end = (value * 2 for value in struct.unpack_from('>HH', data, loca_offset + 2 * glyph))
        if end <= start:
            continue
        at = glyf_offset + start
        contours = struct.unpack_from('>h', data, at)[0]
        if contours <= 0:
            continue  # composite glyphs carry no point flags
        at += 10
        end_points = struct.unpack_from('>%dH' % contours, data, at)
        at += 2 * contours
        instruction_length = struct.unpack_from('>H', data, at)[0]
        at += 2 + instruction_length
        points = end_points[-1] + 1
        while points > 0:
            flag = data[at]
            if flag & 0x80:
                data[at] = flag & 0x7F
                cleared += 1
            at += 1
            repeat = 0
            if flag & 0x08:
                repeat = data[at]
                at += 1
            points -= 1 + repeat
    return cleared


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    source, target = sys.argv[1], sys.argv[2]
    with open(source, 'rb') as handle:
        data = bytearray(handle.read())
    if data[:4] not in (b'\x00\x01\x00\x00', b'true'):
        shutil.copyfile(source, target)
        print('copied unchanged (not a TrueType font)')
        return 0
    cleared = clear_reserved_flag_bits(data)
    with open(target, 'wb') as handle:
        handle.write(data)
    print('cleared %d reserved flag byte(s)' % cleared)
    return 0


if __name__ == '__main__':
    sys.exit(main())
