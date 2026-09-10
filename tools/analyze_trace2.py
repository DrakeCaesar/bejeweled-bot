#!/usr/bin/env python3
"""Second pass: can we tell which gems are settled (at rest)?

For each slot we look at the candidate animated float fields:
    +0x10 / +0x14   (dw[2], dw[3])
    +0x18 / +0x1C   (dw[4], dw[5])

and ask:
  * During quiet stretches (no type change anywhere for a while), are these
    constant?  -> they'd be a usable "at rest" signal.
  * What are the resting values, and do they relate to (row, col)?
  * When a slot's type changes, are the floats mid-motion?
"""

import struct
import sys
from collections import Counter, defaultdict

FRAME = 2824
DW_OFF = [0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24]


def f(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def load(path):
    data = open(path, "rb").read()
    n = len(data) // FRAME
    out = []
    for i in range(n):
        off = i * FRAME
        (ms,) = struct.unpack_from("<q", data, off)
        p = off + 8 + 64 * 4
        dw = struct.unpack_from("<512I", data, p)
        p += 512 * 4
        typ = struct.unpack_from("<64i", data, p)
        out.append((ms, dw, typ))
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "board_trace.bin"
    fr = load(path)
    print(f"frames={len(fr)}")

    # frames where ANY slot type changed
    changed = [False] * len(fr)
    for i in range(1, len(fr)):
        if fr[i][2] != fr[i - 1][2]:
            changed[i] = True

    # quiet = no type change in the previous 300 ms
    QUIET_MS = 300
    quiet = [False] * len(fr)
    lo = 0
    for i in range(len(fr)):
        while fr[i][0] - fr[lo][0] > QUIET_MS:
            lo += 1
        quiet[i] = not any(changed[lo:i + 1]) if i > 0 else (not changed[0])
    nq = sum(quiet)
    print(f"quiet frames (no type change for {QUIET_MS}ms): {nq}/{len(fr)}")

    # for each (field-pair, slot) collect values during quiet frames
    for (a, b, label) in [(2, 3, "+0x10/+0x14"), (4, 5, "+0x18/+0x1C")]:
        print(f"\n=== {label} during quiet frames ===")
        multi = 0
        examples = []
        for slot in range(64):
            vals = Counter()
            for i in range(len(fr)):
                if not quiet[i]:
                    continue
                vals[(fr[i][1][slot * 8 + a], fr[i][1][slot * 8 + b])] += 1
            if len(vals) > 1:
                multi += 1
                if len(examples) < 6:
                    examples.append((slot, len(vals), vals.most_common(3)))
        print(f"slots with >1 value while quiet: {multi}/64")
        for (slot, n, top) in examples:
            fl = [((round(f(u), 2), round(f(v), 2)), c) for ((u, v), c) in top]
            print(f"  slot {slot:2d} (r{slot//8},c{slot%8}) distinct={n} "
                  f"int={top} float={fl}")

    # resting values vs (row,col) on the very last frame
    print("\n=== last-frame rest values (int / float) ===")
    dw = fr[-1][1]
    for label, (a, b) in [("+0x10/+0x14", (2, 3)), ("+0x18/+0x1C", (4, 5))]:
        print(f"{label}:")
        for r in range(8):
            cells = []
            for c in range(8):
                s = r * 8 + c
                u, v = dw[s * 8 + a], dw[s * 8 + b]
                cells.append(f"({f(u):7.2f},{f(v):7.2f})")
            print("  " + " ".join(cells))

    # when a slot's type changes, look at the position fields just before/after
    print("\n=== type-change events: position fields around the change ===")
    shown = 0
    for i in range(1, len(fr)):
        for slot in range(64):
            if fr[i][2][slot] != fr[i - 1][2][slot]:
                if shown >= 10:
                    break
                shown += 1
                pv = fr[i - 1][1]
                nv = fr[i][1]
                print(f" t={fr[i][0]} slot {slot:2d} (r{slot//8},c{slot%8}) "
                      f"type {fr[i-1][2][slot]}->{fr[i][2][slot]}")
                print(f"   before dw2,3={f(pv[slot*8+2]):8.3f},{f(pv[slot*8+3]):8.3f} "
                      f"dw4,5={f(pv[slot*8+4]):8.3f},{f(pv[slot*8+5]):8.3f}")
                print(f"   after  dw2,3={f(nv[slot*8+2]):8.3f},{f(nv[slot*8+3]):8.3f} "
                      f"dw4,5={f(nv[slot*8+4]):8.3f},{f(nv[slot*8+5]):8.3f}")
        if shown >= 10:
            break


if __name__ == "__main__":
    main()
