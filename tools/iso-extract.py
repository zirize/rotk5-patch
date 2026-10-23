#!/usr/bin/env -S python3 -I
"""Copy the files of the ISO 9660 data track (track 1, MODE1/2352) of a BIN into a directory.

usage: iso-extract.py <Audio.bin> <out dir>   (root directory only; the game CDs have no subdirs)
"""
import os
import struct
import sys

SECTOR, DATA, OFF = 2352, 2048, 16


def read(f, lba, n):
    f.seek(lba * SECTOR)
    raw = f.read(n * SECTOR)
    return b''.join(raw[i * SECTOR + OFF:i * SECTOR + OFF + DATA] for i in range(n))


def main(bin_path, out_dir):
    with open(bin_path, 'rb') as f:
        pvd = read(f, 16, 1)
        assert pvd[1:6] == b'CD001', 'no ISO 9660 volume in track 1'
        root = pvd[156:190]
        lba, size = struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]
        d = read(f, lba, (size + DATA - 1) // DATA)
        p = 0
        while p < len(d):
            n = d[p]
            if n == 0:
                p = (p // DATA + 1) * DATA
                continue
            ext, ln, flags, nl = (struct.unpack_from('<I', d, p + 2)[0], struct.unpack_from('<I', d, p + 10)[0],
                                  d[p + 25], d[p + 32])
            name = d[p + 33:p + 33 + nl].decode('ascii', 'replace').split(';')[0]
            if not flags & 2 and name not in ('\0', '\1'):
                data = read(f, ext, (ln + DATA - 1) // DATA)[:ln]
                with open(os.path.join(out_dir, name), 'wb') as o:
                    o.write(data)
                print(name, ln)
            p += n


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
