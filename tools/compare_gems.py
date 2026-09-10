#!/usr/bin/env python3
"""Parse `bejeweled-bot.exe --dumpgem` output and compare the full field set of
selected slots (natural gems vs app-drawn gems)."""

import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "dump.txt"
txt = open(path).read()

sec = txt.split("full probe for special slots")[1]
blocks = re.findall(
    r"slot\s+(\d+) \(r(\d),c(\d)\) gem=(\w+)\n((?:\s+\+0x[0-9A-F]+ = [^\n]+\n)+)",
    sec)

data = {}
for idx, r, c, gem, body in blocks:
    d = {}
    for line in body.strip().split("\n"):
        m = re.match(r"\s+\+0x([0-9A-F]+) = 0x([0-9A-F]+) \((-?\d+)\)", line)
        if m:
            d[m.group(1)] = (int(m.group(2), 16), m.group(2))
    data[int(idx)] = (f"r{r}c{c}", gem, d)

slots = sorted(data)
print("slots:", [(s, data[s][0]) for s in slots])

offs = sorted({o for s in slots for o in data[s][2]})
print("\noffset   " + "".join(f"{s:>12}" for s in slots))
for o in offs:
    vals = [data[s][2].get(o, (-1, "??")) for s in slots]
    if len({v[0] for v in vals}) == 1:
        continue  # identical across all compared slots
    row = "".join(f"{v[1]:>12}" for v in vals)
    print(f"0x{o:<6}{row}")
