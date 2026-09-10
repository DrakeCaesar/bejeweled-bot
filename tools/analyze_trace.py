#!/usr/bin/env python3
"""Analyze board_trace.bin produced by `bejeweled-bot.exe --trace <ms>`.

Record format (per frame, little-endian):
    int64   ms
    uint32  ptr[64]
    uint32  dw[64][8]      # gem+0x08, +0x0C, +0x10, +0x14, +0x18, +0x1C, +0x20, +0x24
    int32   type[64]       # gem+0x220
    int32   state[64]      # gem+0x228

Answers two questions:
  1. "Settled" detection: do any gem fields animate during a fall, and could we
     tell when a gem has come to rest?
  2. Special-gem creation: when does a special appear, and what was on the board
     at that moment (to learn the bomb rule)?
"""

import struct
import sys
from collections import Counter, defaultdict

FRAME = 8 + 64 * 4 + 64 * 8 * 4 + 64 * 4 + 64 * 4  # 2824 bytes
NL = 8

# dword window starts at gem+0x08
DW_OFF = [0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24]


def load(path):
    frames = []
    with open(path, "rb") as fh:
        data = fh.read()
    n = len(data) // FRAME
    print(f"file={path} bytes={len(data)} frames={n} (trailing {len(data)-n*FRAME})")
    for i in range(n):
        off = i * FRAME
        (ms,) = struct.unpack_from("<q", data, off)
        p = off + 8
        ptr = struct.unpack_from("<64I", data, p)
        p += 64 * 4
        dw = struct.unpack_from("<512I", data, p)
        p += 512 * 4
        typ = struct.unpack_from("<64i", data, p)
        p += 64 * 4
        st = struct.unpack_from("<64i", data, p)
        frames.append((ms, ptr, dw, typ, st))
    return frames


def as_float(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def report_basic(frames):
    ms0, ms1 = frames[0][0], frames[-1][0]
    dur = ms1 - ms0
    print(f"\n=== timing === duration={dur} ms, fps={len(frames)/(dur/1000.0):.1f}")


def report_field_volatility(frames):
    """For each dword index, how often does it change, and how many distinct
    values does it take? Animated position fields should be volatile."""
    print("\n=== per-offset volatility (over all 64 slots) ===")
    print(f"{'off':>6} {'changes':>10} {'distinct':>9}  sample-values")
    prev = None
    for k in range(8):
        changes = 0
        distinct = set()
        for (ms, ptr, dw, typ, st) in frames:
            row = dw[k::8]  # stride 8 => dword index k across the 64 slots
            for i in range(64):
                distinct.add(row[i])
            if prev is not None:
                for i in range(64):
                    if row[i] != prev[i]:
                        changes += 1
            prev = list(row)
        sample = sorted(distinct)[:6]
        print(f"0x{DW_OFF[k]:02X} {changes:>10} {len(distinct):>9}  {sample}")


def report_settle(frames):
    """Focus on the candidate animated fields (dw[2]=+0x10, dw[3]=+0x14) and the
    logical grid coords (dw[0]=+0x08, dw[1]=+0x0C).

    For each slot, count how many distinct (dw2,dw3) values occur and how many
    frames it deviates from its most common value."""
    print("\n=== candidate animated position (+0x10/+0x14) ===")
    per_slot_vals = defaultdict(Counter)
    per_slot_f = defaultdict(Counter)
    for (ms, ptr, dw, typ, st) in frames:
        for i in range(64):
            d2 = dw[i * 8 + 2]
            d3 = dw[i * 8 + 3]
            per_slot_vals[i][(d2, d3)] += 1
            per_slot_f[i][(round(as_float(d2), 3), round(as_float(d3), 3))] += 1

    moving = 0
    for i in range(64):
        if len(per_slot_vals[i]) > 1:
            moving += 1
    print(f"slots whose (dw2,dw3) took >1 value: {moving}/64")
    for i in range(64):
        vals = per_slot_vals[i]
        if len(vals) > 1:
            top = vals.most_common(3)
            print(f"  slot {i:2d} (r{i//8},c{i%8}) distinct={len(vals):3d} "
                  f"top(int)={top} "
                  f"top(float)={per_slot_f[i].most_common(3)}")


def report_type_changes(frames):
    """How long after a slot's type changes does it stop changing (i.e. how
    quickly does the logical board 'step')? And do position fields move then?"""
    print("\n=== type-change cadence ===")
    last_change = {}
    gaps = Counter()
    for (ms, ptr, dw, typ, st) in frames:
        for i in range(64):
            t = typ[i]
            if i in last_change and last_change[i][0] != t:
                gaps[ms - last_change[i][1]] += 1
            if i not in last_change or last_change[i][0] != t:
                last_change[i] = (t, ms)
    if gaps:
        common = sorted(gaps.items(), key=lambda kv: -kv[1])[:12]
        print(f"inter-change gaps (ms -> count), top: {common}")
    else:
        print("no type changes observed")


def report_specials(frames):
    """Detect special-gem spawns: state 0->nonzero, or type becoming -1
    (hypercube). Print the board right before, so we can infer the rule."""
    print("\n=== special-gem spawns ===")
    events = 0
    prev = None
    for (ms, ptr, dw, typ, st) in frames:
        if prev is not None:
            for i in range(64):
                pst, pt = prev[2][i], prev[3][i]
                nst, nt = st[i], typ[i]
                became_special = (pst == 0 and nst != 0)
                became_hc = (pt != -1 and nt == -1)
                if became_special or became_hc:
                    events += 1
                    kind = "HYPERCUBE" if became_hc else f"state={nst}"
                    print(f"  t={ms:6d} slot {i:2d} (r{i//8},c{i%8}) {kind} "
                          f"color {pt}->{nt}")
                    if events <= 12:
                        print("    board before:")
                        print_board(prev)
        prev = (ms, ptr, typ, st)
    print(f"total spawn events: {events}")


def print_board(frame):
    ms, ptr, typ, st = frame
    for r in range(8):
        row = ""
        for c in range(8):
            i = r * 8 + c
            t = typ[i]
            ch = "H" if t == -1 else (str(t) if t >= 0 else "?")
            s = st[i]
            if s:
                ch += "FSNS"[min(s, 3)]
            row += f"{ch:>3}"
        print("    " + row)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "board_trace.bin"
    frames = load(path)
    if not frames:
        print("no frames")
        return
    report_basic(frames)
    report_field_volatility(frames)
    report_type_changes(frames)
    report_specials(frames)
    report_settle(frames)


if __name__ == "__main__":
    main()
