#!/usr/bin/env python3
"""Analyze rng_log.txt: correlate MT19937 index/board changes with outputs."""
import sys

LOG = sys.argv[1] if len(sys.argv) > 1 else "rng_log.txt"

lines = open(LOG).read().split("\n")
assert lines[0].startswith("STATE"), lines[0]
init_idx = int(lines[0].split()[1])
state = [int(x) for x in lines[1].split()]
assert len(state) == 624, len(state)

# --- MT19937 (mirror of the game: output masked to 31 bits) -----------------
class MT:
    def __init__(self, state, idx):
        self.s = list(state)
        self.i = idx
    def twist(self):
        s = self.s
        for k in range(624):
            y = (s[k] & 0x80000000) | (s[(k + 1) % 624] & 0x7fffffff)
            s[k] = s[(k + 397) % 624] ^ (y >> 1) ^ (0x9908B0DF if (y & 1) else 0)
        self.i = 0
    def next(self):
        if self.i >= 624:
            self.twist()
        y = self.s[self.i]
        self.i += 1
        y ^= y >> 11
        y ^= (y << 7) & 0x9D2C5680
        y ^= (y << 15) & 0xEFC60000
        y ^= y >> 18
        return y & 0x7fffffff

# Sanity: reproduce the game's state evolution? We can't easily verify here, but
# we compute the forward output stream from the captured state.
mt = MT(state, init_idx)

# --- Parse samples ----------------------------------------------------------
samples = []  # (ms, idx, board)
for ln in lines[2:]:
    p = ln.split()
    if not p or p[0] != "T":
        continue
    ms = int(p[1]); idx = int(p[2]); board = [int(x) for x in p[3:3+64]]
    if len(board) != 64:
        continue
    samples.append((ms, idx, board))
print(f"samples={len(samples)} init_idx={init_idx}")

# --- Distinct board states --------------------------------------------------
states = []
prev = None
for ms, idx, b in samples:
    if prev is None or b != prev[2]:
        states.append((ms, idx, b))
        prev = (ms, idx, b)
print(f"distinct board states={len(states)}")

# --- Show index progression + changes --------------------------------------
for i in range(1, len(states)):
    pms, pidx, pb = states[i-1]
    ms, idx, b = states[i]
    changed = [(j, pb[j], b[j]) for j in range(64) if pb[j] != b[j]]
    if not changed:
        continue
    # raw index delta (may wrap)
    raw = idx - pidx
    # count of "new" cells: those that went to a color (not from another color
    # in a way consistent with a move) - just print all
    print(f"[{pms:6d}->{ms:6d}ms] idx {pidx}->{idx} (raw d={raw:+d}) "
          f"changed={len(changed)}: " +
          " ".join(f"{j}:{a}>{c}" for j, a, c in changed[:24]) +
          (" ..." if len(changed) > 24 else ""))
