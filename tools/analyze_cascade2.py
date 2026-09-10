#!/usr/bin/env python3
"""Scan the raw-grid trace for: pending 3+ matches (lingering matched gems),
empty cells, and gem-pointer movement between cells."""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "rng_state.bin"
data = open(path, "rb").read()
REC = 8 + 4 + 624 * 4 + 64 * 4 + 64
n = len(data) // REC
print(f"records={n}")

recs = []
for k in range(n):
    o = k * REC
    ms, = struct.unpack_from("<q", data, o)
    idx, = struct.unpack_from("<i", data, o + 8)
    ptr = struct.unpack_from("<64I", data, o + 12 + 2496)
    typ = struct.unpack_from("<64b", data, o + 12 + 2496 + 256)
    recs.append((ms, idx, ptr, typ))

COL = set(range(7))


def find_matches(typ):
    res = []
    for r in range(8):
        for c in range(6):
            t = typ[r * 8 + c]
            if t in COL and typ[r * 8 + c + 1] == t and typ[r * 8 + c + 2] == t:
                res.append((r, c, "H"))
    for c in range(8):
        for r in range(6):
            t = typ[r * 8 + c]
            if t in COL and typ[(r + 1) * 8 + c] == t and typ[(r + 2) * 8 + c] == t:
                res.append((r, c, "V"))
    return res


n_empty_frames = 0
n_match_frames = 0
first_empty = []
first_match = []
for ms, idx, ptr, typ in recs:
    if any(t == -100 for t in typ):
        n_empty_frames += 1
        if len(first_empty) < 5:
            first_empty.append((ms, [i for i in range(64) if typ[i] == -100]))
    m = find_matches(typ)
    if m:
        n_match_frames += 1
        if len(first_match) < 8:
            first_match.append((ms, m))

print(f"frames with empty cells : {n_empty_frames}/{n}")
print(f"frames with 3+ match    : {n_match_frames}/{n}")
print("sample empty frames:", first_empty)
print("sample match frames:", first_match)

# How often do gem pointers move between cells (falling)?
moves = 0
big = 0
prev = None
for ms, idx, ptr, typ in recs:
    if prev is not None:
        ch = sum(1 for i in range(64) if ptr[i] != prev[i])
        if ch:
            moves += 1
            if ch >= 8:
                big += 1
    prev = ptr
print(f"frames with pointer change: {moves}/{n} (>=8 cells: {big})")
