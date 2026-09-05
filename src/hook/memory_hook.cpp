#include "memory_hook.h"

#include <TlHelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>
#include <wchar.h>

namespace MemoryHook {

namespace {

struct State {
  DWORD pid = 0;
  HANDLE handle = nullptr;
  uint32_t moduleBase = 0;
  uint32_t moduleSize = 0;
  bool attached = false;
};

State g_state;

// Previous board snapshot, for diffing across two dumps (before/after a move).
uint32_t g_prevBoardAddr = 0;
std::vector<uint8_t> g_prevBoardBytes;

// ---------------------------------------------------------------------------
// Module enumeration: find Bejeweled3.exe base + size in the (32-bit) target.
// ---------------------------------------------------------------------------
bool FindModule(HANDLE hProcess, DWORD pid, uint32_t &base, uint32_t &size) {
  HANDLE snap =
      CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
  if (snap == INVALID_HANDLE_VALUE) {
    return false;
  }

  MODULEENTRY32W me;
  me.dwSize = sizeof(me);
  bool found = false;

  if (Module32FirstW(snap, &me)) {
    do {
      std::wstring name(me.szModule);
      // Case-insensitive compare against "Bejeweled3.exe"
      if (name.size() >= 14 &&
          _wcsicmp(me.szModule, L"Bejeweled3.exe") == 0) {
        base = (uint32_t)(uintptr_t)me.modBaseAddr;
        size = me.modBaseSize;
        found = true;
        break;
      }
    } while (Module32NextW(snap, &me));
  }

  CloseHandle(snap);
  return found;
}

// ---------------------------------------------------------------------------
// Hexdump helpers
// ---------------------------------------------------------------------------
void PrintHexLine(uint32_t addr, const uint8_t *p, size_t n) {
  char line[128];
  int off = std::snprintf(line, sizeof(line), "%08X  ", addr);
  char ascii[17];
  ascii[16] = 0;

  for (size_t i = 0; i < 16; i++) {
    if (i < n) {
      off += std::snprintf(line + off, sizeof(line) - off, "%02X ", p[i]);
      ascii[i] = (p[i] >= 0x20 && p[i] < 0x7f) ? (char)p[i] : '.';
    } else {
      off += std::snprintf(line + off, sizeof(line) - off, "   ");
      ascii[i] = ' ';
    }
    if (i == 7) {
      line[off++] = ' ';
    }
  }
  ascii[16] = 0;
  std::snprintf(line + off, sizeof(line) - off, " |%s|", ascii);
  std::cout << line << "\n";
}

bool Hexdump(uint32_t addr, size_t len, size_t maxLines = 256) {
  std::vector<uint8_t> buf(len);
  if (!Read(addr, buf.data(), len)) {
    return false;
  }
  size_t lines = (len + 15) / 16;
  lines = std::min(lines, maxLines);
  for (size_t l = 0; l < lines; l++) {
    PrintHexLine(addr + (uint32_t)(l * 16), buf.data() + l * 16,
                 std::min<size_t>(16, len - l * 16));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Memory region walker (VirtualQueryEx) + dword scanner
// ---------------------------------------------------------------------------
struct Region {
  uint32_t base;
  uint32_t size;
};

std::vector<Region> EnumerateReadableRegions(HANDLE hProcess) {
  std::vector<Region> regions;
  uintptr_t addr = 0x10000;
  const uintptr_t maxAddr = 0x7FFF0000; // 32-bit user space ceiling

  while (addr < maxAddr) {
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T r = VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi));
    if (r == 0) {
      break;
    }

    uintptr_t regionBase = (uintptr_t)mbi.BaseAddress;
    uintptr_t regionSize = mbi.RegionSize;

    bool readable = (mbi.State == MEM_COMMIT) &&
                    (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE |
                                    PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                    PAGE_EXECUTE_READWRITE |
                                    PAGE_EXECUTE_WRITECOPY));
    // Skip the module image itself: its .text immediates and .rdata/.data
    // references to vftables are false positives, not live heap objects.
    uintptr_t modEnd = (uintptr_t)g_state.moduleBase + g_state.moduleSize;
    bool inModule =
        (regionBase >= g_state.moduleBase && regionBase < modEnd);
    if (readable && !inModule && mbi.Protect != PAGE_NOACCESS &&
        !(mbi.Protect & PAGE_GUARD)) {
      regions.push_back({(uint32_t)regionBase, (uint32_t)regionSize});
    }

    addr = regionBase + regionSize;
    if (addr <= regionBase) {
      break;
    }
  }
  return regions;
}

// Scan committed readable regions for a 4-byte little-endian value. Returns
// the list of matching addresses.
std::vector<uint32_t> ScanForDword(uint32_t target) {
  std::vector<uint32_t> hits;
  auto regions = EnumerateReadableRegions(g_state.handle);

  std::vector<uint8_t> buf;
  for (const auto &r : regions) {
    buf.resize(r.size);
    if (!Read(r.base, buf.data(), r.size)) {
      continue;
    }
    for (size_t i = 0; i + 4 <= r.size; i++) {
      uint32_t v;
      std::memcpy(&v, buf.data() + i, 4);
      if (v == target) {
        hits.push_back(r.base + (uint32_t)i);
      }
    }
  }
  return hits;
}

// Print a 4-byte-aligned dump of dwords for quick pointer-field inspection.
void PrintDwords(uint32_t addr, uint32_t count) {
  std::vector<uint32_t> vals(count);
  if (!Read(addr, vals.data(), count * 4)) {
    std::cout << "  (unreadable)\n";
    return;
  }
  for (uint32_t i = 0; i < count; i++) {
    if (i % 8 == 0) {
      std::printf("  %08X:", addr + i * 4);
    }
    std::printf(" %08X", vals[i]);
    if (i % 8 == 7) {
      std::printf("\n");
    }
  }
  if (count % 8 != 0) {
    std::printf("\n");
  }
}

// Compare current board bytes against the previous snapshot and print the
// offsets that changed (4-byte granularity). Used to locate the 8x8 gem grid:
// make a move, re-dump, and the changed offsets reveal the grid location.
void DiffBoard(uint32_t addr, const uint8_t *cur, size_t len) {
  if (g_prevBoardAddr != addr || g_prevBoardBytes.size() < len) {
    std::cout << "  (no previous snapshot to diff against)\n";
    return;
  }
  size_t changes = 0;
  for (size_t i = 0; i + 4 <= len && i + 4 <= g_prevBoardBytes.size(); i += 4) {
    uint32_t a, b;
    std::memcpy(&a, cur + i, 4);
    std::memcpy(&b, g_prevBoardBytes.data() + i, 4);
    if (a != b) {
      if (changes == 0) {
        std::cout << "  Changed dword offsets (offset: old -> new):\n";
      }
      std::printf("    +0x%04X: 0x%08X -> 0x%08X\n", (unsigned)i, b, a);
      if (++changes >= 64) {
        std::cout << "    ... (truncated)\n";
        break;
      }
    }
  }
  if (changes == 0) {
    std::cout << "  (no changes since previous snapshot)\n";
  }
}

// Scan the board object for a likely 8x8 gem grid: 64 consecutive values in a
// small range (gem colors 0..6, special gems up to ~15). Reports candidate
// offsets so the real grid can be identified.
void ScanForGrid(uint32_t board) {
  std::vector<uint8_t> buf(0x4000);
  if (!Read(board, buf.data(), buf.size())) {
    return;
  }

  // Byte granularity: 64 consecutive bytes all < 16, >= 4 distinct (gem colors
  // 0..6 give 7 distinct; the 0/1/2 match-flag array has only 3, so it is
  // filtered out). Report up to 24 candidates.
  std::cout << "  Byte-grid candidates (64 bytes, >=4 distinct, all < 16):\n";
  int found = 0;
  for (size_t i = 0; i + 64 <= buf.size(); i++) {
    bool ok = true;
    for (size_t j = 0; j < 64; j++) {
      if (buf[i + j] >= 16) { ok = false; break; }
    }
    if (!ok) continue;
    int distinct = 0;
    bool seen[16] = {false};
    for (size_t j = 0; j < 64; j++) {
      if (!seen[buf[i + j]]) { seen[buf[i + j]] = true; distinct++; }
    }
    if (distinct < 4) continue;
    std::printf("    +0x%04X:", (unsigned)i);
    for (size_t j = 0; j < 64; j++) {
      if (j % 8 == 0) std::printf("\n      ");
      std::printf(" %u", (unsigned)buf[i + j]);
    }
    std::printf("\n");
    if (++found >= 24) break;
  }
  if (found == 0) std::cout << "    (none)\n";

  // Dword granularity: 64 consecutive dwords all < 64, >= 4 distinct.
  std::cout << "  Dword-grid candidates (64 dwords, >=4 distinct, all < 64):\n";
  found = 0;
  for (size_t i = 0; i + 64 * 4 <= buf.size(); i += 4) {
    bool ok = true;
    for (size_t j = 0; j < 64; j++) {
      uint32_t v;
      std::memcpy(&v, buf.data() + i + j * 4, 4);
      if (v >= 64) { ok = false; break; }
    }
    if (!ok) continue;
    int distinct = 0;
    bool seen[64] = {false};
    for (size_t j = 0; j < 64; j++) {
      uint32_t v;
      std::memcpy(&v, buf.data() + i + j * 4, 4);
      if (!seen[v]) { seen[v] = true; distinct++; }
    }
    if (distinct < 4) continue;
    std::printf("    +0x%04X:", (unsigned)i);
    for (size_t j = 0; j < 64; j++) {
      uint32_t v;
      std::memcpy(&v, buf.data() + i + j * 4, 4);
      if (j % 8 == 0) std::printf("\n      ");
      std::printf(" %u", v);
    }
    std::printf("\n");
    if (++found >= 24) break;
  }
  if (found == 0) std::cout << "    (none)\n";

  // Pointer granularity: 64 consecutive dwords that all look like heap
  // pointers (non-polymorphic gem objects would be stored as pointers).
  std::cout << "  Pointer-grid candidates (64 heap pointers):\n";
  found = 0;
  for (size_t i = 0; i + 64 * 4 <= buf.size(); i += 4) {
    bool ok = true;
    for (size_t j = 0; j < 64; j++) {
      uint32_t v;
      std::memcpy(&v, buf.data() + i + j * 4, 4);
      if (v < 0x0F000000 || v >= 0x7FFF0000) { ok = false; break; }
    }
    if (!ok) continue;
    std::printf("    +0x%04X (64 pointers)\n", (unsigned)i);
    if (++found >= 24) break;
  }
  if (found == 0) std::cout << "    (none)\n";
}

// Process-wide scan for the 8x8 gem TYPE grid. The board holds pointers to
// gem meshes and render state, but the actual 8x8 array of gem colors may
// live in its own heap object. Gem colors appear to be bitmask-encoded
// (1,2,4,8,16,32,64) rather than sequential 0..6, so accept any value <= 0x80.
// Filter on >= 4 distinct values and >= 16 non-zero cells to reject the
// sparse animation/flag arrays, and skip overlapping (sliding-window) hits.
void ScanForGemGridProcessWide() {
  auto regions = EnumerateReadableRegions(g_state.handle);
  std::cout << "\n------------------------------------------------------------\n";
  std::cout << "Process-wide scan for 8x8 gem grid (dense 64-cell arrays):\n";

  int foundBytes = 0;
  int foundDwords = 0;
  std::vector<uint8_t> buf;

  // Byte granularity
  for (const auto &r : regions) {
    if (foundBytes >= 32) break;
    buf.resize(r.size);
    if (!Read(r.base, buf.data(), r.size)) continue;
    size_t skip = 0;
    for (size_t i = 0; i + 64 <= r.size; i++) {
      if (i < skip) continue;
      bool ok = true;
      int distinct = 0, nonzero = 0;
      bool seen[0x81] = {false};
      for (size_t j = 0; j < 64; j++) {
        uint8_t v = buf[i + j];
        if (v > 0x80) { ok = false; break; }
        if (v) nonzero++;
        if (!seen[v]) { seen[v] = true; distinct++; }
      }
      if (!ok || distinct < 4 || nonzero < 16) continue;
      std::printf("  BYTE grid @ 0x%08X (%d distinct, %d nonzero):\n",
                  r.base + (uint32_t)i, distinct, nonzero);
      for (int row = 0; row < 8; row++) {
        std::printf("    ");
        for (int c = 0; c < 8; c++) std::printf("%d ", buf[i + row * 8 + c]);
        std::printf("\n");
      }
      skip = i + 64; // skip overlapping windows of the same array
      if (++foundBytes >= 32) break;
    }
  }

  // Dword granularity
  for (const auto &r : regions) {
    if (foundDwords >= 32) break;
    buf.resize(r.size);
    if (!Read(r.base, buf.data(), r.size)) continue;
    size_t skip = 0;
    for (size_t i = 0; i + 256 <= r.size; i += 4) {
      if (i < skip) continue;
      bool ok = true;
      int distinct = 0, nonzero = 0;
      bool seen[0x81] = {false};
      for (size_t j = 0; j < 64; j++) {
        uint32_t v;
        std::memcpy(&v, buf.data() + i + j * 4, 4);
        if (v > 0x80) { ok = false; break; }
        if (v) nonzero++;
        if (!seen[v]) { seen[v] = true; distinct++; }
      }
      if (!ok || distinct < 4 || nonzero < 16) continue;
      std::printf("  DWORD grid @ 0x%08X (%d distinct, %d nonzero):\n",
                  r.base + (uint32_t)i, distinct, nonzero);
      for (int row = 0; row < 8; row++) {
        std::printf("    ");
        for (int c = 0; c < 8; c++) {
          uint32_t v;
          std::memcpy(&v, buf.data() + i + (row * 8 + c) * 4, 4);
          std::printf("%d ", v);
        }
        std::printf("\n");
      }
      skip = i + 256; // skip overlapping windows
      if (++foundDwords >= 32) break;
    }
  }

  // Pointer granularity: 64 consecutive heap pointers (gem grid may hold
  // pointers to gem mesh objects rather than small type codes).
  int foundPtr = 0;
  std::cout << "  Pointer-grid candidates (64 heap pointers):\n";
  for (const auto &r : regions) {
    if (foundPtr >= 24) break;
    buf.resize(r.size);
    if (!Read(r.base, buf.data(), r.size)) continue;
    size_t skip = 0;
    for (size_t i = 0; i + 256 <= r.size; i += 4) {
      if (i < skip) continue;
      bool ok = true;
      uint32_t first = 0, distinct = 0;
      bool seen[0x40] = {false};
      for (size_t j = 0; j < 64; j++) {
        uint32_t v;
        std::memcpy(&v, buf.data() + i + j * 4, 4);
        if (v < 0x00F00000 || v >= 0x7FFF0000) { ok = false; break; }
        if (j == 0) first = v;
        uint32_t key = (v >> 12) & 0x3f;
        if (!seen[key]) { seen[key] = true; distinct++; }
      }
      if (!ok) continue;
      std::printf("    @ 0x%08X (first 0x%08X, %u distinct page-hint)\n",
                  r.base + (uint32_t)i, first, distinct);
      skip = i + 256;
      if (++foundPtr >= 24) break;
    }
  }
  if (foundPtr == 0) std::cout << "    (none)\n";

  // Strided byte scan: gem grid may be interleaved with a state/flag byte
  // (stride 2) or packed in a small struct (stride 4). Check 64 cells at
  // fixed stride for values <= 0x80 with >= 4 distinct and >= 16 non-zero.
  int foundStride = 0;
  std::cout << "  Strided-grid candidates (stride 4..64, 64 cells <= 0x80):\n";
  for (int stride : {4, 8, 16, 32, 64}) {
    int foundThis = 0;
    for (const auto &r : regions) {
      if (foundThis >= 12) break;
      buf.resize(r.size);
      if (!Read(r.base, buf.data(), r.size)) continue;
      for (size_t i = 0; i + 63 * stride + 1 <= r.size; i++) {
        bool ok = true;
        int distinct = 0, nonzero = 0;
        bool seen[0x81] = {false};
        for (size_t j = 0; j < 64; j++) {
          uint8_t v = buf[i + j * stride];
          if (v > 0x80) { ok = false; break; }
          if (v) nonzero++;
          if (!seen[v]) { seen[v] = true; distinct++; }
        }
        if (!ok || distinct < 4 || nonzero < 16) continue;
        std::printf("    stride %d @ 0x%08X (%d distinct, %d nonzero):\n",
                    stride, r.base + (uint32_t)i, distinct, nonzero);
        for (size_t j = 0; j < 64; j++) {
          if (j % 8 == 0) std::printf("      ");
          std::printf("%d ", buf[i + j * stride]);
          if (j % 8 == 7) std::printf("\n");
        }
        i += 63 * stride; // skip overlapping windows
        if (++foundThis >= 12) break;
      }
    }
    if (foundThis == 0) std::printf("    stride %d: (none)\n", stride);
    foundStride += foundThis;
  }
  if (foundStride == 0) std::cout << "    (none)\n";

  // Strided pointer scan: gem grid may store a pointer (to one of the 8 gem
  // type objects) at a fixed stride within a per-cell struct.
  int foundPtrStride = 0;
  std::cout << "  Strided-pointer candidates (64 pointers at stride):\n";
  for (int stride : {4, 8, 16, 32}) {
    for (const auto &r : regions) {
      if (foundPtrStride >= 24) break;
      buf.resize(r.size);
      if (!Read(r.base, buf.data(), r.size)) continue;
      for (size_t i = 0; i + 63 * stride + 4 <= r.size; i++) {
        bool ok = true;
        int distinct = 0;
        bool seen[0x40] = {false};
        for (size_t j = 0; j < 64; j++) {
          uint32_t v;
          std::memcpy(&v, buf.data() + i + j * stride, 4);
          if (v < 0x00F00000 || v >= 0x7FFF0000) { ok = false; break; }
          uint32_t key = (v >> 12) & 0x3f;
          if (!seen[key]) { seen[key] = true; distinct++; }
        }
        if (!ok || distinct < 2) continue;
        std::printf("    stride %d @ 0x%08X (%u distinct page-hint)\n", stride,
                    r.base + (uint32_t)i, distinct);
        i += 63 * stride;
        if (++foundPtrStride >= 24) break;
      }
    }
  }
  if (foundPtrStride == 0) std::cout << "    (none)\n";

  if (foundBytes == 0 && foundDwords == 0 && foundPtr == 0 && foundStride == 0)
    std::cout << "  (no dense 64-cell array found in any readable region)\n";
}

// Scan the module image itself (base..base+size) for the 8x8 gem grid. The
// grid may be a global/static array in .data/.bss rather than a heap object.
void ScanModuleForGrid() {
  uint32_t base = g_state.moduleBase;
  uint32_t size = g_state.moduleSize;
  std::vector<uint8_t> buf(size);
  if (size == 0 || !Read(base, buf.data(), size)) return;

  std::cout << "\nModule scan for 8x8 gem grid (dense 64-cell arrays):\n";
  int found = 0;

  // Byte granularity
  for (size_t i = 0; i + 64 <= size; i++) {
    bool ok = true;
    int distinct = 0, nonzero = 0;
    bool seen[0x81] = {false};
    for (size_t j = 0; j < 64; j++) {
      uint8_t v = buf[i + j];
      if (v > 0x80) { ok = false; break; }
      if (v) nonzero++;
      if (!seen[v]) { seen[v] = true; distinct++; }
    }
    if (!ok || distinct < 4 || nonzero < 16) continue;
    std::printf("  BYTE grid @ 0x%08X (%d distinct, %d nonzero):\n",
                base + (uint32_t)i, distinct, nonzero);
    for (int r = 0; r < 8; r++) {
      std::printf("    ");
      for (int c = 0; c < 8; c++) std::printf("%d ", buf[i + r * 8 + c]);
      std::printf("\n");
    }
    i += 63; // skip overlapping
    if (++found >= 32) break;
  }

  // Dword granularity
  for (size_t i = 0; i + 256 <= size; i += 4) {
    bool ok = true;
    int distinct = 0, nonzero = 0;
    bool seen[0x81] = {false};
    for (size_t j = 0; j < 64; j++) {
      uint32_t v;
      std::memcpy(&v, buf.data() + i + j * 4, 4);
      if (v > 0x80) { ok = false; break; }
      if (v) nonzero++;
      if (!seen[v]) { seen[v] = true; distinct++; }
    }
    if (!ok || distinct < 4 || nonzero < 16) continue;
    std::printf("  DWORD grid @ 0x%08X (%d distinct, %d nonzero):\n",
                base + (uint32_t)i, distinct, nonzero);
    for (int r = 0; r < 8; r++) {
      std::printf("    ");
      for (int c = 0; c < 8; c++) {
        uint32_t v;
        std::memcpy(&v, buf.data() + i + (r * 8 + c) * 4, 4);
        std::printf("%d ", v);
      }
      std::printf("\n");
    }
    i += 255; // skip overlapping
    if (++found >= 32) break;
  }

  if (found == 0) std::cout << "  (no dense 64-cell array in module)\n";
}

} // namespace

// ===========================================================================
// Public API
// ===========================================================================

bool Attach(HWND hwnd) {
  Detach();

  if (!hwnd) {
    std::cerr << "[MemoryHook] No window handle.\n";
    return false;
  }

  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0) {
    std::cerr << "[MemoryHook] Could not get process id.\n";
    return false;
  }

  HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (!h) {
    std::cerr << "[MemoryHook] OpenProcess failed (err " << GetLastError()
              << ").\n";
    return false;
  }

  uint32_t base = 0, size = 0;
  if (!FindModule(h, pid, base, size)) {
    std::cerr << "[MemoryHook] Could not find Bejeweled3.exe module.\n";
    CloseHandle(h);
    return false;
  }

  g_state.pid = pid;
  g_state.handle = h;
  g_state.moduleBase = base;
  g_state.moduleSize = size;
  g_state.attached = true;
  return true;
}

void Detach() {
  if (g_state.handle) {
    CloseHandle(g_state.handle);
    g_state.handle = nullptr;
  }
  g_state.pid = 0;
  g_state.moduleBase = 0;
  g_state.moduleSize = 0;
  g_state.attached = false;
}

bool IsAttached() { return g_state.attached; }

bool Read(uint32_t address, void *buffer, size_t size) {
  if (!g_state.attached) {
    return false;
  }
  SIZE_T n = 0;
  return ReadProcessMemory(g_state.handle, (LPCVOID)(uintptr_t)address, buffer,
                           size, &n) &&
         n == size;
}

bool ReadU32(uint32_t address, uint32_t &out) {
  return Read(address, &out, sizeof(out));
}

bool ReadPtr(uint32_t address, uint32_t &out) { return ReadU32(address, out); }

uint32_t ModuleBase() { return g_state.moduleBase; }
uint32_t ModuleSize() { return g_state.moduleSize; }

// ===========================================================================
// Diagnostic dump
// ===========================================================================

void DumpBoardState() {
  if (!g_state.attached) {
    std::cerr << "[MemoryHook] Not attached. Call Attach() first.\n";
    return;
  }

  const uint32_t base = g_state.moduleBase;

  std::cout << "\n============================================================\n";
  std::cout << "  BEJEWELED 3 MEMORY DUMP\n";
  std::cout << "============================================================\n";
  std::printf("  PID          : %u\n", (unsigned int)g_state.pid);
  std::printf("  Module base  : 0x%08X\n", base);
  std::printf("  Module size  : 0x%08X (%u bytes)\n", g_state.moduleSize,
              g_state.moduleSize);

  // 1. Read the global app pointer
  uint32_t gApp = 0;
  if (ReadPtr(base + kGAppGlobalRva, gApp)) {
    std::printf("  gSexyAppBase : 0x%08X\n", gApp);
  } else {
    std::printf("  gSexyAppBase : <unreadable @ 0x%08X>\n",
                base + kGAppGlobalRva);
  }

  const uint32_t meshVft = base + kMeshPieceVftRva;
  const uint32_t pieceVft = base + kD3DMeshPieceVftRva;
  std::printf("  MeshPiece vft: 0x%08X (scan target)\n", meshVft);
  std::printf("  D3DMesh vft  : 0x%08X (scan target)\n", pieceVft);

  // 2. Scan for Board instances (base + derived subclasses)
  const uint32_t boardVfts[] = {
      base + kBoardVftRva,          // Sexy::Board (abstract base)
      base + kClassicBoardVftRva,   // Sexy::ClassicBoard
      base + kSpeedBoardVftRva,     // Sexy::SpeedBoard
      base + kZenBoardVftRva,       // Sexy::ZenBoard
      base + kQuestBoardVftRva,     // Sexy::QuestBoard
      base + kPokerBoardVftRva,     // Sexy::PokerBoard
      base + kTimeLimitBoardVftRva, // Sexy::TimeLimitBoard
  };
  std::cout << "\n------------------------------------------------------------\n";
  std::cout << "Scanning process memory for Board vftables (base+derived)...\n";
  std::vector<uint32_t> boards;
  for (uint32_t vft : boardVfts) {
    std::vector<uint32_t> hits = ScanForDword(vft);
    std::printf("  0x%08X -> %zu hit(s)\n", vft, hits.size());
    for (uint32_t h : hits) {
      boards.push_back(h);
      std::printf("    Board @ 0x%08X\n", h);
    }
  }
  std::printf("  Total Board instance(s): %zu\n", boards.size());

  // 3. Scan for MeshPiece instances (candidate gem class)
  std::cout << "\nScanning process memory for MeshPiece vftable...\n";
  std::vector<uint32_t> meshes = ScanForDword(meshVft);
  std::printf("  Found %zu MeshPiece instance(s)\n", meshes.size());
  for (size_t i = 0; i < meshes.size() && i < 32; i++) {
    std::printf("    Mesh[%zu] @ 0x%08X\n", i, meshes[i]);
  }
  if (meshes.size() > 32) {
    std::printf("    ... and %zu more\n", meshes.size() - 32);
  }

  // 4. Scan for D3DMeshPiece instances (3D scene meshes)
  std::cout << "\nScanning process memory for D3DMeshPiece vftable...\n";
  std::vector<uint32_t> pieces = ScanForDword(pieceVft);
  std::printf("  Found %zu D3DMeshPiece instance(s)\n", pieces.size());
  for (size_t i = 0; i < pieces.size() && i < 32; i++) {
    std::printf("    Piece[%zu] @ 0x%08X\n", i, pieces[i]);
  }
  if (pieces.size() > 32) {
    std::printf("    ... and %zu more\n", pieces.size() - 32);
  }

  // 4. Dump gSexyAppBase object (to trace app -> board pointer chain)
  if (gApp) {
    std::cout << "\n------------------------------------------------------------\n";
    std::cout << "gSexyAppBase object @ 0x" << std::hex << gApp << std::dec
              << " (first 0x1000 bytes):\n";
    Hexdump(gApp, 0x1000, 128); // 128 lines = 2KB, enough for the manager ptrs
  }

  // 5. Dump the first Board instance (dword view + diff vs previous dump)
  if (!boards.empty()) {
    uint32_t b = boards[0];
    std::cout << "\n------------------------------------------------------------\n";
    std::cout << "Board[0] object @ 0x" << std::hex << b << std::dec << "\n";

    std::vector<uint8_t> buf(0x4000);
    if (Read(b, buf.data(), buf.size())) {
      DiffBoard(b, buf.data(), buf.size());
      g_prevBoardAddr = b;
      g_prevBoardBytes = buf;
    }

    std::cout << "Dword view (0x4000 bytes = 4096 dwords):\n";
    PrintDwords(b, 0x4000 / 4);

    ScanForGrid(b);

    // Targeted dump of the suspected 8x8 gem grid. The Board constructor
    // (sub_74ae30) zeroes 64 dwords at [this + (0x7 + 0x37 + row*8 + col)*4],
    // i.e. byte offset 0xF8, row stride 8 dwords.
    {
      const uint32_t gridOff = 0xF8;
      uint32_t cells[64];
      if (Read(b + gridOff, cells, sizeof(cells))) {
        std::cout << "\n--- Gem grid @ board+0xF8 (8x8 dwords) ---\n";
        for (int r = 0; r < 8; r++) {
          std::printf("  row %d:", r);
          for (int c = 0; c < 8; c++) {
            std::printf(" %08X", cells[r * 8 + c]);
          }
          std::printf("\n");
        }
      } else {
        std::cout << "\n--- Gem grid @ board+0xF8: unreadable ---\n";
      }

      // Raw byte hexdump of the region that holds the 8x8 state array (+0x688)
      // and (likely) the gem color array. Byte-level view reveals interleaved
      // or strided layouts that the dword view obscures.
      std::cout << "\n--- Board bytes @ +0x600..+0x900 (raw hexdump) ---\n";
      Hexdump(b + 0x600, 0x300, 0x300 / 16);

      // Follow the 8 gem-type pointers (column 1 of the +0xF8 table, i.e.
      // +0xF8 + r*32 + 4). Each points to a per-color gem object; dumping the
      // pointed-to bytes reveals the type structure used to map grid cells to
      // colors.
      std::cout << "\n--- Gem type objects (8 pointers from +0xF8 col 1) ---\n";
      for (int i = 0; i < 8; i++) {
        uint32_t p = 0;
        if (ReadU32(b + 0xF8 + (uint32_t)i * 32 + 4, p)) {
          std::printf("  type[%d] -> 0x%08X:\n", i, p);
          if (p >= 0x00F00000 && p < 0x7FFF0000) {
            Hexdump(p, 0x40, 4);
          } else {
            std::cout << "    (not a heap pointer)\n";
          }
        }
      }
    }
  }

  // 6. Dump the first few MeshPiece instances (candidate gems)
  size_t meshDumpCount = std::min<size_t>(meshes.size(), 12);
  for (size_t i = 0; i < meshDumpCount; i++) {
    std::cout << "\n------------------------------------------------------------\n";
    std::cout << "Mesh[" << i << "] object @ 0x" << std::hex << meshes[i]
              << std::dec << " (hexdump 0x80 bytes):\n";
    Hexdump(meshes[i], 0x80, 8);
  }

  ScanForGemGridProcessWide();

  ScanModuleForGrid();

  std::cout << "\n============================================================\n";
  std::cout << "  END OF DUMP\n";
  std::cout << "============================================================\n\n";
}

void WatchBoardForMove() {
  if (!g_state.attached) {
    std::cerr << "[MemoryHook] Not attached.\n";
    return;
  }
  const uint32_t base = g_state.moduleBase;
  const uint32_t boardVfts[] = {
      base + kBoardVftRva, base + kClassicBoardVftRva,
      base + kSpeedBoardVftRva, base + kZenBoardVftRva,
      base + kQuestBoardVftRva, base + kPokerBoardVftRva,
      base + kTimeLimitBoardVftRva,
  };
  uint32_t board = 0;
  for (uint32_t vft : boardVfts) {
    for (uint32_t h : ScanForDword(vft)) {
      if ((h & 3) == 0) { board = h; break; }
    }
    if (board) break;
  }
  if (!board) {
    std::cout << "Board not found.\n";
    return;
  }

  std::vector<uint8_t> snap(0x4000);
  if (!Read(board, snap.data(), snap.size())) return;
  std::cout << "Board @ 0x" << std::hex << board << std::dec
            << " snapshotted. MAKE A MOVE NOW (swap two adjacent gems) and "
               "wait ~30s...\n";

  Sleep(30000);

  std::vector<uint8_t> cur(0x4000);
  if (!Read(board, cur.data(), cur.size())) return;

  std::cout << "Changed dwords (offset: before -> after):\n";
  int n = 0;
  for (size_t i = 0; i + 4 <= 0x4000; i += 4) {
    uint32_t a, b;
    std::memcpy(&a, snap.data() + i, 4);
    std::memcpy(&b, cur.data() + i, 4);
    if (a != b) {
      std::printf("  +0x%04X: %08X -> %08X\n", (unsigned)i, a, b);
      if (++n >= 128) {
        std::cout << "  ... (truncated)\n";
        break;
      }
    }
  }
  std::cout << "Total changed dwords: " << n << "\n";
}

// Read one cell value at a given stride (1 = byte, 2 = short, 4 = dword).
static uint32_t CellValue(const uint8_t *p, int stride, bool byteMode) {
  if (byteMode) return p[0];
  if (stride == 1) return p[0];
  if (stride == 2) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
  }
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

// Check whether a 64-cell run matches the board's equality pattern: cells with
// the same color must hold the same value, different colors different values,
// and all 7 colors must be present. Independent of the actual encoding.
static bool PatternMatch(const uint8_t *p, int stride, const int *board,
                         bool byteMode) {
  uint32_t valueForLabel[7];
  bool seen[7] = {false};
  for (int i = 0; i < 64; i++) {
    int l = board[i];
    uint32_t v = CellValue(p + (size_t)i * stride, stride, byteMode);
    if (!seen[l]) {
      seen[l] = true;
      valueForLabel[l] = v;
    } else if (valueForLabel[l] != v) {
      return false;
    }
  }
  for (int l = 0; l < 7; l++)
    if (!seen[l]) return false;
  for (int a = 0; a < 7; a++)
    for (int b = a + 1; b < 7; b++)
      if (valueForLabel[a] == valueForLabel[b]) return false;
  return true;
}

void FindGemGridByPattern(const int board[64]) {
  if (!g_state.attached) {
    std::cerr << "[MemoryHook] Not attached.\n";
    return;
  }

  const uint32_t base = g_state.moduleBase;
  const uint32_t boardVfts[] = {
      base + kBoardVftRva, base + kClassicBoardVftRva,
      base + kSpeedBoardVftRva, base + kZenBoardVftRva,
      base + kQuestBoardVftRva, base + kPokerBoardVftRva,
      base + kTimeLimitBoardVftRva,
  };
  uint32_t boardAddr = 0;
  for (uint32_t vft : boardVfts) {
    for (uint32_t h : ScanForDword(vft)) {
      if ((h & 3) == 0) { boardAddr = h; break; }
    }
    if (boardAddr) break;
  }

  // Board variants: transposed (column-major) and reversed rows (bottom-up).
  int boardT[64];
  int boardRev[64];
  int boardTRev[64];
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
      boardT[c * 8 + r] = board[r * 8 + c];
      boardRev[(7 - r) * 8 + c] = board[r * 8 + c];
      boardTRev[c * 8 + (7 - r)] = board[r * 8 + c];
    }

  bool found = false;
  auto tryRegion = [&](uint32_t addr, std::vector<uint8_t> &buf,
                       const char *label, bool full) {
    const int *variants[4] = {board, boardT, boardRev, boardTRev};
    const char *vname[4] = {"row", "transposed", "rev-rows", "rev+trans"};
    const int stridesFull[] = {1, 2, 4, 8, 16, 32};
    const int stridesFast[] = {1, 4};
    const int *strides = full ? stridesFull : stridesFast;
    int nStride = full ? 6 : 2;
    int nVariant = full ? 4 : 2;
    for (int si = 0; si < nStride; si++) {
      int stride = strides[si];
      size_t span = 64 * (size_t)stride;
      for (size_t i = 0; i + span <= buf.size(); i++) {
        for (int variant = 0; variant < nVariant; variant++) {
          const int *b = variants[variant];
          for (int byteMode = 0; byteMode < (full ? 2 : 1); byteMode++) {
            if (stride == 1 && byteMode == 1) continue;
            if (!PatternMatch(buf.data() + i, stride, b, byteMode == 1))
              continue;
            std::printf("  MATCH @ 0x%08X  stride=%d  %s  %s%s\n",
                        addr + (uint32_t)i, stride, label, vname[variant],
                        byteMode ? " (byte)" : "");
            std::cout << "  values (in found order):\n";
            for (int r = 0; r < 8; r++) {
              std::printf("    ");
              for (int c = 0; c < 8; c++) {
                std::printf(
                    "%u ", CellValue(buf.data() + i + (size_t)(r * 8 + c) * stride,
                                   stride, byteMode == 1));
              }
              std::printf("\n");
            }
            std::cout << "  value -> label mapping:\n";
            uint32_t valueForLabel[7];
            bool seen[7] = {false};
            for (int j = 0; j < 64; j++) {
              int l = b[j];
              if (!seen[l]) {
                seen[l] = true;
                valueForLabel[l] = CellValue(
                    buf.data() + i + (size_t)j * stride, stride, byteMode == 1);
              }
            }
            for (int l = 0; l < 7; l++)
              std::printf("    label %d -> value %u\n", l, valueForLabel[l]);
            found = true;
            return;
          }
        }
      }
    }
  };

  if (boardAddr) {
    // The board object may be larger than 16KB; read up to 64KB.
    std::vector<uint8_t> buf(0x10000);
    if (Read(boardAddr, buf.data(), buf.size()))
      tryRegion(boardAddr, buf, "board object", true);
  }
  if (!found) {
    std::cout << "  (not in board object, scanning process heap)\n";
    for (const auto &r : EnumerateReadableRegions(g_state.handle)) {
      std::vector<uint8_t> buf(r.size);
      if (!Read(r.base, buf.data(), r.size)) continue;
      tryRegion(r.base, buf, "heap", false);
      if (found) break;
    }
  }
  if (!found)
    std::cout << "  No 64-cell array matched the board pattern.\n";
}

// ---------------------------------------------------------------------------
// Live board read: locate the Board object, then dereference the 8x8 grid of
// gem pointers at +0xF8 and read each gem's type dword at +0x18.
// ---------------------------------------------------------------------------
static uint32_t FindBoardObject() {
  const uint32_t base = g_state.moduleBase;
  const uint32_t boardVfts[] = {
      base + kBoardVftRva,          base + kClassicBoardVftRva,
      base + kSpeedBoardVftRva,     base + kZenBoardVftRva,
      base + kQuestBoardVftRva,     base + kPokerBoardVftRva,
      base + kTimeLimitBoardVftRva,
  };
  for (uint32_t vft : boardVfts) {
    for (uint32_t h : ScanForDword(vft)) {
      if ((h & 3) == 0) return h;
    }
  }
  return 0;
}

bool ReadBoard(int board[64]) {
  if (!g_state.attached) {
    std::cerr << "[MemoryHook] Not attached.\n";
    return false;
  }

  uint32_t b = FindBoardObject();
  if (!b) {
    std::cerr << "[MemoryHook] Board object not found.\n";
    return false;
  }
  std::printf("\nBoard @ 0x%08X\n", b);

  uint32_t grid[64];
  if (!Read(b + kGridOffset, grid, sizeof(grid))) {
    std::printf("[MemoryHook] Failed to read gem grid @ +0x%X.\n", kGridOffset);
    return false;
  }

  // Raw pointer grid (diagnostic).
  std::cout << "Gem pointers @ +0xF8:\n";
  for (int r = 0; r < 8; r++) {
    std::printf("  row %d:", r);
    for (int c = 0; c < 8; c++) {
      std::printf(" %08X", grid[r * 8 + c]);
    }
    std::printf("\n");
  }

  // Dereference each pointer and read the type dword at +kGemTypeOffset.
  int deref = 0;
  for (int i = 0; i < 64; i++) {
    uint32_t gem = grid[i];
    uint32_t type = 0xFFFFFFFF;
    if (gem >= 0x00F00000 && gem < 0x7FFF0000 &&
        ReadU32(gem + kGemTypeOffset, type)) {
      deref++;
    }
    board[i] = (int)type;
  }
  std::printf("Dereferenced %d/64 gem pointers (type @ +0x%X).\n", deref,
              kGemTypeOffset);

  std::cout << "8x8 gem grid (game-internal color codes):\n";
  for (int r = 0; r < 8; r++) {
    std::printf("  ");
    for (int c = 0; c < 8; c++) {
      std::printf(" %2d", board[r * 8 + c]);
    }
    std::printf("\n");
  }

  return deref == 64;
}

} // namespace MemoryHook
