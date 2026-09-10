#!/usr/bin/env python3
"""Third pass: debounced special-gem spawn detection.

The board is read at ~370 Hz, so single-frame flicker is common. Only count a
special as real if (state != 0 or type == -1) holds for >= MIN_RUN consecutive
frames. For each episode print the board ~400 ms earlier to expose the match
geometry that created it.
"""

import struct
import sys

FRAME = 2824
MIN_RUN = 6  # frames (~16 ms at 370 Hz)


def load(path):
    data = open(path, "rb").read()
    n = len(data) // FRAME
    out = []
    for i in range(n):
        off = i * FRAME
        (ms,) = struct.unpack_from("<q", data, off)
        p = off + 8 + 64 * 4 + 512 * 4
        typ = struct.unpack_from("<64i", data, p)
        st = struct.unpack_from("<64i", data, p + 256)
        out.append((ms, typ, st))
    return out


def is_special(t, s):
    return s != 0 or t == -1


def find_frame_at(fr, t):
    best = 0
    for i in range(len(fr)):
        if fr[i][0] <= t:
            best = i
        else:
            break
    return best


def print_board(typ, st):
    for r in range(8):
        row = ""
        for c in range(8):
            i = r * 8 + c
            t = typ[i]
            ch = "H" if t == -1 else (str(t) if t >= 0 else "?")
            s = st[i]
            if s == 1:
                ch += "F"
            elif s == 2:
                ch += "N"
            elif s == 4:
                ch += "S"
            row += f"{ch:>3}"
        print("    " + row)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "board_trace.bin"
    fr = load(path)
    print(f"frames={len(fr)} span={fr[0][0]}..{fr[-1][0]} ms")

    episodes = 0
    for slot in range(64):
        run = 0
        start = None
        for i in range(len(fr)):
            if is_special(fr[i][1][slot], fr[i][2][slot]):
                if run == 0:
                    start = i
                run += 1
            else:
                if run >= MIN_RUN:
                    episodes += 1
                    tt = fr[start][1][slot]
                    stt = fr[start][2][slot]
                    kind = "HYPERCUBE" if tt == -1 else f"state={stt}"
                    print(f"\n[{episodes}] slot {slot:2d} (r{slot//8},c{slot%8}) "
                          f"{kind} color={tt} t={fr[start][0]}..{fr[i-1][0]} "
                          f"({fr[i-1][0]-fr[start][0]} ms)")
                    j = find_frame_at(fr, fr[start][0] - 400)
                    print(f"  board at t={fr[j][0]} (~400ms before):")
                    print_board(fr[j][1], fr[j][2])
                run = 0
        if run >= MIN_RUN:
            episodes += 1
            print(f"\n[{episodes}] slot {slot:2d} special open at end of trace")
    print(f"\ntotal debounced special episodes: {episodes}")


if __name__ == "__main__":
    main()
