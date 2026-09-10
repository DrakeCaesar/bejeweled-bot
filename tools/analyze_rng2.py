#!/usr/bin/env python3
"""Correlate MT19937 outputs (consumed between frames) with gem board changes."""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "rng_state.bin"
data = open(path, "rb").read()
REC = 8 + 4 + 624 * 4 + 64
n = len(data) // REC
print(f"records={n} (rec={REC}B)")

MASK = 0xFFFFFFFF


def temper(y):
    y ^= y >> 11
    y &= MASK
    y ^= (y << 7) & 0x9D2C5680
    y &= MASK
    y ^= (y << 15) & 0xEFC60000
    y &= MASK
    y ^= y >> 18
    return y & 0x7FFFFFFF


recs = []
for k in range(n):
    o = k * REC
    ms, = struct.unpack_from("<q", data, o)
    idx, = struct.unpack_from("<i", data, o + 8)
    st = struct.unpack_from("<624I", data, o + 12)
    bd = struct.unpack_from("<64b", data, o + 12 + 2496)
    recs.append((ms, idx, st, bd))

print("first idx", recs[0][1], "last idx", recs[-1][1])

events = 0
for k in range(1, n):
    ms0, idx0, st0, bd0 = recs[k - 1]
    ms1, idx1, st1, bd1 = recs[k]
    if bd0 == bd1:
        continue
    # MT outputs consumed between the two frames
    outs = []
    if idx1 >= idx0:
        outs = [temper(st0[i]) for i in range(idx0, idx1)]
    else:  # a twist happened
        outs = [temper(st0[i]) for i in range(idx0, 624)]
        outs += [temper(st1[i]) for i in range(0, idx1)]
    changed = [(i, bd0[i], bd1[i]) for i in range(64) if bd0[i] != bd1[i]]
    events += 1
    if events > 40:
        break
    print(f"[{ms0:6d}->{ms1:6d}] idx {idx0}->{idx1} nouts={len(outs)} "
          f"changed={len(changed)}")
    print("   outs%7 :", [o % 7 for o in outs][:40])
    print("   newcol :", [c for _, _, c in changed][:40])
print("done")
