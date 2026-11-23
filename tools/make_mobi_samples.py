from pathlib import Path
from struct import pack, pack_into
from zlib import compress, crc32


def uint(value):
    return pack(">I", value)


def varint(value):
    digits = bytearray([0x80 | (value & 0x7F)])
    while value >> 7:
        value >>= 7
        digits.append(value & 0x7F)
    return bytes(reversed(digits))


def png(width, height):
    def chunk(kind, data):
        return uint(len(data)) + kind + data + uint(crc32(kind + data))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", compress((b"\0" + b"\x28\x70\xb0" * width) * height))
            + chunk(b"IEND", b""))


def database(records):
    header = bytearray(78)
    header[:12] = b"MyReaderTest"
    header[60:68] = b"BOOKMOBI"
    pack_into(">H", header, 76, len(records))
    offset = len(header) + 8 * len(records) + 2
    directory = bytearray()
    for index, record in enumerate(records):
        directory.extend(pack(">II", offset, index))
        offset += len(record)
    return bytes(header) + directory + b"\0\0" + b"".join(records)


def header(version, markup_size, first_image, title, *, compression=1, record_count=1,
           flags=0, ncx=0xFFFFFFFF, fragments=0xFFFFFFFF, skeletons=0xFFFFFFFF,
           fdst=0xFFFFFFFF, boundary=None):
    data = bytearray(16 + 264)
    pack_into(">H2xIHHH", data, 0, compression, markup_size, record_count, 4096, 0)
    data[16:20] = b"MOBI"
    fields = {
        0x14: len(data) - 16, 0x18: 2, 0x1C: 65001, 0x24: version,
        0x50: first_image, 0x6C: first_image, 0x68: version, 0x80: 0x40,
        0xC0: fdst, 0xC4: 2 if fdst != 0xFFFFFFFF else 1, 0xF4: ncx,
        0xF8: fragments, 0xFC: skeletons,
    }
    for offset, value in fields.items():
        pack_into(">I", data, offset, value)
    pack_into(">H", data, 0xF2, flags)
    entries = [(100, "测试作者".encode()), (503, title.encode()), (201, uint(0))]
    if boundary is not None:
        entries.append((121, uint(boundary)))
    fields = b"".join(pack(">II", kind, len(value) + 8) + value for kind, value in entries)
    data.extend(b"EXTH" + pack(">II", len(fields) + 12, len(entries)) + fields)
    full_name = b"Fallback title"
    pack_into(">II", data, 0x54, len(data), len(full_name))
    return bytes(data) + full_name


def index_records(tags, rows, strings=None):
    descriptors = b"".join(pack("BBBB", tag, arity, 1 << slot, 0)
                           for slot, (tag, arity) in enumerate(tags)) + bytes((0, 0, 0, 1))
    main = bytearray(192)
    main[:4] = b"INDX"
    for offset, value in [(4, 192), (24, 1), (36, len(rows)), (52, int(strings is not None))]:
        pack_into(">I", main, offset, value)
    main.extend(b"TAGX" + pack(">II", len(descriptors) + 12, 1) + descriptors)
    data = bytearray(192)
    data[:4] = b"INDX"
    positions = []
    for key, fields in rows:
        positions.append(len(data))
        control = sum(1 << slot for slot, (tag, _) in enumerate(tags) if tag in fields)
        data.extend(bytes([len(key)]) + key + bytes([control]))
        for tag, arity in tags:
            if tag in fields:
                assert len(fields[tag]) == arity
                data.extend(b"".join(varint(value) for value in fields[tag]))
    pack_into(">I", data, 4, 192)
    pack_into(">II", data, 20, len(data), len(rows))
    data.extend(b"IDXT" + b"".join(pack(">H", pos) for pos in positions))
    return [bytes(main), bytes(data)] + ([strings] if strings is not None else [])


def labels(names):
    positions = []
    strings = bytearray()
    for name in names:
        value = name.encode()
        positions.append(len(strings))
        strings.extend(varint(len(value)) + value)
    return positions, bytes(strings)


def palm_literals(text):
    return b"".join(bytes([len(text[pos:pos + 8])]) + text[pos:pos + 8]
                    for pos in range(0, len(text), 8))


def mobi6(boundary=None):
    prefix = b"<html><body>"
    first = '<h1>第一节</h1><p>中文正文 café</p><img recindex="00002" alt="湖面"/>'.encode()
    last = "<h2>第二节</h2><p>Second chapter</p>".encode()
    repeated = b"AAAAB A"
    page = b"<mbp:pagebreak/>"
    markup = prefix + b"<p>" + repeated + b"</p>" + first + page + last + b"</body></html>"
    trailer = b"\xe4\x01\xff\x82"
    block = (palm_literals(prefix + b"<p>") + b"A\x80\x08B\xc1"
             + palm_literals(b"</p>" + first + page) + trailer)
    second = palm_literals(last + b"</body></html>") + trailer
    label_at, strings = labels(["目录第一节", "目录第二节"])
    entries = [(b"0", {1: [0], 3: [label_at[0]]}),
               (b"1", {1: [len(markup) - len(last) - len(b"</body></html>")], 3: [label_at[1]]})]
    ncx = index_records([(1, 1), (3, 1)], entries, strings)
    return [header(6, len(markup), 6, "测试 MOBI6", compression=2, record_count=2,
                   flags=3, ncx=3, boundary=boundary), block, second, *ncx,
            png(5, 7), png(11, 13), b"FLIS\0\0\0\0"]


def kf8():
    skeleton = b"<html><body></body></html>"
    insert = len(b"<html><body>")
    first = "<h1>章一</h1><p>第一片正文。</p>".encode()
    second = '<p>第二片正文。</p><img src="kindle:embed:000B?mime=image/png" alt="远山"/>'.encode()
    third = "<h1>章二</h1><p>第二章正文。</p>".encode()
    after_first = len(skeleton) + len(first) + len(second)
    main = skeleton + first + second + skeleton + third
    css = b"body{color:green}"
    skel = index_records([(1, 1), (6, 2)], [
        (b"SKEL0000000000", {1: [2], 6: [0, len(skeleton)]}),
        (b"SKEL0000000001", {1: [1], 6: [after_first, len(skeleton)]}),
    ])
    frag = index_records([(3, 1), (4, 1), (6, 2)], [
        (str(insert).encode(), {3: [0], 4: [0], 6: [0, len(first)]}),
        (str(insert + len(first)).encode(), {3: [0], 4: [1], 6: [len(first), len(second)]}),
        (str(after_first + insert).encode(), {3: [1], 4: [2], 6: [0, len(third)]}),
    ])
    label_at, strings = labels(["KF8 目录章一", "KF8 目录章二"])
    ncx = index_records([(3, 1), (6, 2)], [
        (b"0", {3: [label_at[0]], 6: [0, 0]}),
        (b"1", {3: [label_at[1]], 6: [2, 0]}),
    ], strings)
    fdst = b"FDST" + pack(">IIIIII", 12, 2, 0, len(main), len(main), len(main) + len(css))
    assert len(skel) == 2 and len(frag) == 2 and len(ncx) == 3
    return [header(8, len(main + css), 10, "测试 AZW3", skeletons=2, fragments=4, ncx=6, fdst=9),
            main + css, *skel, *frag, *ncx, fdst, png(5, 7), *([b"RESC"] * 9), png(17, 19)]


if __name__ == "__main__":
    tests = Path(__file__).resolve().parents[1] / "tests"
    (tests / "sample_mobi6.mobi").write_bytes(database(mobi6()))
    (tests / "sample_azw3.azw3").write_bytes(database(kf8()))
    shared_images = [b"RESC"] * 7 + [png(17, 19)]
    legacy = mobi6(boundary=len(mobi6()) + len(shared_images) + 1)
    (tests / "sample_hybrid.mobi").write_bytes(database(legacy + shared_images + [b"BOUNDARY"] + kf8()))
