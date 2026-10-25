#!/usr/bin/env python3
"""Convert a WinHelp 3.x help file (.HLP) to static HTML pages.

Python 3 standard library only. The output (one page per topic, an index,
PNG bitmaps, a stylesheet and help-map.json) is derived from the user's own
copy of the game and must stay out of version control.

Handled: the internal file system and its B+ trees, |SYSTEM (title,
contents topic, windows), |TOPIC (LZ77 blocks, topic headers, text and
table records, phrase compression through |Phrases), |FONT, |CONTEXT,
|CTXOMAP, |TTLBTREE, the keyword index (|KWBTREE, |KWDATA) and |bm*
pictures (SHG/MRB device-independent and device-dependent bitmaps, plain
BMP), converted to PNG. Anything else is logged and skipped.

Usage: convert_winhelp.py HELPFILE OUTDIR [--quiet] [--strict]
"""

from __future__ import annotations

import argparse
import html
import json
import re
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterator


class HelpFormatError(ValueError):
    """The data is not a readable WinHelp structure."""


class Log:
    """Collects warnings: unknown records are reported, never fatal."""

    def __init__(self, quiet: bool = False) -> None:
        self.quiet = quiet
        self.warnings: list[str] = []
        self._seen: dict[str, int] = {}

    def warn(self, message: str, key: str | None = None) -> None:
        # Repeats of one kind of problem are counted, printed once.
        key = key or message
        count = self._seen.get(key, 0)
        self._seen[key] = count + 1
        self.warnings.append(message)
        if not self.quiet and count == 0:
            print(f"convert_winhelp: warning: {message}", file=sys.stderr)

    def repeats(self) -> dict[str, int]:
        return {key: n for key, n in self._seen.items() if n > 1}


class Reader:
    """Little-endian reader over bytes with WinHelp compressed integers."""

    def __init__(self, data: bytes, pos: int = 0, end: int | None = None):
        self.data = data
        self.pos = pos
        self.end = len(data) if end is None else end

    def left(self) -> int:
        return self.end - self.pos

    def take(self, size: int) -> bytes:
        if size < 0 or self.pos + size > self.end:
            raise HelpFormatError(
                f"read past end at 0x{self.pos:x} (+{size})")
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        return value

    def u8(self) -> int:
        return self.take(1)[0]

    def i8(self) -> int:
        return struct.unpack("<b", self.take(1))[0]

    def u16(self) -> int:
        return struct.unpack("<H", self.take(2))[0]

    def i16(self) -> int:
        return struct.unpack("<h", self.take(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def i32(self) -> int:
        return struct.unpack("<i", self.take(4))[0]

    # Compressed unsigned short: one byte (bit 0 clear) or two, value >> 1.
    def cword(self) -> int:
        low = self.u8()
        if low & 1:
            return ((self.u8() << 8) | low) >> 1
        return low >> 1

    # Compressed signed short: the unsigned form biased by 0x4000 (two
    # bytes) or 0x40 (one byte).
    def cshort(self) -> int:
        low = self.u8()
        if low & 1:
            return (((self.u8() << 8) | low) >> 1) - 0x4000
        return (low >> 1) - 0x40

    # Compressed unsigned long: two bytes (bit 0 clear) or four, value >> 1.
    def cdword(self) -> int:
        low = self.u16()
        if low & 1:
            return ((self.u16() << 16) | low) >> 1
        return low >> 1

    # Compressed signed long: biased by 0x40000000 or 0x4000.
    def clong(self) -> int:
        low = self.u16()
        if low & 1:
            return (((self.u16() << 16) | low) >> 1) - 0x40000000
        return (low >> 1) - 0x4000

    def cstring(self, encoding: str = "cp1252") -> str:
        stop = self.data.find(b"\0", self.pos, self.end)
        if stop < 0:
            raise HelpFormatError(f"unterminated string at 0x{self.pos:x}")
        value = self.data[self.pos:stop].decode(encoding, errors="replace")
        self.pos = stop + 1
        return value


# ---------------------------------------------------------------------------
# Decompression


def lz77_decompress(data: bytes, limit: int | None = None) -> bytes:
    """WinHelp LZ77: a flag byte, then 8 items, a set bit marks a 16-bit
    back reference (low 12 bits distance - 1, high 4 bits length - 3)."""
    out = bytearray()
    pos = 0
    size = len(data)
    while pos < size:
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if pos >= size or (limit is not None and len(out) >= limit):
                break
            if flags & (1 << bit):
                if pos + 1 >= size:
                    pos = size
                    break
                token = data[pos] | (data[pos + 1] << 8)
                pos += 2
                length = (token >> 12) + 3
                start = len(out) - (token & 0x0FFF) - 1
                if start < 0:
                    raise HelpFormatError("LZ77 reference before the start")
                for index in range(length):
                    out.append(out[start + index])
            else:
                out.append(data[pos])
                pos += 1
        if limit is not None and len(out) >= limit:
            break
    return bytes(out if limit is None else out[:limit])


def rle_decompress(data: bytes) -> bytes:
    """Picture run length: n & 0x80 copies n & 0x7f bytes, else repeats the
    next byte n times."""
    out = bytearray()
    pos = 0
    while pos < len(data):
        count = data[pos]
        pos += 1
        length = count & 0x7F
        if count & 0x80:
            out += data[pos:pos + length]
            pos += length
        elif pos < len(data):
            out += bytes([data[pos]]) * length
            pos += 1
    return bytes(out)


def decompress_picture(method: int, data: bytes) -> bytes:
    if method not in (0, 1, 2, 3):
        raise HelpFormatError(f"unknown picture packing {method}")
    if method & 2:
        data = lz77_decompress(data)
    if method & 1:
        data = rle_decompress(data)
    return data


# ---------------------------------------------------------------------------
# Container: the 16-byte header, the internal directory and B+ trees

HELP_MAGIC = 0x00035F3F
BTREE_MAGIC = 0x293B
BTREE_HEADER_SIZE = 38
FILE_HEADER_SIZE = 9


def read_internal_file(data: bytes, offset: int, log: Log | None = None,
                       name: str = "") -> bytes:
    """An internal file: reserved space, used space, flags, then the data.
    A file running past the end is cut there (with a warning) when a log
    is given."""
    if offset < 0 or offset + FILE_HEADER_SIZE > len(data):
        raise HelpFormatError(f"internal file at 0x{offset:x} out of range")
    _reserved, used, _flags = struct.unpack_from("<iiB", data, offset)
    start = offset + FILE_HEADER_SIZE
    if used < 0 or start + used > len(data):
        if log is None or used < 0:
            raise HelpFormatError(
                f"internal file at 0x{offset:x} is truncated")
        log.warn(f"{name}: truncated, {len(data) - start} of {used} bytes")
        used = len(data) - start
    return data[start:start + used]


FieldParser = Callable[[Reader], object]


def structure_parsers(structure: str) -> list[FieldParser]:
    """Field readers for a B+ tree structure string ('z4', 'L4', 'Lz')."""
    parsers: list[FieldParser] = []
    for code in structure:
        if code in "L4":
            parsers.append(Reader.i32)
        elif code == "2":
            parsers.append(Reader.u16)
        elif code == "1":
            parsers.append(Reader.u8)
        elif code in "ziF":
            parsers.append(Reader.cstring)
        else:
            raise HelpFormatError(f"unknown B+ tree field '{code}'")
    return parsers


def btree_entries(tree: bytes, log: Log | None = None,
                  name: str = "") -> list[tuple]:
    """Every leaf entry of a B+ tree, in key order."""
    header = Reader(tree)
    magic = header.u16()
    if magic != BTREE_MAGIC:
        raise HelpFormatError(f"{name}: bad B+ tree magic 0x{magic:04x}")
    _flags = header.u16()
    page_size = header.u16()
    structure = header.take(16).split(b"\0")[0].decode("ascii", "replace")
    header.u16()  # must be zero
    header.u16()  # page splits
    root = header.u16()
    header.i16()  # must be -1
    total_pages = header.u16()
    levels = header.u16()
    _total_entries = header.u32()
    parsers = structure_parsers(structure)
    if page_size <= 8:
        raise HelpFormatError(f"{name}: bad page size {page_size}")

    def page_reader(page: int) -> Reader:
        if page < 0 or page >= total_pages:
            raise HelpFormatError(f"{name}: page {page} out of range")
        start = BTREE_HEADER_SIZE + page * page_size
        if start + page_size > len(tree):
            raise HelpFormatError(f"{name}: page {page} is truncated")
        return Reader(tree, start, start + page_size)

    # Index pages: unused, count, first child; follow the first children.
    page = root
    for _ in range(max(levels - 1, 0)):
        reader = page_reader(page)
        reader.u16()
        reader.u16()
        page = reader.u16()
    entries: list[tuple] = []
    seen: set[int] = set()
    while page not in seen and 0 <= page < total_pages:
        seen.add(page)
        reader = page_reader(page)
        reader.u16()  # unused bytes
        count = reader.u16()
        reader.i16()  # previous page
        next_page = reader.i16()
        for _ in range(count):
            try:
                entries.append(tuple(parse(reader) for parse in parsers))
            except HelpFormatError as error:
                if log:
                    log.warn(f"{name}: page {page}: {error}")
                break
        page = next_page
    return entries


class Container:
    """The internal file system of a .HLP file."""

    def __init__(self, data: bytes, log: Log | None = None):
        if len(data) < 16:
            raise HelpFormatError("file too short for a WinHelp header")
        magic, directory, _free, _size = struct.unpack_from("<IiiI", data)
        if magic != HELP_MAGIC:
            raise HelpFormatError(f"not a WinHelp file (magic 0x{magic:x})")
        self.data = data
        self.offsets: dict[str, int] = {}
        tree = read_internal_file(data, directory)
        for name, offset in btree_entries(tree, log, "directory"):
            self.offsets[name] = offset
        self.log = log

    def names(self) -> list[str]:
        return sorted(self.offsets)

    def get(self, name: str) -> bytes | None:
        offset = self.offsets.get(name)
        if offset is None:
            return None
        return read_internal_file(self.data, offset, self.log, name)


# ---------------------------------------------------------------------------
# |SYSTEM


@dataclass
class HelpWindow:
    name: str = ""
    caption: str = ""
    kind: str = ""
    scroll_rgb: tuple[int, int, int] | None = None
    nonscroll_rgb: tuple[int, int, int] | None = None


@dataclass
class SystemInfo:
    minor: int = 21
    flags: int = 0
    title: str = ""
    copyright: str = ""
    contents: int | None = None  # TOPICOFFSET
    macros: list[str] = field(default_factory=list)
    windows: list[HelpWindow] = field(default_factory=list)
    charset: int | None = None

    @property
    def compressed(self) -> bool:
        return self.minor > 16 and bool(self.flags & 0x0C)

    @property
    def block_size(self) -> int:
        if self.minor <= 16:
            return 2048
        return 2048 if self.flags & 0x08 else 4096


def parse_window(data: bytes) -> HelpWindow:
    reader = Reader(data)
    flags = reader.u16()

    def text(size: int) -> str:
        raw = reader.take(size).split(b"\0")[0]
        return raw.decode("cp1252", "replace")

    window = HelpWindow()
    kind = text(10)
    name = text(9)
    caption = text(51)
    window.kind = kind if flags & 0x0001 else ""
    window.name = name if flags & 0x0002 else ""
    window.caption = caption if flags & 0x0004 else ""
    reader.take(10)  # x, y, width, height, maximize
    scroll = reader.take(4)
    nonscroll = reader.take(4)
    if flags & 0x0100:
        window.scroll_rgb = (scroll[0], scroll[1], scroll[2])
    if flags & 0x0200:
        window.nonscroll_rgb = (nonscroll[0], nonscroll[1], nonscroll[2])
    return window


def parse_system(data: bytes, log: Log) -> SystemInfo:
    reader = Reader(data)
    magic = reader.u16()
    if magic != 0x036C:
        raise HelpFormatError(f"|SYSTEM: bad magic 0x{magic:04x}")
    info = SystemInfo()
    info.minor = reader.u16()
    reader.u16()  # major
    reader.u32()  # generation date
    info.flags = reader.u16()
    if info.minor <= 16:
        info.title = reader.cstring()
        return info
    while reader.left() >= 4:
        kind = reader.u16()
        size = reader.u16()
        if size > reader.left():
            log.warn(f"|SYSTEM: record {kind} is truncated")
            break
        body = reader.take(size)
        sub = Reader(body)
        try:
            if kind == 1:
                info.title = sub.cstring()
            elif kind == 2:
                info.copyright = sub.cstring()
            elif kind == 3 and size >= 4:
                info.contents = sub.i32()
            elif kind == 4:
                info.macros.append(sub.cstring())
            elif kind == 6 and size >= 90:
                info.windows.append(parse_window(body))
            elif kind == 11 and size >= 1:
                info.charset = body[0]
            elif kind in (5, 8, 9, 10, 12, 13, 14, 15, 18, 19):
                pass  # icon, citation, locale, index options: not shown
            else:
                log.warn(f"|SYSTEM: unknown record type {kind} skipped",
                         "system-record")
        except HelpFormatError as error:
            log.warn(f"|SYSTEM: record {kind}: {error}")
    return info


# ---------------------------------------------------------------------------
# |Phrases (Windows 3.1 phrase compression)


def parse_phrases(data: bytes, system: SystemInfo) -> list[bytes]:
    reader = Reader(data)
    count = reader.u16()
    one_hundred = reader.u16()
    if one_hundred != 0x0100:
        raise HelpFormatError(f"|Phrases: bad marker 0x{one_hundred:04x}")
    decompressed_size = None
    if system.minor > 16 and system.compressed:
        decompressed_size = reader.i32()
    offsets = [reader.u16() for _ in range(count + 1)]
    text = data[reader.pos:]
    if decompressed_size is not None:
        text = lz77_decompress(text, decompressed_size)
    base = offsets[0]
    phrases = []
    for index in range(count):
        start = offsets[index] - base
        end = offsets[index + 1] - base
        if start < 0 or end < start or end > len(text):
            raise HelpFormatError(f"|Phrases: phrase {index} out of range")
        phrases.append(text[start:end])
    return phrases


def expand_phrases(data: bytes, phrases: list[bytes]) -> bytes:
    """A byte 1..9 and the next byte select phrase ((c - 1) * 256 + n) / 2,
    followed by a space when that value is odd."""
    out = bytearray()
    pos = 0
    while pos < len(data):
        code = data[pos]
        pos += 1
        if 0 < code < 10 and pos < len(data):
            number = (code - 1) * 256 + data[pos]
            pos += 1
            index = number >> 1
            if index >= len(phrases):
                raise HelpFormatError(f"phrase {index} out of range")
            out += phrases[index]
            if number & 1:
                out.append(0x20)
        else:
            out.append(code)
    return bytes(out)


# ---------------------------------------------------------------------------
# |FONT


@dataclass
class FontDescriptor:
    face: str = ""
    family: int = 0
    half_points: int = 20
    bold: bool = False
    italic: bool = False
    underline: bool = False
    strikeout: bool = False
    double_underline: bool = False
    small_caps: bool = False
    color: tuple[int, int, int] = (0, 0, 0)


def parse_fonts(data: bytes, log: Log) -> list[FontDescriptor]:
    reader = Reader(data)
    face_count = reader.u16()
    descriptor_count = reader.u16()
    face_offset = reader.u16()
    descriptor_offset = reader.u16()
    faces: list[str] = []
    if face_count:
        size = (descriptor_offset - face_offset) // face_count
        for index in range(face_count):
            start = face_offset + index * size
            raw = data[start:start + size].split(b"\0")[0]
            faces.append(raw.decode("cp1252", "replace"))
    fonts: list[FontDescriptor] = []
    record = Reader(data, descriptor_offset)
    for index in range(descriptor_count):
        try:
            attributes = record.u8()
            half_points = record.u8()
            family = record.u8()
            face = record.u16()
            fg = record.take(3)
            record.take(3)  # background
        except HelpFormatError:
            log.warn(f"|FONT: descriptor {index} is truncated")
            break
        fonts.append(FontDescriptor(
            face=faces[face] if face < len(faces) else "",
            family=family,
            half_points=half_points,
            bold=bool(attributes & 0x01),
            italic=bool(attributes & 0x02),
            underline=bool(attributes & 0x04),
            strikeout=bool(attributes & 0x08),
            double_underline=bool(attributes & 0x10),
            small_caps=bool(attributes & 0x20),
            color=(fg[0], fg[1], fg[2]),
        ))
    return fonts


# ---------------------------------------------------------------------------
# |CTXOMAP and the keyword index


def parse_ctxomap(data: bytes) -> list[tuple[int, int]]:
    reader = Reader(data)
    count = reader.u16()
    return [(reader.i32(), reader.i32()) for _ in range(count)]


def parse_keywords(tree: bytes, kwdata: bytes,
                   log: Log) -> list[tuple[str, list[int]]]:
    result: list[tuple[str, list[int]]] = []
    for entry in btree_entries(tree, log, "|KWBTREE"):
        if len(entry) < 3:
            log.warn("|KWBTREE: unexpected entry layout", "kwbtree")
            continue
        keyword, count, offset = entry[0], entry[1], entry[2]
        if offset < 0 or offset + 4 * count > len(kwdata):
            log.warn(f"|KWBTREE: keyword data out of range", "kwdata")
            continue
        offsets = list(struct.unpack_from(f"<{count}i", kwdata, offset))
        result.append((keyword, offsets))
    return result


# ---------------------------------------------------------------------------
# |TOPIC: blocks, links and TOPICOFFSET character counts

TOPIC_BLOCK_HEADER = 12
TOPIC_LINK_HEADER = 21
RECORD_DISPLAY30 = 0x01
RECORD_TOPIC_HEADER = 0x02
RECORD_DISPLAY = 0x20
RECORD_TABLE = 0x23


@dataclass
class TopicLink:
    pos: int            # TOPICPOS of the link
    offset: int         # TOPICOFFSET (block << 15 | character count)
    record_type: int
    data1: bytes        # LinkData1 (after the 21-byte header)
    data2: bytes        # LinkData2, phrases expanded
    next_pos: int
    prev_pos: int


class TopicStream:
    """The |TOPIC blocks decompressed into one stream. A TOPICPOS selects
    a block (pos // span) and an offset in it that counts the 12-byte block
    header; links may run on into the next block."""

    def __init__(self, data: bytes, system: SystemInfo, log: Log):
        self.log = log
        self.block_size = system.block_size
        self.span = 0x4000 if system.compressed else self.block_size
        self.stream = bytearray()
        self.bases: list[int] = []
        self.headers: list[tuple[int, int, int]] = []
        for start in range(0, len(data), self.block_size):
            block = data[start:start + self.block_size]
            if len(block) < TOPIC_BLOCK_HEADER:
                break
            self.headers.append(struct.unpack_from("<iii", block))
            body = block[TOPIC_BLOCK_HEADER:]
            if system.compressed:
                try:
                    body = lz77_decompress(body,
                                           self.span - TOPIC_BLOCK_HEADER)
                except HelpFormatError as error:
                    log.warn(f"|TOPIC block {len(self.bases)}: {error}")
                    body = b""
            self.bases.append(len(self.stream))
            self.stream += body
        self.stream = bytes(self.stream)

    def index(self, pos: int) -> int | None:
        block, offset = divmod(pos, self.span)
        if pos < 0 or block >= len(self.bases) or offset < TOPIC_BLOCK_HEADER:
            return None
        return self.bases[block] + offset - TOPIC_BLOCK_HEADER

    def links(self, phrases: list[bytes] | None) -> Iterator[TopicLink]:
        """Every TOPICLINK in NextBlock order; offset is left at 0 (the
        character count needs the parsed records, see read_topics)."""
        if not self.headers:
            return
        pos = self.headers[0][1]  # FirstTopicLink
        seen: set[int] = set()
        while pos != -1 and pos not in seen:
            seen.add(pos)
            index = self.index(pos)
            if index is None or index + TOPIC_LINK_HEADER > len(self.stream):
                self.log.warn(f"|TOPIC: link 0x{pos:x} out of range")
                return
            size, len2, prev_pos, next_pos, len1, kind = struct.unpack_from(
                "<iiiiiB", self.stream, index)
            if (size < TOPIC_LINK_HEADER or len1 < TOPIC_LINK_HEADER
                    or len1 > size or index + size > len(self.stream)):
                self.log.warn(f"|TOPIC: bad link sizes at 0x{pos:x}")
                return
            data1 = self.stream[index + TOPIC_LINK_HEADER:index + len1]
            raw2 = self.stream[index + len1:index + size]
            data2 = raw2
            if len2 > len(raw2):
                if phrases is None:
                    self.log.warn("|TOPIC: compressed text but no |Phrases",
                                  "no-phrases")
                else:
                    try:
                        data2 = expand_phrases(raw2, phrases)
                    except HelpFormatError as error:
                        self.log.warn(f"|TOPIC: 0x{pos:x}: {error}")
            yield TopicLink(pos=pos, offset=0, record_type=kind,
                            data1=data1, data2=data2[:max(len2, 0)],
                            next_pos=next_pos, prev_pos=prev_pos)
            pos = next_pos


# ---------------------------------------------------------------------------
# Text and table records: paragraphs, character formatting and hotspots


@dataclass
class Target:
    """Where a hotspot leads."""
    kind: str                 # "jump", "popup" or "macro"
    hash: int | None = None   # context hash (HC31)
    offset: int | None = None  # TOPICOFFSET (HC30 jumps)
    file: str = ""
    window: str = ""
    macro: str = ""


@dataclass
class Text:
    text: str
    font: int
    target: Target | None = None


@dataclass
class LineBreak:
    target: Target | None = None


@dataclass
class Tab:
    target: Target | None = None


@dataclass
class Picture:
    number: int | None        # |bm<number>, or None when embedded
    align: str                # "inline", "left" or "right"
    target: Target | None = None
    data: bytes | None = None  # embedded picture bytes
    hotspots: int = 0


@dataclass
class Paragraph:
    offset: int = 0           # TOPICOFFSET of its first character
    items: list = field(default_factory=list)
    align: str = "left"
    left: int = 0             # indents and spacing, in twips
    right: int = 0
    first: int = 0
    above: int = 0
    below: int = 0
    border: bool = False


@dataclass
class Table:
    offset: int = 0
    widths: list[int] = field(default_factory=list)
    cells: dict[int, list[Paragraph]] = field(default_factory=dict)


COMMAND_FONT = 0x80
COMMAND_LINE_BREAK = 0x81
COMMAND_PARAGRAPH = 0x82
COMMAND_TAB = 0x83
COMMAND_HOTSPOT_END = 0x89
COMMAND_NBSP = 0x8B
COMMAND_NBHYPHEN = 0x8C
COMMAND_END = 0xFF
PICTURE_COMMANDS = {0x86: "inline", 0x87: "left", 0x88: "right"}
HASH_JUMPS = {0xE2: "popup", 0xE3: "jump", 0xE6: "popup", 0xE7: "jump"}
OFFSET_JUMPS = {0xE0: "popup", 0xE1: "jump"}
EXTERNAL_JUMPS = {0xEA: "popup", 0xEB: "jump", 0xEE: "popup", 0xEF: "jump"}


class RecordParser:
    """Parses one text (0x20, 0x01) or table (0x23) record into paragraphs.
    Text segments in LinkData2 end with a NUL; each NUL is followed by one
    formatting command from LinkData1. The TOPICOFFSET character count
    advances by one per LinkData2 byte and by the hotspot count of each
    picture with hotspots."""

    def __init__(self, link: TopicLink, windows: list[HelpWindow], log: Log):
        self.link = link
        self.windows = windows
        self.log = log
        self.reader = Reader(link.data1)
        self.text_pos = 0
        self.extra = 0  # picture hotspots counted as characters
        self.font = 0
        self.target: Target | None = None
        self.where = f"record 0x{link.pos:x}"

    def char_offset(self) -> int:
        return self.link.offset + self.text_pos + self.extra

    def characters(self) -> int:
        """The record's share of the TOPICOFFSET count (after parse)."""
        return len(self.link.data2) + self.extra

    def parse(self) -> list:
        kind = self.link.record_type
        reader = self.reader
        reader.clong()  # topic size
        if kind in (RECORD_DISPLAY, RECORD_TABLE):
            reader.cword()  # topic length
        if kind == RECORD_TABLE:
            return [self.parse_table()]
        blocks: list = []
        while reader.left() > 0 and self.text_pos < len(self.link.data2):
            template = self.paragraph_info()
            blocks.extend(self.paragraphs(template))
        return blocks

    def parse_table(self) -> Table:
        reader = self.reader
        table = Table(offset=self.char_offset())
        columns = reader.u8()
        table_type = reader.u8()
        if table_type in (0, 2):
            reader.i16()  # minimum table width
        for _ in range(columns):
            width = reader.i16()
            reader.i16()  # gap
            table.widths.append(width)
        while reader.left() >= 2:
            column = reader.i16()
            if column == -1:
                break
            reader.i16()  # unknown
            reader.u8()   # always 0
            template = self.paragraph_info()
            cell = table.cells.setdefault(column, [])
            cell.extend(self.paragraphs(template))
        return table

    def paragraph_info(self) -> Paragraph:
        reader = self.reader
        reader.u8()   # unknown
        reader.u8()   # unknown, biased
        reader.u16()  # id
        bits = reader.u16()
        paragraph = Paragraph()
        if bits & 0x0001:
            reader.clong()
        # Indents and spacing are stored in units of 10 twips.
        if bits & 0x0002:
            paragraph.above = reader.cshort() * 10
        if bits & 0x0004:
            paragraph.below = reader.cshort() * 10
        if bits & 0x0008:
            reader.cshort()  # line spacing
        if bits & 0x0010:
            paragraph.left = reader.cshort() * 10
        if bits & 0x0020:
            paragraph.right = reader.cshort() * 10
        if bits & 0x0040:
            paragraph.first = reader.cshort() * 10
        if bits & 0x0100:
            reader.u8()   # border flags
            reader.i16()  # border width
            paragraph.border = True
        if bits & 0x0200:
            for _ in range(max(reader.cshort(), 0)):
                stop = reader.cword()
                if stop & 0x4000:
                    reader.cword()  # tab type
        if bits & 0x0400:
            paragraph.align = "right"
        elif bits & 0x0800:
            paragraph.align = "center"
        return paragraph

    def next_text(self) -> str:
        data = self.link.data2
        stop = data.find(b"\0", self.text_pos)
        if stop < 0:
            stop = len(data)
        text = data[self.text_pos:stop].decode("cp1252", "replace")
        self.text_pos = stop + 1
        return text

    def new_paragraph(self, template: Paragraph) -> Paragraph:
        paragraph = Paragraph(**{**template.__dict__, "items": []})
        paragraph.offset = self.char_offset()
        return paragraph

    def paragraphs(self, template: Paragraph) -> list[Paragraph]:
        """Text and commands up to the 0xFF that ends this format."""
        reader = self.reader
        result: list[Paragraph] = []
        current = self.new_paragraph(template)
        while True:
            text = self.next_text()
            if text:
                current.items.append(Text(text, self.font, self.target))
            if reader.left() <= 0:
                break
            command = reader.u8()
            if command == COMMAND_END:
                break
            if command == COMMAND_PARAGRAPH:
                result.append(current)
                current = self.new_paragraph(template)
                continue
            if not self.command(command, current):
                # Unknown length: the rest of this record cannot be read.
                self.log.warn(f"{self.where}: unknown command 0x{command:02x}"
                              ", rest of the record kept as plain text",
                              f"command-{command:02x}")
                rest = self.link.data2[self.text_pos:]
                self.text_pos = len(self.link.data2)
                plain = rest.replace(b"\0", b" ").decode("cp1252", "replace")
                if plain.strip():
                    current.items.append(Text(plain, self.font, None))
                reader.pos = reader.end
                break
        if current.items or not result:
            result.append(current)
        return result

    def command(self, command: int, paragraph: Paragraph) -> bool:
        reader = self.reader
        if command == COMMAND_FONT:
            self.font = reader.u16()
        elif command == COMMAND_LINE_BREAK:
            paragraph.items.append(LineBreak(self.target))
        elif command == COMMAND_TAB:
            paragraph.items.append(Tab(self.target))
        elif command == COMMAND_NBSP:
            paragraph.items.append(Text(" ", self.font, self.target))
        elif command == COMMAND_NBHYPHEN:
            paragraph.items.append(Text("‑", self.font, self.target))
        elif command == COMMAND_HOTSPOT_END:
            self.target = None
        elif command in PICTURE_COMMANDS:
            picture = self.picture(PICTURE_COMMANDS[command])
            if picture is not None:
                paragraph.items.append(picture)
        elif command in HASH_JUMPS:
            self.target = Target(HASH_JUMPS[command], hash=reader.u32())
        elif command in OFFSET_JUMPS:
            self.target = Target(OFFSET_JUMPS[command], offset=reader.i32())
        elif command in EXTERNAL_JUMPS:
            self.target = self.external(EXTERNAL_JUMPS[command])
        elif command in (0xC8, 0xCC):
            size = reader.u16()
            macro = reader.take(size).split(b"\0")[0]
            self.target = Target("macro",
                                 macro=macro.decode("cp1252", "replace"))
        elif command == 0x20:
            reader.i32()  # vfld (multimedia viewer)
        elif command == 0x21:
            reader.i16()  # dtype (multimedia viewer)
        else:
            return False
        return True

    def external(self, kind: str) -> Target:
        reader = self.reader
        size = reader.u16()
        body = Reader(reader.take(size))
        jump_type = body.u8()
        target = Target(kind, hash=body.u32())
        if jump_type == 1:
            number = body.u8()
            if number < len(self.windows):
                target.window = self.windows[number].name
        elif jump_type == 4:
            target.file = body.cstring()
        elif jump_type == 6:
            target.window = body.cstring()
            target.file = body.cstring()
        return target

    def picture(self, align: str) -> Picture | None:
        reader = self.reader
        picture_type = reader.u8()
        size = reader.clong()
        hotspots = 0
        if picture_type == 0x22:
            hotspots = reader.cword()
            self.extra += hotspots
        body = Reader(reader.take(size))
        if picture_type not in (0x03, 0x22):
            self.log.warn(f"{self.where}: picture type 0x{picture_type:02x}"
                          " (embedded window or unknown) skipped",
                          f"picture-type-{picture_type:02x}")
            return None
        embedded = body.u16()
        number = body.u16()
        if embedded:
            return Picture(None, align, self.target,
                           data=body.data[body.pos:], hotspots=hotspots)
        return Picture(number, align, self.target, hotspots=hotspots)


# ---------------------------------------------------------------------------
# Topics


@dataclass
class Topic:
    number: int
    offset: int               # TOPICOFFSET of the topic header
    pos: int                  # TOPICPOS of the topic header
    title: str = ""
    macros: list[str] = field(default_factory=list)
    browse_back: int = -1     # TOPICOFFSET or -1
    browse_forward: int = -1
    nonscroll: list = field(default_factory=list)
    scroll: list = field(default_factory=list)
    scroll_pos: int = -1      # TOPICPOS where the scrolling region starts
    has_nonscroll: bool = False

    def blocks(self) -> list:
        return self.nonscroll + self.scroll


def fallback_paragraph(link: TopicLink) -> Paragraph:
    text = link.data2.replace(b"\0", b" ").decode("cp1252", "replace")
    return Paragraph(items=[Text(text.strip(), 0)] if text.strip() else [])


def read_topics(stream: TopicStream, phrases: list[bytes] | None,
                system: SystemInfo, log: Log) -> list[Topic]:
    topics: list[Topic] = []
    block = -1
    count = 0
    for link in stream.links(phrases):
        # TOPICOFFSET: the block of the link and a character count that
        # restarts in every block.
        link_block = link.pos // stream.span
        if link_block != block:
            block = link_block
            count = 0
        link.offset = (block << 15) | count
        kind = link.record_type
        if kind == RECORD_TOPIC_HEADER:
            topics.append(parse_topic_header(link, len(topics), system, log))
            continue
        if kind not in (RECORD_DISPLAY, RECORD_TABLE, RECORD_DISPLAY30):
            log.warn(f"|TOPIC: record type 0x{kind:02x} skipped",
                     f"record-{kind:02x}")
            continue
        if not topics:
            log.warn("|TOPIC: text before the first topic header")
            topics.append(Topic(number=0, offset=link.offset, pos=link.pos))
        parser = RecordParser(link, system.windows, log)
        try:
            blocks = parser.parse()
        except HelpFormatError as error:
            log.warn(f"record 0x{link.pos:x}: {error}; kept as plain text",
                     "record-error")
            blocks = [fallback_paragraph(link)]
        count += parser.characters()
        topic = topics[-1]
        if topic.has_nonscroll and link.pos < topic.scroll_pos:
            topic.nonscroll.extend(blocks)
        else:
            topic.scroll.extend(blocks)
    return topics


def parse_topic_header(link: TopicLink, number: int, system: SystemInfo,
                       log: Log) -> Topic:
    topic = Topic(number=number, offset=link.offset, pos=link.pos)
    strings = link.data2.split(b"\0")
    topic.title = strings[0].decode("cp1252", "replace")
    topic.macros = [item.decode("cp1252", "replace")
                    for item in strings[1:] if item]
    reader = Reader(link.data1)
    try:
        if system.minor <= 16:
            log.warn("Windows 3.0 browse sequences are not converted",
                     "hc30-browse")
            return topic
        reader.i32()  # block size
        topic.browse_back = reader.i32()
        topic.browse_forward = reader.i32()
        reader.i32()  # topic number
        nonscroll = reader.i32()
        scroll = reader.i32()
        topic.has_nonscroll = nonscroll != -1
        topic.scroll_pos = scroll
    except HelpFormatError as error:
        log.warn(f"topic header 0x{link.pos:x}: {error}")
    return topic


# ---------------------------------------------------------------------------
# Pictures: SHG / MRB containers and BMP files, written as PNG

SHG_MAGIC = 0x506C   # "lP"
MRB_MAGIC = 0x706C   # "lp"
PICTURE_DDB = 5
PICTURE_DIB = 6
PICTURE_METAFILE = 8


@dataclass
class Hotspot:
    kind: int
    x: int
    y: int
    width: int
    height: int
    hash: int
    name: str = ""
    target: str = ""


@dataclass
class Image:
    width: int
    height: int
    rows: list[bytes]                 # palette indices or RGB triples
    palette: list[tuple[int, int, int]] | None
    hotspots: list[Hotspot] = field(default_factory=list)


def png_bytes(image: Image) -> bytes:
    """A minimal PNG: 8-bit palette (colour type 3) or RGB (type 2)."""
    def chunk(tag: bytes, body: bytes) -> bytes:
        crc = zlib.crc32(tag + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + tag + body + struct.pack(
            ">I", crc)

    colour_type = 3 if image.palette is not None else 2
    header = struct.pack(">IIBBBBB", image.width, image.height, 8,
                         colour_type, 0, 0, 0)
    raw = b"".join(b"\0" + row for row in image.rows)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    if image.palette is not None:
        palette = list(image.palette[:256]) or [(0, 0, 0)]
        used = max((max(row) for row in image.rows if row), default=0)
        palette += [(0, 0, 0)] * (used + 1 - len(palette))
        out += chunk(b"PLTE", bytes(v for rgb in palette for v in rgb))
    out += chunk(b"IDAT", zlib.compress(raw, 9))
    return out + chunk(b"IEND", b"")


MAX_PICTURE_SIDE = 8192


def unpack_rows(pixels: bytes, width: int, height: int, bits: int,
                stride: int, bottom_up: bool) -> list[bytes]:
    """Scan lines as one byte per palette index, or RGB for 16/24/32."""
    if not (0 < width <= MAX_PICTURE_SIDE and 0 < height <= MAX_PICTURE_SIDE):
        raise HelpFormatError(f"implausible picture size {width}x{height}")
    if bits not in (1, 2, 4, 8, 16, 24, 32):
        raise HelpFormatError(f"unsupported bit count {bits}")
    if len(pixels) < stride * height:
        pixels = pixels + bytes(stride * height - len(pixels))
    rows: list[bytes] = []
    for y in range(height):
        line = height - 1 - y if bottom_up else y
        row = pixels[line * stride:(line + 1) * stride]
        if bits == 8:
            rows.append(bytes(row[:width]))
        elif bits in (1, 2, 4):
            per_byte = 8 // bits
            mask = (1 << bits) - 1
            out = bytearray(width)
            for x in range(width):
                byte = row[x // per_byte]
                shift = 8 - bits * (x % per_byte + 1)
                out[x] = (byte >> shift) & mask
            rows.append(bytes(out))
        elif bits == 24:
            out = bytearray(width * 3)
            out[0::3] = row[2:width * 3:3]
            out[1::3] = row[1:width * 3:3]
            out[2::3] = row[0:width * 3:3]
            rows.append(bytes(out))
        elif bits == 32:
            out = bytearray(width * 3)
            out[0::3] = row[2:width * 4:4]
            out[1::3] = row[1:width * 4:4]
            out[2::3] = row[0:width * 4:4]
            rows.append(bytes(out))
        elif bits == 16:
            out = bytearray()
            for x in range(width):
                value = row[2 * x] | (row[2 * x + 1] << 8)
                out += bytes((((value >> 10) & 31) * 255 // 31,
                              ((value >> 5) & 31) * 255 // 31,
                              (value & 31) * 255 // 31))
            rows.append(bytes(out))
        else:
            raise HelpFormatError(f"unsupported bit count {bits}")
    return rows


def read_palette(data: bytes, count: int) -> list[tuple[int, int, int]]:
    return [(data[4 * i + 2], data[4 * i + 1], data[4 * i])
            for i in range(count)]


# Win16 default colours for device-dependent bitmaps without a palette.
VGA_PALETTE = [
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128),
    (128, 0, 128), (0, 128, 128), (192, 192, 192), (128, 128, 128),
    (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255),
    (0, 255, 255), (255, 255, 255),
]


def parse_shg_hotspots(data: bytes, offset: int) -> list[Hotspot]:
    reader = Reader(data, offset)
    if reader.u8() != 1:
        raise HelpFormatError("bad hotspot marker")
    count = reader.u16()
    macro_size = reader.u32()
    spots = []
    for _ in range(count):
        kind, _flags, _reserved = reader.u8(), reader.u8(), reader.u8()
        x, y, width, height = (reader.u16(), reader.u16(), reader.u16(),
                               reader.u16())
        spots.append(Hotspot(kind, x, y, width, height, reader.u32()))
    reader.take(macro_size)
    for spot in spots:
        spot.name = reader.cstring()
        spot.target = reader.cstring()
    return spots


def decode_shg_picture(data: bytes, start: int, log: Log,
                       where: str) -> Image | None:
    reader = Reader(data, start)
    picture_type = reader.u8()
    packing = reader.u8()
    if picture_type == PICTURE_METAFILE:
        log.warn(f"{where}: metafile picture not converted", "metafile")
        return None
    if picture_type not in (PICTURE_DDB, PICTURE_DIB):
        log.warn(f"{where}: picture type {picture_type} not converted",
                 f"shg-type-{picture_type}")
        return None
    reader.cdword()  # x pixels per metre
    reader.cdword()  # y pixels per metre
    planes = reader.cword()
    bits = reader.cword()
    width = reader.cdword()
    height = reader.cdword()
    colours = reader.cdword()
    reader.cdword()  # colours important
    compressed_size = reader.cdword()
    hotspot_size = reader.cdword()
    data_offset = reader.u32()
    hotspot_offset = reader.u32()
    if planes != 1:
        log.warn(f"{where}: {planes}-plane bitmap not converted", "planes")
        return None
    palette: list[tuple[int, int, int]] | None = None
    if picture_type == PICTURE_DIB:
        count = colours or (1 << bits if bits <= 8 else 0)
        palette = read_palette(reader.take(count * 4), count) or None
    elif bits == 1:
        palette = [(0, 0, 0), (255, 255, 255)]
    elif bits <= 4:
        palette = VGA_PALETTE
    elif bits == 8:
        palette = [(v, v, v) for v in range(256)]
    packed = data[start + data_offset:start + data_offset + compressed_size]
    pixels = decompress_picture(packing, packed)
    if picture_type == PICTURE_DIB:
        stride = ((width * bits + 31) // 32) * 4
    else:
        stride = ((width * bits + 15) // 16) * 2
    rows = unpack_rows(pixels, width, height, bits, stride,
                       bottom_up=picture_type == PICTURE_DIB)
    if bits > 8:
        palette = None
    image = Image(width, height, rows, palette)
    if hotspot_size and hotspot_offset:
        try:
            image.hotspots = parse_shg_hotspots(data, start + hotspot_offset)
        except HelpFormatError as error:
            log.warn(f"{where}: hotspots: {error}")
    return image


def decode_bmp(data: bytes) -> Image:
    if data[:2] != b"BM":
        raise HelpFormatError("not a BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    (size, width, height, planes, bits, compression,
     _image_size, _x, _y, colours, _important) = struct.unpack_from(
        "<IiiHHIIiiII", data, 14)
    if compression != 0 or planes != 1:
        raise HelpFormatError("compressed BMP not supported")
    count = colours or (1 << bits if bits <= 8 else 0)
    palette = read_palette(data[14 + size:14 + size + 4 * count], count)
    stride = ((width * bits + 31) // 32) * 4
    rows = unpack_rows(data[pixel_offset:], width, abs(height), bits,
                       stride, bottom_up=height > 0)
    return Image(width, abs(height), rows, palette if bits <= 8 else None)


def decode_picture_file(data: bytes, log: Log, where: str) -> Image | None:
    """A |bm file or an embedded picture: SHG, MRB (best resolution) or
    BMP."""
    try:
        if data[:2] == b"BM":
            return decode_bmp(data)
        reader = Reader(data)
        magic = reader.u16()
        if magic not in (SHG_MAGIC, MRB_MAGIC):
            log.warn(f"{where}: unknown picture format 0x{magic:04x}",
                     "picture-format")
            return None
        count = reader.u16()
        offsets = [reader.u32() for _ in range(count)]
        best: Image | None = None
        for index, offset in enumerate(offsets):
            image = decode_shg_picture(data, offset, log,
                                       f"{where} picture {index}")
            if image and (best is None
                          or image.width * image.height
                          > best.width * best.height):
                best = image
        return best
    except (HelpFormatError, struct.error, IndexError) as error:
        log.warn(f"{where}: {error}")
        return None


# ---------------------------------------------------------------------------
# Context hashes and help macros


def context_hash(name: str) -> int:
    """The WinHelp hash of a context string (case-insensitive)."""
    value = 0
    for code in name.encode("cp1252", "replace"):
        if 0x61 <= code <= 0x7A:
            weight = code - 0x50
        else:
            weight = {0x30: 0x0A, 0x21: 0x0B, 0x2E: 0x0C,
                      0x5F: 0x0D}.get(code, code - 0x30)
        value = (value * 43 + weight) & 0xFFFFFFFF
    return value


def split_macros(text: str) -> list[tuple[str, list[str]]]:
    """'A(`x', "y"):B()' -> [("A", ["x", "y"]), ("B", [])]. Quotes are
    `...' (nesting) or "..."; macros are separated by ':' or ';'."""
    calls: list[tuple[str, list[str]]] = []
    name, args, current = "", [], ""
    depth = quote = 0
    double = False
    for char in text:
        if double:
            if char == '"':
                double = False
            else:
                current += char
            continue
        if quote:
            if char == "`":
                quote += 1
            elif char == "'":
                quote -= 1
                if quote == 0:
                    continue
            current += char
            continue
        if char == "`":
            quote = 1
        elif char == '"':
            double = True
        elif char == "(":
            depth += 1
            if depth == 1:
                name, current = current.strip(), ""
            else:
                current += char
        elif char == ")":
            depth -= 1
            if depth == 0:
                if current.strip() or args:
                    args.append(current.strip())
                calls.append((name, args))
                name, args, current = "", [], ""
            else:
                current += char
        elif char == "," and depth == 1:
            args.append(current.strip())
            current = ""
        elif char in ":;" and depth == 0:
            current = ""
        else:
            current += char
    return calls


MACRO_JUMPS = {"ji": "jump", "jumpid": "jump", "pi": "popup",
               "popupid": "popup"}


def macro_target(text: str, help_name: str) -> Target | None:
    """The jump or popup a macro string makes (JumpId, PopupId, JumpContents
    and their short forms), or None."""
    for name, args in split_macros(text):
        lowered = name.lower()
        if lowered in MACRO_JUMPS and len(args) >= 2:
            file, _, window = args[0].partition(">")
            same = not file or Path(file).name.lower() == help_name.lower()
            return Target(MACRO_JUMPS[lowered], hash=context_hash(args[1]),
                          file="" if same else file, window=window,
                          macro=text)
        if lowered in ("jc", "jumpcontents"):
            return Target("jump", offset=-2, macro=text)
    return None


# ---------------------------------------------------------------------------
# HTML output

FAMILY_FALLBACK = {1: "monospace", 2: "serif", 3: "sans-serif",
                   4: "cursive", 5: "fantasy"}
FACE_ALIASES = {"helv": '"MS Sans Serif", Arial',
                "tms rmn": '"Times New Roman"',
                "courier": '"Courier New"'}


def page_name(topic: Topic) -> str:
    return f"topic-{topic.number:03d}.html"


def css_rgb(rgb: tuple[int, int, int]) -> str:
    return "#{:02x}{:02x}{:02x}".format(*rgb)


def font_css(index: int, font: FontDescriptor) -> str:
    face = font.face.strip()
    families = []
    if face:
        families.append(FACE_ALIASES.get(face.lower(), f'"{face}"'))
    families.append(FAMILY_FALLBACK.get(font.family, "sans-serif"))
    rules = [f"font-family: {', '.join(families)}"]
    if font.half_points:  # 0: the default size
        rules.append(f"font-size: {font.half_points / 2:g}pt")
    if font.bold:
        rules.append("font-weight: bold")
    if font.italic:
        rules.append("font-style: italic")
    decorations = []
    if font.underline or font.double_underline:
        decorations.append("underline")
    if font.strikeout:
        decorations.append("line-through")
    if decorations:
        rules.append(f"text-decoration: {' '.join(decorations)}")
    if font.small_caps:
        rules.append("font-variant: small-caps")
    if font.color != (0, 0, 0):
        rules.append(f"color: {css_rgb(font.color)}")
    return f".f{index} {{ {'; '.join(rules)} }}"


BASE_CSS = """\
body { margin: 0; background: var(--scroll, #ffffff); color: #000000;
       font-family: Arial, sans-serif; font-size: 10pt; }
nav.buttons { position: sticky; top: 0; z-index: 2; display: flex;
              gap: 4px; padding: 4px 8px; background: #c0c0c0;
              border-bottom: 1px solid #808080; }
nav.buttons a, nav.buttons span { padding: 2px 10px; border: 1px solid;
              border-color: #ffffff #808080 #808080 #ffffff;
              color: #000000; text-decoration: none;
              background: #c0c0c0; }
nav.buttons span { color: #808080; }
header.nonscroll { padding: 4px 12px; background: var(--nonscroll, #ffffff);
                   border-bottom: 1px solid #000000; }
main { padding: 4px 12px 24px; }
p { margin: 0; white-space: pre-wrap; min-height: 1em; }
.tab { display: inline-block; min-width: 2em; }
a.jump { color: #008000; text-decoration: underline; }
a.popup-link { color: #008000; text-decoration: underline dotted; }
a.jump *, a.popup-link * { color: inherit; }
.unresolved { color: #800000; }
img.left { float: left; margin: 0 8px 4px 0; }
img.right { float: right; margin: 0 0 4px 8px; }
table.ht { border-collapse: collapse; }
table.ht td { vertical-align: top; padding: 0 6px 0 0; }
.popup { display: none; }
.popup:target { display: block; position: fixed; inset: 0; z-index: 3;
                background: rgba(0, 0, 0, 0.25); }
.popup-body { position: relative; max-width: 32em; margin: 12vh auto 0;
              padding: 8px 12px; background: #ffffe0;
              border: 1px solid #000000;
              box-shadow: 4px 4px 0 rgba(0, 0, 0, 0.4); }
.popup-close { display: block; text-align: right; margin-top: 6px; }
ul.index { columns: 2; }
"""


class HtmlWriter:
    def __init__(self, help_title: str, topics: list[Topic],
                 contexts: dict[int, int], fonts: list[FontDescriptor],
                 system: SystemInfo, log: Log, help_name: str = ""):
        self.help_title = help_title
        self.help_name = help_name
        self.topics = topics
        self.contexts = contexts  # unsigned hash -> TOPICOFFSET
        self.fonts = fonts
        self.system = system
        self.log = log
        self.starts = [topic.offset for topic in topics]
        self.contents = self.topic_at(system.contents or 0) or (
            topics[0] if topics else None)
        # Anchors for offsets inside a topic: TOPICOFFSET -> anchor id.
        self.anchors: dict[int, str] = {}
        self.pictures: dict[int, str] = {}       # bm number -> image path
        self.picture_maps: dict[int, list[Hotspot]] = {}
        self.embedded: dict[int, str] = {}       # id(Picture) -> path
        self.popups: set[int] = set()            # topic numbers
        self.browse = any(t.browse_back != -1 or t.browse_forward != -1
                          for t in topics) or any(
            name.lower() == "browsebuttons"
            for macro in system.macros for name, _ in split_macros(macro))

    # -- resolution --------------------------------------------------------

    def topic_at(self, offset: int) -> Topic | None:
        best = None
        for topic in self.topics:
            if topic.offset <= offset:
                best = topic
            else:
                break
        return best

    def locate(self, offset: int) -> tuple[Topic, str] | None:
        """The topic holding a TOPICOFFSET and an anchor when it is not
        the topic's start."""
        topic = self.topic_at(offset)
        if topic is None:
            return None
        if offset == topic.offset:
            return topic, ""
        paragraph_offset = topic.offset
        for block in topic.blocks():
            if block.offset <= offset:
                paragraph_offset = block.offset
        if paragraph_offset == topic.offset:
            return topic, ""
        anchor = self.anchors.setdefault(paragraph_offset,
                                         f"p{paragraph_offset:x}")
        return topic, anchor

    def href(self, offset: int) -> str | None:
        located = self.locate(offset)
        if located is None:
            return None
        topic, anchor = located
        return page_name(topic) + (f"#{anchor}" if anchor else "")

    def resolve(self, target: Target) -> tuple[Topic, str] | None:
        if target.kind == "macro":
            target = macro_target(target.macro, self.help_name) or target
            if target.kind == "macro":
                return None
        if target.file:
            return None
        if target.offset == -2 and self.contents:  # JumpContents
            return self.contents, ""
        offset = target.offset
        if target.hash is not None:
            offset = self.contexts.get(target.hash & 0xFFFFFFFF)
        if offset is None:
            return None
        return self.locate(offset)

    def prepare(self) -> None:
        """Resolve every target once so anchors exist before writing."""
        for hash_value, offset in sorted(self.contexts.items()):
            if self.locate(offset) is None:
                self.log.warn(f"context 0x{hash_value:08x} points outside"
                              " every topic", "context-outside")
        for topic in self.topics:
            for item in self.items(topic.blocks()):
                target = getattr(item, "target", None)
                if target is not None:
                    self.resolve(target)

    def items(self, blocks: list) -> Iterator:
        for block in blocks:
            for paragraph in self.paragraphs_of(block):
                yield from paragraph.items

    @staticmethod
    def paragraphs_of(block) -> list[Paragraph]:
        if isinstance(block, Table):
            return [p for cell in block.cells.values() for p in cell]
        return [block]

    # -- inline rendering --------------------------------------------------

    def link_open(self, target: Target, popups: set[int]) -> tuple[str, str]:
        if target.kind == "macro":
            target = macro_target(target.macro, self.help_name) or target
        if target.kind == "macro":
            self.log.warn(f"macro hotspot not converted: {target.macro}",
                          "macro-hotspot")
            title = html.escape(target.macro, quote=True)
            return f'<span class="macro" title="{title}">', "</span>"
        located = self.resolve(target)
        if located is None:
            where = target.file or (f"0x{target.hash & 0xFFFFFFFF:08x}"
                                    if target.hash is not None
                                    else f"offset 0x{target.offset:x}")
            self.log.warn(f"unresolved {target.kind} to {where}",
                          f"unresolved-{target.kind}")
            return '<span class="unresolved">', "</span>"
        topic, anchor = located
        if target.kind == "popup" and not anchor:
            popups.add(topic.number)
            return (f'<a class="popup-link" href="#popup-{topic.number:03d}"'
                    '>', "</a>")
        href = page_name(topic) + (f"#{anchor}" if anchor else "")
        return f'<a class="jump" href="{href}">', "</a>"

    def picture_html(self, picture: Picture, popups: set[int]) -> str:
        if picture.number is not None:
            path = self.pictures.get(picture.number)
            spots = self.picture_maps.get(picture.number, [])
            name = f"bm{picture.number}"
        else:
            path = self.embedded.get(id(picture))
            spots = []
            name = f"e{id(picture):x}"
        if path is None:
            return '<span class="unresolved">[picture]</span>'
        css = {"inline": "inline", "left": "left", "right": "right"}
        tag = f'<img class="{css[picture.align]}" src="{path}" alt=""'
        if not spots:
            return tag + ">"
        areas = []
        for spot in spots:
            kind = {0xE2: "popup", 0xE6: "popup", 0xE3: "jump",
                    0xE7: "jump"}.get(spot.kind)
            if kind is None:
                continue
            located = self.resolve(Target(kind, hash=spot.hash))
            if located is None:
                self.log.warn(f"{name}: unresolved hotspot",
                              "unresolved-hotspot")
                continue
            topic, anchor = located
            if kind == "popup" and not anchor:
                popups.add(topic.number)
                href = f"#popup-{topic.number:03d}"
            else:
                href = page_name(topic) + (f"#{anchor}" if anchor else "")
            coords = (f"{spot.x},{spot.y},{spot.x + spot.width},"
                      f"{spot.y + spot.height}")
            title = html.escape(topic.title, quote=True)
            areas.append(f'<area shape="rect" coords="{coords}" '
                         f'href="{href}" alt="{title}" title="{title}">')
        if not areas:
            return tag + ">"
        return (tag + f' usemap="#map-{name}">'
                f'<map name="map-{name}">{"".join(areas)}</map>')

    def item_html(self, item, popups: set[int]) -> str:
        if isinstance(item, Text):
            return (f'<span class="f{item.font}">'
                    f"{html.escape(item.text, quote=False)}</span>")
        if isinstance(item, LineBreak):
            return "<br>"
        if isinstance(item, Tab):
            return '<span class="tab">\t</span>'
        if isinstance(item, Picture):
            return self.picture_html(item, popups)
        return ""

    def inline_html(self, items: list, popups: set[int]) -> str:
        out = []
        index = 0
        while index < len(items):
            target = getattr(items[index], "target", None)
            end = index
            while (end < len(items)
                   and getattr(items[end], "target", None) is target):
                end += 1
            inner = "".join(self.item_html(item, popups)
                            for item in items[index:end])
            if target is not None:
                start, stop = self.link_open(target, popups)
                inner = start + inner + stop
            out.append(inner)
            index = end
        return "".join(out)

    def paragraph_html(self, paragraph: Paragraph, popups: set[int]) -> str:
        styles = []
        if paragraph.align != "left":
            styles.append(f"text-align: {paragraph.align}")
        # twips / 20 = points
        if paragraph.left:
            styles.append(f"margin-left: {paragraph.left / 20:g}pt")
        if paragraph.right:
            styles.append(f"margin-right: {paragraph.right / 20:g}pt")
        if paragraph.first:
            styles.append(f"text-indent: {paragraph.first / 20:g}pt")
        if paragraph.above:
            styles.append(f"margin-top: {paragraph.above / 20:g}pt")
        if paragraph.below:
            styles.append(f"margin-bottom: {paragraph.below / 20:g}pt")
        if paragraph.border:
            styles.append("border: 1px solid #000000; padding: 2px")
        attributes = ""
        anchor = self.anchors.get(paragraph.offset)
        if anchor:
            attributes += f' id="{anchor}"'
        if styles:
            attributes += f' style="{"; ".join(styles)}"'
        return (f"<p{attributes}>{self.inline_html(paragraph.items, popups)}"
                "</p>")

    def blocks_html(self, blocks: list, popups: set[int]) -> str:
        out = []
        for block in blocks:
            if isinstance(block, Table):
                cells = []
                for column in sorted(block.cells):
                    width = (block.widths[column]
                             if 0 <= column < len(block.widths) else 0)
                    style = (f' style="width: {width / 20:g}pt"' if width
                             else "")
                    body = "".join(self.paragraph_html(p, popups)
                                   for p in block.cells[column])
                    cells.append(f"<td{style}>{body}</td>")
                out.append(f'<table class="ht"><tr>{"".join(cells)}</tr>'
                           "</table>")
            else:
                out.append(self.paragraph_html(block, popups))
        return "\n".join(out)

    # -- pages -------------------------------------------------------------

    def window_colours(self) -> str:
        main = next((w for w in self.system.windows
                     if "main" in (w.name.lower(), w.kind.lower())), None)
        rules = []
        if main and main.scroll_rgb:
            rules.append(f"--scroll: {css_rgb(main.scroll_rgb)}")
        if main and main.nonscroll_rgb:
            rules.append(f"--nonscroll: {css_rgb(main.nonscroll_rgb)}")
        return f":root {{ {'; '.join(rules)} }}\n" if rules else ""

    def stylesheet(self) -> str:
        fonts = "\n".join(font_css(index, font)
                          for index, font in enumerate(self.fonts))
        return BASE_CSS + self.window_colours() + fonts + "\n"

    def nav(self, topic: Topic | None) -> str:
        links = ['<a href="contents.html">Contents</a>',
                 '<a href="index.html">Index</a>']
        # CreateButton (CB) config macros with a jump or popup.
        for macro in self.system.macros:
            for name, args in split_macros(macro):
                if name.lower() not in ("cb", "createbutton") or len(args) < 3:
                    continue
                label = html.escape(args[1].replace("&", ""))
                target = macro_target(args[2], self.help_name)
                located = self.resolve(target) if target else None
                if located is None:
                    links.append(f"<span>{label}</span>")
                    continue
                found, anchor = located
                links.append(f'<a href="{page_name(found)}'
                             f'{"#" + anchor if anchor else ""}">{label}</a>')
        if self.browse:
            for label, offset in (("&lt;&lt;", topic.browse_back if topic
                                   else -1),
                                  ("&gt;&gt;", topic.browse_forward if topic
                                   else -1)):
                href = self.href(offset) if offset != -1 else None
                links.append(f'<a href="{href}">{label}</a>' if href
                             else f"<span>{label}</span>")
        return f'<nav class="buttons">{"".join(links)}</nav>'

    def document(self, title: str, body: str) -> str:
        heading = html.escape(title, quote=False)
        return ("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
                f"<title>{heading}</title>\n"
                '<link rel="stylesheet" href="style.css">\n'
                f"</head>\n<body>\n{body}\n</body>\n</html>\n")

    def popup_boxes(self, popups: set[int]) -> str:
        # Popups may open further popups: render the closure, once each.
        done: set[int] = set()
        boxes = []
        pending = sorted(popups)
        while pending:
            number = pending.pop(0)
            if number in done:
                continue
            done.add(number)
            nested: set[int] = set()
            body = self.blocks_html(self.topics[number].blocks(), nested)
            boxes.append(f'<section class="popup" id="popup-{number:03d}">'
                         f'<div class="popup-body">{body}'
                         '<a class="popup-close" href="#_">Close</a>'
                         "</div></section>")
            pending.extend(sorted(nested - done))
        return "\n".join(boxes)

    def topic_page(self, topic: Topic) -> str:
        popups: set[int] = set()
        parts = [self.nav(topic)]
        if topic.nonscroll:
            parts.append('<header class="nonscroll">'
                         f"{self.blocks_html(topic.nonscroll, popups)}"
                         "</header>")
        parts.append(f"<main>{self.blocks_html(topic.scroll, popups)}</main>")
        parts.append(self.popup_boxes(popups))
        title = topic.title or self.help_title
        return self.document(title, "\n".join(parts))

    def index_page(self, keywords: list[tuple[str, list[int]]]) -> str:
        parts = [self.nav(None),
                 f"<main><h1>{html.escape(self.help_title)}</h1>"]
        parts.append("<h2>Topics</h2>\n<ul class=\"index\">")
        for topic in self.topics:
            if topic.title:
                parts.append(f'<li><a href="{page_name(topic)}">'
                             f"{html.escape(topic.title)}</a></li>")
        parts.append("</ul>")
        if keywords:
            parts.append("<h2>Keywords</h2>\n<ul class=\"index\">")
            for keyword, offsets in keywords:
                hrefs = [(self.href(offset), self.topic_at(offset))
                         for offset in offsets]
                hrefs = [(h, t) for h, t in hrefs if h and t]
                if not hrefs:
                    continue
                label = html.escape(keyword)
                if len(hrefs) == 1:
                    parts.append(f'<li><a href="{hrefs[0][0]}">{label}'
                                 "</a></li>")
                    continue
                subs = "".join(
                    f'<li><a href="{h}">'
                    f"{html.escape(t.title or page_name(t))}</a></li>"
                    for h, t in hrefs)
                parts.append(f"<li>{label}<ul>{subs}</ul></li>")
            parts.append("</ul>")
        parts.append("</main>")
        return self.document(self.help_title or "Help", "\n".join(parts))

    def contents_redirect(self) -> str:
        target = page_name(self.contents) if self.contents else "index.html"
        return ("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
                f'<meta http-equiv="refresh" content="0; url={target}">\n'
                f"<title>{html.escape(self.help_title or 'Help')}</title>\n"
                f'</head>\n<body><a href="{target}">Contents</a></body>\n'
                "</html>\n")


# ---------------------------------------------------------------------------
# Conversion


@dataclass
class Result:
    topics: int = 0
    bitmaps: int = 0
    warnings: int = 0
    pages: list[str] = field(default_factory=list)


def convert(data: bytes, output: Path, log: Log,
            help_name: str = "") -> Result:
    """help_name: the file name macros use for this file (JumpId)."""
    container = Container(data, log)
    known = {"|SYSTEM", "|TOPIC", "|FONT", "|Phrases", "|CONTEXT",
             "|CTXOMAP", "|TTLBTREE", "|KWBTREE", "|KWDATA", "|KWMAP"}
    for name in container.names():
        if name not in known and not re.fullmatch(r"\|bm\d+", name):
            log.warn(f"internal file {name} skipped", f"file-{name}")
    system_data = container.get("|SYSTEM")
    if system_data is None:
        raise HelpFormatError("no |SYSTEM file")
    system = parse_system(system_data, log)
    if system.minor > 21:
        log.warn(f"|SYSTEM minor version {system.minor} (WinHelp 4 or"
                 " later): only the 3.x structures are converted")
    def optional(name: str, parse: Callable[[bytes], object], default):
        """An optional internal file: a damaged one is logged, skipped."""
        try:
            data = container.get(name)
            return default if data is None else parse(data)
        except (HelpFormatError, struct.error) as error:
            log.warn(f"{name}: {error}; skipped")
            return default

    phrases = optional("|Phrases", lambda d: parse_phrases(d, system), None)
    if "|PhrIndex" in container.offsets:
        log.warn("Hall phrase compression (|PhrIndex) is not supported")
    topic_data = container.get("|TOPIC")
    if topic_data is None:
        raise HelpFormatError("no |TOPIC file")
    stream = TopicStream(topic_data, system, log)
    topics = read_topics(stream, phrases, system, log)
    if any(topic.macros for topic in topics):
        log.warn("topic entry macros are not run", "entry-macros")

    fonts = optional("|FONT", lambda d: parse_fonts(d, log), [])
    contexts: dict[int, int] = {
        hash_value & 0xFFFFFFFF: offset
        for hash_value, offset in optional(
            "|CONTEXT", lambda d: btree_entries(d, log, "|CONTEXT"), [])}
    map_ids = optional("|CTXOMAP", parse_ctxomap, [])
    kwdata = optional("|KWDATA", bytes, None)
    keywords = []
    if kwdata is not None:
        keywords = optional("|KWBTREE",
                            lambda d: parse_keywords(d, kwdata, log), [])
    starts = {topic.offset for topic in topics}
    for offset, _title in optional(
            "|TTLBTREE", lambda d: btree_entries(d, log, "|TTLBTREE"), []):
        if offset not in starts:
            log.warn(f"|TTLBTREE offset 0x{offset:x} is not a topic start",
                     "ttl-mismatch")

    writer = HtmlWriter(system.title, topics, contexts, fonts, system, log,
                        help_name)
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir(exist_ok=True)
    for stale in list(output.glob("topic-*.html")) + list(
            images.glob("*.png")):
        stale.unlink()
    result = Result(topics=len(topics))

    for name in container.names():
        match = re.fullmatch(r"\|bm(\d+)", name)
        if not match:
            continue
        number = int(match.group(1))
        image = optional(name, lambda d, n=name: decode_picture_file(
            d, log, n), None)
        if image is None:
            continue
        path = f"images/bm{number}.png"
        (output / path).write_bytes(png_bytes(image))
        writer.pictures[number] = path
        writer.picture_maps[number] = image.hotspots
        result.bitmaps += 1
    embedded = 0
    for topic in topics:
        for item in writer.items(topic.blocks()):
            if isinstance(item, Picture) and item.data is not None:
                image = decode_picture_file(item.data, log,
                                            f"topic {topic.number} picture")
                if image is None:
                    continue
                path = f"images/embedded-{embedded}.png"
                (output / path).write_bytes(png_bytes(image))
                writer.embedded[id(item)] = path
                embedded += 1
                result.bitmaps += 1

    writer.prepare()
    for topic in topics:
        (output / page_name(topic)).write_text(writer.topic_page(topic),
                                               encoding="utf-8")
        result.pages.append(page_name(topic))
    (output / "index.html").write_text(writer.index_page(keywords),
                                       encoding="utf-8")
    (output / "contents.html").write_text(writer.contents_redirect(),
                                          encoding="utf-8")
    (output / "style.css").write_text(writer.stylesheet(), encoding="utf-8")

    contents = page_name(writer.contents) if writer.contents else None
    help_map = {
        "format": 1,
        "title": system.title,
        "contents": contents,
        "index": "index.html",
        "topics": [{"number": topic.number,
                    "offset": f"0x{topic.offset:08x}",
                    "page": page_name(topic),
                    "title": topic.title} for topic in topics],
        "contexts": {f"0x{hash_value:08x}": writer.href(offset)
                     for hash_value, offset in sorted(contexts.items())},
        "map": {str(map_id): writer.href(offset)
                for map_id, offset in map_ids},
        "bitmaps": {f"bm{number}": path
                    for number, path in sorted(writer.pictures.items())},
        "warnings": len(log.warnings),
    }
    result.warnings = len(log.warnings)
    # Written last: the build uses it as the conversion's output stamp.
    (output / "help-map.json").write_text(
        json.dumps(help_map, indent=1, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Convert a WinHelp 3.x .HLP file to HTML pages.")
    parser.add_argument("help_file", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--quiet", action="store_true",
                        help="print only the summary line")
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 when anything was skipped")
    args = parser.parse_args(argv)
    log = Log(quiet=args.quiet)
    try:
        data = args.help_file.read_bytes()
        result = convert(data, args.output, log, args.help_file.name)
    except (OSError, HelpFormatError, struct.error) as error:
        print(f"convert_winhelp: error: {error}", file=sys.stderr)
        return 1
    print(f"convert_winhelp: {result.topics} topics, {result.bitmaps} "
          f"bitmaps, {result.warnings} warnings -> {args.output}")
    if args.strict and result.warnings:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
