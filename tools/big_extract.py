"""List or extract files from a Homeworld Remastered .big archive (Relic "_ARCHIVE" v2). Read-only on the archive.

Usage:
  python -I tools/big_extract.py <archive.big> list [substring]
  python -I tools/big_extract.py <archive.big> extract <substring> <out_dir>
"""
import struct
import sys
import zlib
from pathlib import Path


def read_archive(path):
    data = Path(path).read_bytes()
    if data[:8] != b"_ARCHIVE":
        raise SystemExit(f"{path}: not a Relic _ARCHIVE file")
    version = struct.unpack_from("<I", data, 8)[0]
    if version != 2:
        raise SystemExit(f"{path}: archive version {version}, only 2 is supported")
    # header: magic[8] version[4] tool_md5[16] name[128 utf-16] header_md5[16] toc_size[4] data_offset[4]
    toc_size, data_offset = struct.unpack_from("<II", data, 8 + 4 + 16 + 128 + 16)
    sect = 8 + 4 + 16 + 128 + 16 + 8  # section header follows, offsets relative to it
    (toc_off, toc_n, dir_off, dir_n, file_off, file_n, str_off, str_n) = struct.unpack_from("<IHIHIHIH", data, sect)

    def cstr(off):
        start = sect + str_off + off
        return data[start:data.index(b"\0", start)].decode("latin-1")

    dirs = []
    for i in range(dir_n):
        name_off, first_sub, last_sub, first_file, last_file = struct.unpack_from("<IHHHH", data, sect + dir_off + 12 * i)
        dirs.append((cstr(name_off), first_file, last_file))
    files = []
    for i in range(file_n):
        # 17-byte entries: name offset, compression flag (0 stored, 16/32 zlib), data offset, sizes
        name_off, flags, off, csize, usize = struct.unpack_from("<IBIII", data, sect + file_off + 17 * i)
        files.append((cstr(name_off), flags, off, csize, usize))
    out = []
    for dname, first, last in dirs:
        for fi in range(first, last):
            name, flags, off, csize, usize = files[fi]
            full = (dname + "\\" + name) if dname else name
            out.append((full, flags, data_offset + off, csize, usize))
    return data, out


def payload(data, entry):
    full, flags, off, csize, usize = entry
    raw = data[off:off + csize]
    if flags == 0 or csize == usize:
        return raw
    return zlib.decompress(raw)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    data, entries = read_archive(sys.argv[1])
    cmd = sys.argv[2]
    needle = sys.argv[3].lower() if len(sys.argv) > 3 else ""
    if cmd == "list":
        for full, flags, off, csize, usize in entries:
            if needle in full.lower():
                print(f"{usize:10} {full}")
    elif cmd == "extract":
        out_dir = Path(sys.argv[4])
        n = 0
        for e in entries:
            if needle in e[0].lower():
                dest = out_dir / e[0].replace("\\", "/")
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(payload(data, e))
                n += 1
        print(f"extracted {n} file(s) to {out_dir}")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
