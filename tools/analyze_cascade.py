#!/usr/bin/env python3
"""Analyze the raw-grid cascade trace: show the per-step logical states,
empty cells (Gemwipe window) and whether matched gems linger."""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "rng_state.bin"
data = open(path, "rb").read()
REC = 8 + 4 + 624 * 4 + 64 * 4 + 64
n = len(data) // REC
print(f"records={n} REC={REC}")

recs = []
for k in range(n):
    o = k * REC
    ms, = struct.unpack_from("<q", data, o)
    idx, = struct.unpack_from("<i", data, o + 8)
    ptr = struct.unpack_from("<64I", data, o + 12 + 2496)
    typ = struct.unpack_from("<64b", data, o + 12 + 2496 + 256)
    recs.append((ms, idx, ptr, typ))

GLYPH = {0: "R", 1: "W", 2: "G", 3: "Y", 4: "P", 5: "O", 6: "B", -1: "*",
         -100: "."}


def grid_str(typ):
    out = []
    for r in range(8):
        out.append("".join(GLYPH.get(typ[r * 8 + c], "?") for c in range(8)))
    return "\n".join(out)


prev = None
events = 0
for ms, idx, ptr, typ in recs:
    empties = tuple(i for i in range(64) if typ[i] == -100)
    if prev is not None and typ == prev:
        continue
    prev = typ
    events += 1
    if events > 30:
        break
    nptrs = len(set(p for p in ptr if p != 0))
    print(f"\n--- ms={ms} empty_slots={len(empties)} {list(empties)} "
          f"distinct_gems={nptrs}")
    print(grid_str(typ))
