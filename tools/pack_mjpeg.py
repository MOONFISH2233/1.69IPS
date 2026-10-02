# -*- coding: utf-8 -*-
"""
Pack a directory of numbered JPEG frames into one .mjpeg file.

Format:
  header: b"MJPG" + uint16 frameCount + uint16 reserved   (8 bytes)
  frame:  uint32 length + JPEG bytes                      (repeated)

Usage:
  python pack_mjpeg.py <frames_dir> <output.mjpeg>
"""

import os
import struct
import sys


def pack(frames_dir, out_path):
    exts = (".jpg", ".jpeg")
    names = sorted(
        f for f in os.listdir(frames_dir)
        if f.lower().endswith(exts)
    )
    if not names:
        raise SystemExit("no JPEG files in " + frames_dir)

    frames = []
    for n in names:
        p = os.path.join(frames_dir, n)
        with open(p, "rb") as fh:
            frames.append(fh.read())

    payload = sum(len(f) for f in frames)

    with open(out_path, "wb") as out:
        out.write(b"MJPG")
        out.write(struct.pack("<HH", len(frames), 0))
        for data in frames:
            out.write(struct.pack("<I", len(data)))
            out.write(data)

    size = os.path.getsize(out_path)
    biggest = max(len(f) for f in frames)

    print("frames   :", len(frames))
    print("payload  : %.1f KB" % (payload / 1024.0))
    print("file     : %s" % out_path)
    print("size     : %.1f KB" % (size / 1024.0))
    print("overhead : %d bytes" % (8 + len(frames) * 4))
    print("max frame: %.2f KB" % (biggest / 1024.0))
    return size


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: pack_mjpeg.py <frames_dir> <output.mjpeg>")
    pack(sys.argv[1], sys.argv[2])
