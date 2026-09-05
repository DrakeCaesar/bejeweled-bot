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

// --- Attach / detach --------------------------------------------------------
bool Attach(HWND hwnd);
void Detach();
bool IsAttached();

// --- Raw reads (32-bit target) ---------------------------------------------
bool Read(uint32_t address, void *buffer, size_t size);
bool ReadU32(uint32_t address, uint32_t &out);
bool ReadPtr(uint32_t address, uint32_t &out);

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

// Verbose diagnostic: locate the board, print the raw gem pointer grid and the
// decoded 8x8 color grid. Used by the --dumpgrid CLI mode.
void DumpGrid();

} // namespace MemoryHook
