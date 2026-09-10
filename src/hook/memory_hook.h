#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>

// ============================================================================
// Memory Hook - read the Bejeweled 3 board state directly from the game
// process instead of classifying pixels with computer vision.
//
// The game is a 32-bit process; the bot is 64-bit. ReadProcessMemory handles
// cross-bitness reads transparently, so all game addresses are treated as
// 32-bit values zero-extended into uintptr_t.
//
// Addresses below are RVAs (relative to the loaded image base). They were
// reverse-engineered from Bejeweled3.exe.bndb (image base 0x400000):
//   gSexyAppBase global   : bndb 0x8e170c -> RVA 0x4e170c
//   Sexy::Board vftable    : bndb 0x8650e4 -> RVA 0x4650e4
//   Sexy::D3DMeshPiece vft : bndb 0x87eb24 -> RVA 0x47eb24
// ============================================================================

namespace MemoryHook {

// --- RVA constants (offsets from the loaded module base) -------------------
constexpr uint32_t kGAppGlobalRva = 0x4e170c;      // gSexyAppBase pointer
constexpr uint32_t kBoardVftRva = 0x464cb4;        // Sexy::Board primary vftable
                                                   //   (offset 0, {for Widget})
constexpr uint32_t kMeshPieceVftRva = 0x473a84;    // Sexy::MeshPiece vftable
constexpr uint32_t kD3DMeshPieceVftRva = 0x47eb24; // Sexy::D3DMeshPiece vftable

// Board is abstract; the live board is one of these derived subclasses. Each
// primary vftable lives at object offset 0 ({for Widget}).
constexpr uint32_t kClassicBoardVftRva = 0x46511c;   // Sexy::ClassicBoard (primary vftable{for Board})
constexpr uint32_t kSpeedBoardVftRva = 0x466a00;     // Sexy::SpeedBoard
constexpr uint32_t kZenBoardVftRva = 0x466f9c;       // Sexy::ZenBoard
constexpr uint32_t kQuestBoardVftRva = 0x467420;     // Sexy::QuestBoard
constexpr uint32_t kPokerBoardVftRva = 0x4697c8;     // Sexy::PokerBoard
constexpr uint32_t kTimeLimitBoardVftRva = 0x46a698; // Sexy::TimeLimitBoard

// Offsets within Board / gem objects (byte offsets), reverse-engineered:
//   Board + kGridOffset    -> 64 gem object pointers (8x8, 4 bytes each)
//   gem   + kGemTypeOffset -> gem type (color 0..6, or -1 = hypercube)
//   gem   + kSpecialStateOffset -> special state (0 normal, 1 flame, 2 star, 4 supernova)
constexpr uint32_t kGridOffset = 0xF8;
constexpr uint32_t kGemTypeOffset = 0x220;
constexpr uint32_t kSpecialStateOffset = 0x228;

// Special-gem state values at kSpecialStateOffset (verified in-game by
// comparing naturally-created specials against ones written by the editor):
//   0 = normal, 1 = flame, 2 = hypercube, 4 = star, 5 = supernova
// Hypercubes additionally carry kGemTypeOffset == -1 (0xFFFFFFFF).
constexpr int kStateNormal = 0;
constexpr int kStateFlame = 1;
constexpr int kStateHypercube = 2;
constexpr int kStateStar = 4;
constexpr int kStateSupernova = 5;

// --- Attach / detach --------------------------------------------------------
bool Attach(HWND hwnd);
void Detach();
bool IsAttached();

// --- Raw reads (32-bit target) ---------------------------------------------
bool Read(uint32_t address, void *buffer, size_t size);
bool ReadU32(uint32_t address, uint32_t &out);
bool ReadPtr(uint32_t address, uint32_t &out);

// --- Raw writes (32-bit target) --------------------------------------------
// Requires the process to be opened with VM_WRITE (done by Attach).
bool Write(uint32_t address, const void *buffer, size_t size);

// --- Test hooks -------------------------------------------------------------
// Overwrite the live board with a 2-colour checkerboard: colorA where
// (row+col) is even, colorB where odd. Special state is reset and each gem is
// snapped to its resting position (col*cellSize, row*cellSize) so the board is
// immediately settled. Returns the number of gem slots written (0 on failure).
int SetBoardCheckerboard(int colorA, int colorB, float cellSize);

// Write selected cells of the live board. `mask[i]` != 0 => cell i is written
// with colour `color[i]` (0..6, or -1 = hypercube) and special state
// `state[i]` (0 normal, 1 flame, 2 supernova, 4 star); the gem is snapped to
// its resting position (col*cellSize, row*cellSize). Returns the number of
// cells written (0 on failure).
int WriteBoardCells(const uint8_t mask[64], const int color[64],
                    const int state[64], float cellSize);

// --- Metadata ---------------------------------------------------------------
uint32_t ModuleBase(); // loaded image base (ASLR-aware)
uint32_t ModuleSize();

// --- Diagnostics ------------------------------------------------------------
// Scan the game process for Board / piece instances and dump their memory as
// human-readable text (hexdump + interpreted pointer fields). Pastable log.
void DumpBoardState();

// Snapshot the board object, wait ~15s for the user to make a move, then
// re-read and print every dword that changed. A swap changes exactly two
// cells; the changed offsets reveal the 8x8 gem grid location and encoding.
void WatchBoardForMove();

// Search process memory for the 8x8 gem grid by matching a KNOWN board state.
// `board` is 64 color labels (0..6, row-major). We look for a 64-element array
// whose values follow the same equality pattern (same color -> same value,
// different color -> different value), independent of the actual encoding
// (byte/dword/short, any value range). Prints the offset + decoded grid.
void FindGemGridByPattern(const int board[64]);

// Read the live 8x8 gem grid (64 color codes, row-major board[r*8+c]) by
// dereferencing the 64 gem pointers at Board+kGridOffset and reading the type
// dword at each gem+kGemTypeOffset. Returns true and fills `board` on success.
// Quiet: no console output (safe to call every frame).
bool ReadBoard(int board[64]);

// Read the special-gem state (0 normal, 1 flame, 2 star, 4 supernova) for all
// 64 cells, row-major. Returns true on success. Quiet.
bool ReadSpecialState(int state[64]);

// Raw logical-grid read. Unlike ReadBoard this does NOT fail when a cell is
// empty — it reports emptiness, which is the "just cleared / Gemwipe window"
// signal. `ptr[i]` = gem object pointer at slot i (0 if empty/unreadable);
// `type[i]` = colour (0..6, -1 hypercube) or -100 if the slot is empty.
// Returns the number of populated slots (0..64).
int ReadBoardRaw(uint32_t ptr[64], int type[64]);

// Read, for each of the 64 grid slots, the gem object pointer, a raw window of
// 8 dwords starting at gem+0x08 (covers the logical x/y at +0x08/+0x0C and the
// candidate animated float position at +0x10/+0x14), plus the type dword at
// +0x220 and special-state dword at +0x228. Used to learn which fields move
// while a gem is falling. `ptr[i]` = 0 for an empty slot. Returns the number of
// readable slots (0..64).
int ReadGemDwords(uint32_t ptr[64], uint32_t d[64][8], int type[64],
                  int state[64]);

// --- Mersenne Twister RNG --------------------------------------------------
// The game's RNG is a standard MT19937 (31-bit output) whose state object is
// static at module-base + kMtStateRva. The 624-dword state array starts at
// +0x000; the current index (0..623) is a dword at +0x9C0. Reading these lets
// us reproduce the exact sequence used when generating falling gems.
constexpr uint32_t kMtStateRva = 0x4E20E8; // absolute 0x8E20E8
constexpr uint32_t kMtIndexOffset = 0x9C0;
constexpr int kMtStateCount = 624;

// Read the live MT index (0..623). Returns false if unreadable.
bool ReadMtIndex(int &index);

// Read the live MT19937 state (624 dwords) and index. Used to reproduce the
// exact random sequence for fallen-gem prediction.
bool ReadMtState(uint32_t state[kMtStateCount], int &index);

// Diagnostic: sample the MT index and the 8x8 gem grid for ~30s, printing a
// line whenever the board changes. Used to learn how many MT values each gem
// generation consumes and the mt-output -> color mapping. Used by --rngtrace.
void TraceRng();

// Verbose diagnostic: locate the board, print the raw gem pointer grid and the
// decoded 8x8 color grid. Used by the --dumpgrid CLI mode.
void DumpGrid();

// Diagnostic: for a board that contains exactly one special gem, find which
// dword offsets inside the gem object distinguish it from the ordinary gems.
// Prints every offset where a small number of slots deviate from the modal
// value, plus a full field dump for those slots. --dumpgem.
void DumpGemFieldOutliers();

// Verbose diagnostic: scan the board object's memory for ALL gem objects (not
// just the 64 visible ones). Each gem has its parent board pointer at +0x00
// and a valid color (0..6 or 0xFFFFFFFF) at +0x220. Incoming/falling gems (the
// pool used to refill the board) will appear with y coordinates outside 0..7.
// Used by the --scanpool CLI mode. This is the key to reading the gems that are
// about to fall during a cascade.
void ScanGemPool();

// Diagnostic: scan the board object for runs of small values (0..6) via both
// dword and byte views — a stored "next gems" sequence / bag. --scancolors.
void ScanColorArrays();

} // namespace MemoryHook
