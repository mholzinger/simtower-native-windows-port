#!/usr/bin/env python3
"""Unit tests for convert_winhelp.py on synthetic WinHelp structures built
here (no game data).

Run: python3 tools/test_convert_winhelp.py
"""

from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import convert_winhelp as cw  # noqa: E402


# -- encoders for the compressed integer forms ------------------------------

def cword(value: int) -> bytes:
    if value < 0x80:
        return bytes([value << 1])
    return struct.pack("<H", (value << 1) | 1)


def cshort(value: int) -> bytes:
    if 0 <= value + 0x40 < 0x80:
        return bytes([(value + 0x40) << 1])
    return struct.pack("<H", ((value + 0x4000) << 1) | 1)


def cdword(value: int) -> bytes:
    if value < 0x8000:
        return struct.pack("<H", value << 1)
    return struct.pack("<I", (value << 1) | 1)


def clong(value: int) -> bytes:
    if 0 <= value + 0x4000 < 0x8000:
        return struct.pack("<H", (value + 0x4000) << 1)
    return struct.pack("<I", ((value + 0x40000000) << 1) | 1)


def lz77_literals(data: bytes) -> bytes:
    """A valid LZ77 stream that only uses literals."""
    out = bytearray()
    for start in range(0, len(data), 8):
        out.append(0)
        out += data[start:start + 8]
    return bytes(out)


# -- container builders ----------------------------------------------------

def btree(structure: str, entries: list[bytes], page_size: int = 256,
          split: int | None = None) -> bytes:
    """A B+ tree with one leaf, or (split) an index page and two leaves."""
    def header(root: int, pages: int, levels: int) -> bytes:
        return struct.pack("<HHH16sHHHhHHI", cw.BTREE_MAGIC, 2, page_size,
                           structure.encode(), 0, 0, root, -1, pages,
                           levels, len(entries))

    def page(body: bytes) -> bytes:
        assert len(body) <= page_size
        return body + bytes(page_size - len(body))

    def leaf(items: list[bytes], prev: int, nxt: int) -> bytes:
        return page(struct.pack("<HHhh", 0, len(items), prev, nxt)
                    + b"".join(items))

    if split is None:
        return header(0, 1, 1) + leaf(entries, -1, -1)
    index = page(struct.pack("<HHH", 0, 1, 1) + entries[split][:4]
                 + struct.pack("<H", 2))
    return (header(0, 3, 2) + index + leaf(entries[:split], -1, 2)
            + leaf(entries[split:], 1, -1))


def build_help(files: dict[str, bytes]) -> bytes:
    out = bytearray(16)
    entries = []
    for name in sorted(files):
        entries.append(name.encode() + b"\0" + struct.pack("<i", len(out)))
        body = files[name]
        out += struct.pack("<iiB", len(body), len(body), 0) + body
    directory = btree("z4", entries, page_size=1024)
    directory_offset = len(out)
    out += struct.pack("<iiB", len(directory), len(directory), 4) + directory
    struct.pack_into("<IiiI", out, 0, cw.HELP_MAGIC, directory_offset, -1,
                     len(out))
    return bytes(out)


def system_file(title: str, flags: int = 0, contents: int | None = None,
                minor: int = 21, macros: tuple[str, ...] = ()) -> bytes:
    out = struct.pack("<HHHIH", 0x036C, minor, 1, 0, flags)

    def record(kind: int, body: bytes) -> bytes:
        return struct.pack("<HH", kind, len(body)) + body

    out += record(1, title.encode() + b"\0")
    if contents is not None:
        out += record(3, struct.pack("<i", contents))
    window = struct.pack("<H", 0x0302) + bytes(10) + b"main".ljust(9, b"\0")
    window += bytes(51) + bytes(10) + bytes((255, 255, 224, 0))
    window += bytes((192, 192, 192, 0))
    out += record(6, window)
    for macro in macros:
        out += record(4, macro.encode() + b"\0")
    return out


def link(kind: int, data1: bytes, data2: bytes) -> tuple:
    return (kind, data1, data2)


def topic_file(links: list[tuple], compressed: bool = False,
               header_fields=None) -> tuple[bytes, list[int]]:
    """One topic block. Returns the |TOPIC bytes and each link's
    TOPICPOS. header_fields(index, positions) fills topic headers."""
    sizes = [21 + len(d1) + len(d2) for _, d1, d2 in links]
    positions = []
    pos = cw.TOPIC_BLOCK_HEADER
    for size in sizes:
        positions.append(pos)
        pos += size
    body = bytearray()
    for index, (kind, data1, data2) in enumerate(links):
        if header_fields and kind == cw.RECORD_TOPIC_HEADER:
            data1 = header_fields(index, positions)
        nxt = positions[index + 1] if index + 1 < len(links) else -1
        prev = positions[index - 1] if index else -1
        body += struct.pack("<iiiiiB", 21 + len(data1) + len(data2),
                            len(data2), prev, nxt, 21 + len(data1), kind)
        body += data1 + data2
    header = struct.pack("<iii", positions[-1], positions[0], 0)
    payload = lz77_literals(bytes(body)) if compressed else bytes(body)
    return header + payload, positions


def topic_header(title: str, back: int = -1, forward: int = -1,
                 nonscroll: int = -1, scroll: int = -1) -> tuple:
    data1 = struct.pack("<iiiiiii", 0, back, forward, 0, nonscroll, scroll,
                        -1)
    return link(cw.RECORD_TOPIC_HEADER, data1, title.encode() + b"\0")


def paragraph_info(bits: int = 0, fields: bytes = b"") -> bytes:
    return bytes((0x00, 0x80)) + struct.pack("<HH", 0, bits) + fields


def text_record(info: bytes, commands: bytes, text: bytes) -> tuple:
    data1 = clong(len(text)) + cword(len(text)) + info + commands
    return link(cw.RECORD_DISPLAY, data1, text)


def font_file(faces: list[str], fonts: list[tuple]) -> bytes:
    face_size = 20
    face_offset = 8
    descriptor_offset = face_offset + face_size * len(faces)
    out = struct.pack("<HHHH", len(faces), len(fonts), face_offset,
                      descriptor_offset)
    for face in faces:
        out += face.encode().ljust(face_size, b"\0")
    for attributes, half_points, family, face, rgb in fonts:
        out += struct.pack("<BBBH", attributes, half_points, family, face)
        out += bytes(rgb) + bytes(3)
    return out


def dib_picture(width: int, height: int, rows: list[bytes],
                palette: list[tuple], packing: int = 0,
                hotspots: bytes = b"") -> bytes:
    """An SHG container with one 8-bit DIB (rows top-down)."""
    stride = (width + 3) & ~3
    pixels = b"".join(row.ljust(stride, b"\0") for row in reversed(rows))
    packed = lz77_literals(pixels) if packing == 2 else pixels
    head = bytes((cw.PICTURE_DIB, packing)) + cdword(0) + cdword(0)
    head += cword(1) + cword(8) + cdword(width) + cdword(height)
    head += cdword(len(palette)) + cdword(0) + cdword(len(packed))
    head += cdword(len(hotspots))
    fixed = len(head) + 8 + 4 * len(palette)
    data_offset = fixed
    hotspot_offset = fixed + len(packed) if hotspots else 0
    head += struct.pack("<II", data_offset, hotspot_offset)
    head += b"".join(bytes((b, g, r, 0)) for r, g, b in palette)
    picture = head + packed + hotspots
    return struct.pack("<HHI", cw.SHG_MAGIC, 1, 8) + picture


def shg_hotspots(spots: list[tuple]) -> bytes:
    out = struct.pack("<BHI", 1, len(spots), 0)
    for kind, x, y, w, h, hash_value, _name in spots:
        out += struct.pack("<BBBHHHHI", kind, 0, 0, x, y, w, h, hash_value)
    for spot in spots:
        out += spot[6].encode() + b"\0" + b"\0"
    return out


def png_rows(png: bytes) -> tuple[tuple, list[bytes], bytes]:
    """IHDR fields, unfiltered rows and the PLTE body of a PNG."""
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    chunks = {}
    idat = b""
    while pos < len(png):
        size = struct.unpack(">I", png[pos:pos + 4])[0]
        tag = png[pos + 4:pos + 8]
        body = png[pos + 8:pos + 8 + size]
        crc = struct.unpack(">I", png[pos + 8 + size:pos + 12 + size])[0]
        assert crc == zlib.crc32(tag + body) & 0xFFFFFFFF
        if tag == b"IDAT":
            idat += body
        chunks[tag] = body
        pos += 12 + size
    ihdr = struct.unpack(">IIBBBBB", chunks[b"IHDR"])
    raw = zlib.decompress(idat)
    width, height, _depth, colour = ihdr[:4]
    stride = width * (3 if colour == 2 else 1)
    rows = []
    for y in range(height):
        line = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        assert line[0] == 0
        rows.append(line[1:])
    return ihdr, rows, chunks.get(b"PLTE", b"")


# -- tests -----------------------------------------------------------------

class CompressedIntegerTests(unittest.TestCase):
    def test_round_trips(self) -> None:
        for value in (0, 1, 63, 127, 128, 300, 0x7FFF):
            self.assertEqual(cw.Reader(cword(value)).cword(), value)
        for value in (-64, -1, 0, 63, 64, -65, 1000, -1000):
            self.assertEqual(cw.Reader(cshort(value)).cshort(), value)
        for value in (0, 5, 0x7FFF, 0x8000, 0x123456):
            self.assertEqual(cw.Reader(cdword(value)).cdword(), value)
        for value in (-0x4000, -1, 0, 4, 0x3FFF, 0x4000, -0x5000):
            self.assertEqual(cw.Reader(clong(value)).clong(), value)

    def test_read_past_end_is_a_format_error(self) -> None:
        with self.assertRaises(cw.HelpFormatError):
            cw.Reader(b"\x01").u16()
        with self.assertRaises(cw.HelpFormatError):
            cw.Reader(b"abc").cstring()


class DecompressionTests(unittest.TestCase):
    def test_lz77_literals_and_back_reference(self) -> None:
        # "abc", then distance 3 length 6: "abcabc", then "d".
        token = struct.pack("<H", ((6 - 3) << 12) | (3 - 1))
        data = bytes([0b00001000]) + b"abc" + token + b"d"
        self.assertEqual(cw.lz77_decompress(data), b"abcabcabcd")
        self.assertEqual(cw.lz77_decompress(data, limit=4), b"abca")

    def test_lz77_overlapping_run(self) -> None:
        token = struct.pack("<H", ((18 - 3) << 12) | 0)
        data = bytes([0b00000010]) + b"x" + token
        self.assertEqual(cw.lz77_decompress(data), b"x" * 19)

    def test_lz77_bad_reference(self) -> None:
        with self.assertRaises(cw.HelpFormatError):
            cw.lz77_decompress(bytes([1]) + struct.pack("<H", 5))

    def test_rle(self) -> None:
        data = bytes([3]) + b"z" + bytes([0x82]) + b"pq"
        self.assertEqual(cw.rle_decompress(data), b"zzzpq")
        packed = lz77_literals(data)
        self.assertEqual(cw.decompress_picture(3, packed), b"zzzpq")


class BTreeTests(unittest.TestCase):
    def test_single_leaf(self) -> None:
        tree = btree("L4", [struct.pack("<ii", 7, 70),
                            struct.pack("<ii", -2, 9)])
        self.assertEqual(cw.btree_entries(tree), [(7, 70), (-2, 9)])

    def test_two_levels_walk_every_leaf(self) -> None:
        entries = [struct.pack("<i", n) + f"t{n}".encode() + b"\0"
                   for n in range(5)]
        tree = btree("Lz", entries, page_size=64, split=2)
        self.assertEqual(cw.btree_entries(tree),
                         [(n, f"t{n}") for n in range(5)])

    def test_bad_magic(self) -> None:
        with self.assertRaises(cw.HelpFormatError):
            cw.btree_entries(bytes(64))

    def test_container_directory(self) -> None:
        data = build_help({"|A": b"one", "|B": b"second"})
        box = cw.Container(data)
        self.assertEqual(box.names(), ["|A", "|B"])
        self.assertEqual(box.get("|B"), b"second")
        self.assertIsNone(box.get("|C"))
        with self.assertRaises(cw.HelpFormatError):
            cw.Container(b"\0" * 32)


class SystemAndFontTests(unittest.TestCase):
    def test_system_records(self) -> None:
        log = cw.Log(quiet=True)
        info = cw.parse_system(system_file("Synthetic", flags=4,
                                           contents=0x8000), log)
        self.assertEqual(info.title, "Synthetic")
        self.assertEqual(info.contents, 0x8000)
        self.assertTrue(info.compressed)
        self.assertEqual(info.block_size, 4096)
        self.assertEqual(info.windows[0].name, "main")
        self.assertEqual(info.windows[0].scroll_rgb, (255, 255, 224))
        self.assertEqual(info.windows[0].nonscroll_rgb, (192, 192, 192))
        self.assertEqual(log.warnings, [])

    def test_unknown_system_record_is_logged(self) -> None:
        log = cw.Log(quiet=True)
        data = system_file("T") + struct.pack("<HH", 99, 2) + b"zz"
        cw.parse_system(data, log)
        self.assertEqual(len(log.warnings), 1)

    def test_fonts(self) -> None:
        data = font_file(["Helv", "Courier"],
                         [(0x01, 20, 3, 0, (0, 0, 0)),
                          (0x06, 24, 1, 1, (0, 128, 0))])
        fonts = cw.parse_fonts(data, cw.Log(quiet=True))
        self.assertEqual(fonts[0].face, "Helv")
        self.assertTrue(fonts[0].bold)
        self.assertTrue(fonts[1].italic and fonts[1].underline)
        self.assertEqual(fonts[1].color, (0, 128, 0))
        css = cw.font_css(1, fonts[1])
        self.assertIn("monospace", css)
        self.assertIn("12pt", css)
        self.assertIn("#008000", css)
        self.assertNotIn("font-size", cw.font_css(2, cw.FontDescriptor(
            half_points=0)))


class MacroTests(unittest.TestCase):
    def test_context_hash(self) -> None:
        # 'a' and 'A' weigh 0x11, 'b' 0x12, '1' 0x01, '0' 0x0a, '.' 0x0c.
        self.assertEqual(cw.context_hash("ab"), 0x11 * 43 + 0x12)
        self.assertEqual(cw.context_hash("AB"), cw.context_hash("ab"))
        self.assertEqual(cw.context_hash("1.0"),
                         (0x01 * 43 + 0x0C) * 43 + 0x0A)
        self.assertEqual(cw.context_hash("\x01"), (0x01 - 0x30) & 0xFFFFFFFF)

    def test_split_macros(self) -> None:
        self.assertEqual(
            cw.split_macros("CB(`b', `&Label', `JI(`x.hlp>w', `ctx')')"
                            ":BrowseButtons()"),
            [("CB", ["b", "&Label", "JI(`x.hlp>w', `ctx')"]),
             ("BrowseButtons", [])])
        self.assertEqual(cw.split_macros('PI("", "p")'), [("PI", ["", "p"])])

    def test_macro_target(self) -> None:
        jump = cw.macro_target("JumpId(`HELP.HLP>win', `ab')", "help.hlp")
        self.assertEqual((jump.kind, jump.hash, jump.file, jump.window),
                         ("jump", cw.context_hash("ab"), "", "win"))
        other = cw.macro_target("JI(`other.hlp', `ab')", "help.hlp")
        self.assertEqual(other.file, "other.hlp")
        popup = cw.macro_target("PI(`', `ab')", "help.hlp")
        self.assertEqual(popup.kind, "popup")
        self.assertEqual(cw.macro_target("JC()", "x").offset, -2)
        self.assertIsNone(cw.macro_target("Beep()", "x"))


class PhraseTests(unittest.TestCase):
    def phrases_file(self, compressed: bool) -> bytes:
        words = [b"alpha", b"beta", b"gamma"]
        offsets = []
        position = 2 * (len(words) + 1)
        for word in words:
            offsets.append(position)
            position += len(word)
        offsets.append(position)
        text = b"".join(words)
        out = struct.pack("<HH", len(words), 0x0100)
        if compressed:
            out += struct.pack("<i", len(text))
            text = lz77_literals(text)
        return out + struct.pack(f"<{len(offsets)}H", *offsets) + text

    def test_parse_and_expand(self) -> None:
        for compressed in (False, True):
            system = cw.SystemInfo(minor=21, flags=4 if compressed else 0)
            phrases = cw.parse_phrases(self.phrases_file(compressed), system)
            self.assertEqual(phrases, [b"alpha", b"beta", b"gamma"])
            # phrase 1 + space, literal, phrase 2
            data = bytes([1, 3]) + b"x" + bytes([1, 4])
            self.assertEqual(cw.expand_phrases(data, phrases),
                             b"beta xgamma")
            with self.assertRaises(cw.HelpFormatError):
                cw.expand_phrases(bytes([1, 9]), phrases)


def sample_topics(compressed: bool = False):
    """Two topics: the first has a jump, a popup and formatting; the
    second is the jump / popup target."""
    commands = b""
    commands += b"\x80" + struct.pack("<H", 1)          # font 1
    commands += b"\xe3" + struct.pack("<I", 0x2222)     # jump
    commands += b"\x89"                                 # hotspot end
    commands += b"\x83"                                 # tab
    commands += b"\xe2" + struct.pack("<I", 0x2222)     # popup
    commands += b"\x89"
    commands += b"\x81"                                 # line break
    commands += b"\x82"                                 # paragraph
    commands += b"\xff"
    text = b"Lead \0\0Go\0\0\0Peek\0\0\0Second para\0"
    info = paragraph_info(0x0850, cshort(20) + cshort(-10))
    links = [
        topic_header("First"),
        text_record(info, commands, text),
        topic_header("Second"),
        text_record(paragraph_info(), b"\x80\x00\x00\xff",
                    b"\0Target text\0"),
    ]

    def fields(index, positions):
        back = -1 if index == 0 else 0
        forward = len(text) if index == 0 else -1
        return struct.pack("<iiiiiii", 0, back, forward, 0, -1,
                           positions[index + 1], -1)

    data, positions = topic_file(links, compressed, fields)
    return data, positions, len(text)


class TopicTests(unittest.TestCase):
    def read(self, compressed: bool):
        data, positions, text_size = sample_topics(compressed)
        system = cw.SystemInfo(minor=21, flags=4 if compressed else 0)
        log = cw.Log(quiet=True)
        stream = cw.TopicStream(data, system, log)
        return cw.read_topics(stream, None, system, log), log, text_size

    def test_topics_and_offsets(self) -> None:
        for compressed in (False, True):
            topics, log, text_size = self.read(compressed)
            self.assertEqual(log.warnings, [])
            self.assertEqual([t.title for t in topics], ["First", "Second"])
            self.assertEqual(topics[0].offset, 0)
            # Characters of the text record, block 0.
            self.assertEqual(topics[1].offset, text_size)
            self.assertEqual(topics[0].browse_forward, text_size)

    def test_paragraph_items(self) -> None:
        topics, _log, _size = self.read(False)
        first, second = topics[0].scroll
        self.assertEqual(first.align, "center")
        self.assertEqual(first.left, 200)
        self.assertEqual(first.first, -100)
        kinds = [type(item).__name__ for item in first.items]
        self.assertEqual(kinds, ["Text", "Text", "Tab", "Text",
                                 "LineBreak"])
        self.assertEqual(first.items[1].text, "Go")
        self.assertEqual(first.items[1].font, 1)
        self.assertEqual(first.items[1].target.kind, "jump")
        self.assertEqual(first.items[1].target.hash, 0x2222)
        self.assertIsNone(first.items[2].target)
        self.assertEqual(first.items[3].target.kind, "popup")
        self.assertEqual(second.items[0].text, "Second para")
        self.assertEqual(second.offset,
                         len(b"Lead \0\0Go\0\0\0Peek\0\0\0"))

    def test_nonscrolling_region(self) -> None:
        links = [topic_header("T"),
                 text_record(paragraph_info(), b"\xff", b"Fixed\0"),
                 text_record(paragraph_info(), b"\xff", b"Body\0")]

        def fields(index, positions):
            return struct.pack("<iiiiiii", 0, -1, -1, 0, positions[1],
                               positions[2], -1)

        data, _ = topic_file(links, header_fields=fields)
        system = cw.SystemInfo(minor=21, flags=0)
        log = cw.Log(quiet=True)
        topic, = cw.read_topics(cw.TopicStream(data, system, log), None,
                                system, log)
        self.assertEqual(topic.nonscroll[0].items[0].text, "Fixed")
        self.assertEqual(topic.scroll[0].items[0].text, "Body")

    def test_picture_hotspots_count_as_characters(self) -> None:
        picture = b"\x86\x22" + clong(4) + cword(3) + struct.pack("<HH", 0,
                                                                   2)
        links = [topic_header("A"),
                 text_record(paragraph_info(), picture + b"\xff",
                             b"x\0\0"),
                 topic_header("B")]
        data, _ = topic_file(links)
        system = cw.SystemInfo(minor=21, flags=0)
        log = cw.Log(quiet=True)
        topics = cw.read_topics(cw.TopicStream(data, system, log), None,
                                system, log)
        self.assertEqual(topics[1].offset, 3 + 3)
        picture_item = topics[0].scroll[0].items[1]
        self.assertEqual((picture_item.number, picture_item.hotspots),
                         (2, 3))
        self.assertEqual(log.warnings, [])

    def test_table_record(self) -> None:
        data1 = clong(0) + cword(4) + bytes((2, 0)) + struct.pack(
            "<h", 100) + struct.pack("<hhhh", 1000, 50, 2000, 50)
        data1 += struct.pack("<hhB", 0, 0, 0) + paragraph_info() + b"\xff"
        data1 += struct.pack("<hhB", 1, 0, 0) + paragraph_info() + b"\xff"
        data1 += struct.pack("<h", -1)
        links = [topic_header("T"),
                 link(cw.RECORD_TABLE, data1, b"a\0b\0")]
        data, _ = topic_file(links)
        system = cw.SystemInfo(minor=21, flags=0)
        log = cw.Log(quiet=True)
        topic, = cw.read_topics(cw.TopicStream(data, system, log), None,
                                system, log)
        table = topic.scroll[0]
        self.assertIsInstance(table, cw.Table)
        self.assertEqual(table.widths, [1000, 2000])
        self.assertEqual(table.cells[0][0].items[0].text, "a")
        self.assertEqual(table.cells[1][0].items[0].text, "b")

    def test_unknown_command_and_record_are_logged(self) -> None:
        links = [topic_header("T"),
                 text_record(paragraph_info(), b"\x99\xff",
                             b"kept\0lost?\0"),
                 link(0x42, b"", b"")]
        data, _ = topic_file(links)
        system = cw.SystemInfo(minor=21, flags=0)
        log = cw.Log(quiet=True)
        topic, = cw.read_topics(cw.TopicStream(data, system, log), None,
                                system, log)
        self.assertEqual(len(log.warnings), 2)
        texts = [item.text for item in topic.scroll[0].items]
        self.assertEqual(texts[0], "kept")

    def test_truncated_record_falls_back_to_text(self) -> None:
        links = [topic_header("T"),
                 text_record(paragraph_info(0x0002), b"", b"plain\0")]
        data, _ = topic_file(links)
        system = cw.SystemInfo(minor=21, flags=0)
        log = cw.Log(quiet=True)
        topic, = cw.read_topics(cw.TopicStream(data, system, log), None,
                                system, log)
        self.assertEqual(topic.scroll[0].items[0].text, "plain")
        self.assertEqual(len(log.warnings), 1)


class PictureTests(unittest.TestCase):
    PALETTE = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
    ROWS = [bytes((0, 1, 2)), bytes((2, 1, 0))]

    def test_dib_packings(self) -> None:
        for packing in (0, 2):
            data = dib_picture(3, 2, self.ROWS, self.PALETTE, packing)
            image = cw.decode_picture_file(data, cw.Log(quiet=True), "t")
            self.assertEqual((image.width, image.height), (3, 2))
            self.assertEqual(image.rows, self.ROWS)
            self.assertEqual(image.palette, self.PALETTE)

    def test_hotspots(self) -> None:
        spots = shg_hotspots([(0xE3, 1, 2, 3, 4, 0xABCD, "ctx")])
        data = dib_picture(3, 2, self.ROWS, self.PALETTE, 0, spots)
        image = cw.decode_picture_file(data, cw.Log(quiet=True), "t")
        spot, = image.hotspots
        self.assertEqual((spot.kind, spot.x, spot.y, spot.width,
                          spot.height, spot.hash, spot.name),
                         (0xE3, 1, 2, 3, 4, 0xABCD, "ctx"))

    def test_monochrome_ddb_is_top_down(self) -> None:
        pixels = bytes((0b10100000, 0, 0b01000000, 0))  # word-aligned rows
        head = bytes((cw.PICTURE_DDB, 0)) + cdword(0) + cdword(0)
        head += cword(1) + cword(1) + cdword(3) + cdword(2) + cdword(0)
        head += cdword(0) + cdword(len(pixels)) + cdword(0)
        head += struct.pack("<II", len(head) + 8, 0)
        data = struct.pack("<HHI", cw.SHG_MAGIC, 1, 8) + head + pixels
        image = cw.decode_picture_file(data, cw.Log(quiet=True), "t")
        self.assertEqual(image.rows, [bytes((1, 0, 1)), bytes((0, 1, 0))])
        self.assertEqual(image.palette, [(0, 0, 0), (255, 255, 255)])

    def test_metafile_and_garbage_are_skipped(self) -> None:
        log = cw.Log(quiet=True)
        metafile = struct.pack("<HHI", cw.SHG_MAGIC, 1, 8) + bytes((8, 0))
        self.assertIsNone(cw.decode_picture_file(metafile, log, "m"))
        self.assertIsNone(cw.decode_picture_file(b"\x01\x02\x03", log, "g"))
        truncated = dib_picture(3, 2, self.ROWS, self.PALETTE)[:20]
        self.assertIsNone(cw.decode_picture_file(truncated, log, "t"))
        self.assertEqual(len(log.warnings), 3)

    def test_bmp(self) -> None:
        width, height = 2, 2
        pixels = bytes((0, 0, 255, 0, 255, 0, 0, 0,   # bottom: red, green
                        255, 0, 0, 0, 0, 0, 0, 0))    # top: blue, black
        info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, 0,
                           0, 0, 0, 0)
        data = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
        image = cw.decode_picture_file(data + info + pixels,
                                       cw.Log(quiet=True), "b")
        self.assertIsNone(image.palette)
        self.assertEqual(image.rows, [bytes((0, 0, 255, 0, 0, 0)),
                                      bytes((255, 0, 0, 0, 255, 0))])

    def test_png_encoder(self) -> None:
        image = cw.Image(3, 2, self.ROWS, self.PALETTE)
        ihdr, rows, plte = png_rows(cw.png_bytes(image))
        self.assertEqual(ihdr, (3, 2, 8, 3, 0, 0, 0))
        self.assertEqual(rows, self.ROWS)
        self.assertEqual(plte, bytes((255, 0, 0, 0, 255, 0, 0, 0, 255)))
        rgb = cw.Image(1, 1, [bytes((1, 2, 3))], None)
        ihdr, rows, plte = png_rows(cw.png_bytes(rgb))
        self.assertEqual(ihdr[3], 2)
        self.assertEqual(rows, [bytes((1, 2, 3))])
        self.assertEqual(plte, b"")


class ConvertTests(unittest.TestCase):
    def build(self) -> bytes:
        topics, _positions, text_size = sample_topics(compressed=True)
        second = cw.context_hash("second")
        context = btree("L4", [struct.pack("<ii", 0x1111, 0),
                               struct.pack("<ii", 0x2222, text_size),
                               struct.pack("<I", second)
                               + struct.pack("<i", text_size)])
        titles = btree("Lz", [struct.pack("<i", 0) + b"First\0",
                              struct.pack("<i", text_size) + b"Second\0"])
        spots = shg_hotspots([(0xE3, 0, 0, 2, 2, 0x2222, "x")])
        return build_help({
            "|SYSTEM": system_file(
                "Synthetic Help", flags=4,
                macros=("CB(`b', `&Next', `JI(`', `second')')",
                        "BrowseButtons()")),
            "|TOPIC": topics,
            "|CONTEXT": context,
            "|TTLBTREE": titles,
            "|CTXOMAP": struct.pack("<Hii", 1, 42, text_size),
            "|FONT": font_file(["Arial"], [(0, 20, 3, 0, (0, 0, 0)),
                                           (1, 20, 3, 0, (0, 0, 0))]),
            "|bm0": dib_picture(3, 2, PictureTests.ROWS,
                                PictureTests.PALETTE, 2, spots),
            "|XYZ": b"unknown",
        })

    def test_end_to_end(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            log = cw.Log(quiet=True)
            result = cw.convert(self.build(), out, log)
            self.assertEqual((result.topics, result.bitmaps), (2, 1))
            self.assertEqual(log.warnings, ["internal file |XYZ skipped"])
            first = (out / "topic-000.html").read_text(encoding="utf-8")
            self.assertIn('<a class="jump" href="topic-001.html">', first)
            self.assertIn('href="#popup-001"', first)
            self.assertIn('id="popup-001"', first)
            self.assertIn('<a href="topic-001.html">&gt;&gt;</a>', first)
            self.assertIn("text-align: center", first)
            self.assertIn('<a href="topic-001.html">Next</a>', first)
            self.assertIn("--scroll: #ffffe0", (out / "style.css").read_text(
                encoding="utf-8"))
            self.assertIn("topic-000.html",
                          (out / "contents.html").read_text(encoding="utf-8"))
            index = (out / "index.html").read_text(encoding="utf-8")
            self.assertIn('href="topic-001.html"', index)
            help_map = json.loads((out / "help-map.json").read_text(
                encoding="utf-8"))
            self.assertEqual(help_map["contents"], "topic-000.html")
            self.assertEqual(help_map["contexts"],
                             {"0x00001111": "topic-000.html",
                              "0x00002222": "topic-001.html",
                              f"0x{cw.context_hash('second'):08x}":
                                  "topic-001.html"})
            self.assertEqual(help_map["map"], {"42": "topic-001.html"})
            png = (out / "images" / "bm0.png").read_bytes()
            self.assertEqual(png_rows(png)[1], PictureTests.ROWS)

    def test_macro_hotspot_link(self) -> None:
        topic = cw.Topic(number=0, offset=0, pos=12, title="Only")
        writer = cw.HtmlWriter("T", [topic], {cw.context_hash("x"): 0}, [],
                               cw.SystemInfo(), cw.Log(quiet=True), "h.hlp")
        popups: set[int] = set()
        start, _ = writer.link_open(cw.Target("macro", macro="PI(`', `x')"),
                                    popups)
        self.assertEqual(start, '<a class="popup-link" href="#popup-000">')
        self.assertEqual(popups, {0})
        log = cw.Log(quiet=True)
        writer.log = log
        start, _ = writer.link_open(cw.Target("macro", macro="Beep()"),
                                    popups)
        self.assertTrue(start.startswith('<span class="macro"'))
        self.assertEqual(len(log.warnings), 1)

    def test_command_line(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "x.hlp"
            source.write_bytes(self.build())
            out = Path(directory) / "out"
            sink = io.StringIO()
            with contextlib.redirect_stdout(sink), \
                    contextlib.redirect_stderr(sink):
                self.assertEqual(cw.main([str(source), str(out)]), 0)
                self.assertEqual(cw.main([str(source), str(out),
                                          "--strict"]), 1)
                source.write_bytes(b"not a help file at all")
                self.assertEqual(cw.main([str(source), str(out)]), 1)
            self.assertIn("2 topics, 1 bitmaps, 1 warnings", sink.getvalue())
            self.assertIn("not a WinHelp file", sink.getvalue())

    def test_garbage_never_crashes(self) -> None:
        data = bytearray(self.build())
        with tempfile.TemporaryDirectory() as directory:
            for start in range(16, len(data), 37):
                damaged = bytearray(data)
                damaged[start:start + 8] = b"\xff" * 8
                try:
                    cw.convert(bytes(damaged), Path(directory),
                               cw.Log(quiet=True))
                except (cw.HelpFormatError, struct.error):
                    pass


if __name__ == "__main__":
    unittest.main()
