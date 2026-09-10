// Unit tests for BoardState logic (pure, no game process needed).
//
// Build/run:  cmake --build build/dynamic-ninja-msvc-release --target bejeweled-tests
//             (or ctest --test-dir build/dynamic-ninja-msvc-release)

#include "bot/board_analysis.h"

#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Tiny test harness
// ---------------------------------------------------------------------------
namespace {
int g_pass = 0;
int g_fail = 0;
const char *g_case = "";

void Check(bool ok, const char *expr, int line) {
  if (ok) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("  FAIL [%s] line %d: %s\n", g_case, line, expr);
  }
}
} // namespace

#define CHECK(cond) Check((cond), #cond, __LINE__)

// ---------------------------------------------------------------------------
// Board construction helpers
// ---------------------------------------------------------------------------
namespace {

GemType LetterToType(char c) {
  switch (c) {
  case 'r':
    return GemType::RED;
  case 'o':
    return GemType::ORANGE;
  case 'y':
    return GemType::YELLOW;
  case 'g':
    return GemType::GREEN;
  case 'b':
    return GemType::BLUE;
  case 'p':
    return GemType::PURPLE;
  case 'w':
    return GemType::WHITE;
  case 'H':
    return GemType::HYPERCUBE;
  case '.':
    return GemType::EMPTY;
  default:
    return GemType::UNKNOWN;
  }
}

// Build a board from 8 strings of 8 letters (same alphabet as toString()).
BoardState MakeBoard(const char *const rows[8]) {
  BoardState b;
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      b.cells[r][c].type = LetterToType(rows[r][c]);
      b.cells[r][c].state = GemState::NORMAL;
      b.cells[r][c].pending = false;
    }
  }
  return b;
}

// A 3-colour Latin square: cell(r,c) = palette[(r+c)%3]. Provably has no
// 2 adjacent equal gems, so no 3-in-a-row anywhere.
BoardState LatinBoard() {
  static const char *const rows[8] = {
      "rgbrgbrg", "gbrgbrgb", "brgbrgbr", "rgbrgbrg",
      "gbrgbrgb", "brgbrgbr", "rgbrgbrg", "gbrgbrgb"};
  return MakeBoard(rows);
}

BoardState Checkerboard() {
  static const char *const rows[8] = {
      "rgrgrgrg", "grgrgrgr", "rgrgrgrg", "grgrgrgr",
      "rgrgrgrg", "grgrgrgr", "rgrgrgrg", "grgrgrgr"};
  return MakeBoard(rows);
}

int CountSpecials(const BoardState &b) {
  int n = 0;
  for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c)
      if (b.cells[r][c].state != GemState::NORMAL ||
          b.cells[r][c].type == GemType::HYPERCUBE)
        ++n;
  return n;
}

int CountType(const BoardState &b, GemType t) {
  int n = 0;
  for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c)
      if (b.cells[r][c].type == t)
        ++n;
  return n;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
namespace {

void TestCheckerboardHasNoMatch() {
  g_case = "checkerboard/no-match";
  BoardState b = Checkerboard();
  CHECK(!b.hasExistingMatch());
  b.markPendingMatches();
  CHECK(CountSpecials(b) == 0);
}

void TestLatinBoardHasNoMatch() {
  g_case = "latin/no-match";
  BoardState b = LatinBoard();
  CHECK(!b.hasExistingMatch());
}

void TestExistingMatchDetected() {
  g_case = "existing-match/detected";
  static const char *const rows[8] = {
      "yyyrgbrg", "gbrgbrgb", "brgbrgbr", "rgbrgbrg",
      "gbrgbrgb", "brgbrgbr", "rgbrgbrg", "gbrgbrgb"};
  BoardState b = MakeBoard(rows);
  CHECK(b.hasExistingMatch());

  // markPendingMatches flags exactly the 3 run cells.
  b.markPendingMatches();
  int pending = 0;
  for (int c = 0; c < 3; ++c)
    if (b.cells[0][c].pending)
      ++pending;
  CHECK(pending == 3);
  CHECK(!b.cells[0][3].pending);
}

void TestFourRunMakesOneFlame() {
  g_case = "clear/4-run-one-flame";
  BoardState b = LatinBoard();
  // row4 cols2..5 = y -> a single horizontal 4-run of a colour used nowhere
  // else, so it is the only match on the board.
  b.cells[4][2].type = GemType::YELLOW;
  b.cells[4][3].type = GemType::YELLOW;
  b.cells[4][4].type = GemType::YELLOW;
  b.cells[4][5].type = GemType::YELLOW;

  CHECK(b.hasExistingMatch());

  int cleared = b.clearMatches(nullptr, true);
  CHECK(CountSpecials(b) == 1);
  CHECK(b.cells[4][4].state == GemState::FLAME); // centre of the run
  CHECK(cleared == 3);                           // 4 gems, 1 became the flame
}

void TestShape3Plus3MakesOneStar() {
  g_case = "clear/L-shape-one-star";
  BoardState b = LatinBoard();
  // Horizontal 3 at row4 cols2..4 and vertical 3 at col4 rows4..6: an L of 5
  // gems sharing (4,4). No 4+ run, so the only special is the shape Star.
  b.cells[4][2].type = GemType::YELLOW;
  b.cells[4][3].type = GemType::YELLOW;
  b.cells[4][4].type = GemType::YELLOW;
  b.cells[5][4].type = GemType::YELLOW;
  b.cells[6][4].type = GemType::YELLOW;

  MatchableSwap swap{cv::Point(3, 4), cv::Point(4, 4)};
  b.clearMatches(&swap, true);
  CHECK(CountSpecials(b) == 1);
  CHECK(b.cells[4][4].state == GemState::STAR);
}

void TestIntersectingFourRunsMakeMultipleSpecials() {
  g_case = "clear/4+4-intersect-multiple-specials";
  BoardState b = LatinBoard();
  // Horizontal 4 at row4 cols2..5 AND vertical 4 at col5 rows4..7. They share
  // (4,5). Verified in-game that an intersection like this produces SEVERAL
  // specials (multiple flames plus a star), not one.
  b.cells[4][2].type = GemType::YELLOW;
  b.cells[4][3].type = GemType::YELLOW;
  b.cells[4][4].type = GemType::YELLOW;
  b.cells[4][5].type = GemType::YELLOW;
  b.cells[5][5].type = GemType::YELLOW;
  b.cells[6][5].type = GemType::YELLOW;
  b.cells[7][5].type = GemType::YELLOW;

  // With a swap supplied the T-shape Star is also created: a flame per 4-run
  // plus a star at the intersection. Confirmed against the live game (a 4+4
  // intersection gives exactly 2 flames + 1 star).
  MatchableSwap swap{cv::Point(4, 4), cv::Point(4, 5)};
  b.clearMatches(&swap, true);
  CHECK(CountSpecials(b) == 3);
  CHECK(b.cells[4][5].state == GemState::STAR);   // intersection
  CHECK(b.cells[4][4].state == GemState::FLAME);  // centre of horizontal run
  CHECK(b.cells[6][5].state == GemState::FLAME);  // centre of vertical run
}

void TestGravityCompacts() {
  g_case = "gravity/compacts";
  BoardState b;
  for (int r = 0; r < 8; ++r)
    for (int c = 0; c < 8; ++c)
      b.cells[r][c].type = GemType::EMPTY;
  b.cells[7][0].type = GemType::RED;
  b.cells[5][0].type = GemType::BLUE;

  CHECK(b.applyGravity());
  // Row 7 is the bottom of the board. RED already sits at the bottom and stays;
  // BLUE falls from row 5 down to row 6.
  CHECK(b.cells[7][0].type == GemType::RED);
  CHECK(b.cells[6][0].type == GemType::BLUE);
  CHECK(b.cells[5][0].type == GemType::EMPTY); // vacated
}

void TestResolveLeavesEmptyAtTop() {
  g_case = "resolve/leaves-empty-at-top";
  BoardState b = LatinBoard();
  // col0 rows5..7 = y -> a vertical 3-run at the bottom of the column.
  b.cells[5][0].type = GemType::YELLOW;
  b.cells[6][0].type = GemType::YELLOW;
  b.cells[7][0].type = GemType::YELLOW;

  int cleared = b.resolveBoard();
  CHECK(cleared >= 3);
  CHECK(b.cells[0][0].type == GemType::EMPTY); // refill gap at the top
}

void TestSimulateMoveSeededFillsRefill() {
  g_case = "simulate/seeded-fills";
  BoardState b = LatinBoard();
  // A vertical 3-run of y at col0 rows5..7 will clear; the refill then comes
  // from a full board.
  b.cells[5][0].type = GemType::YELLOW;
  b.cells[6][0].type = GemType::YELLOW;
  b.cells[7][0].type = GemType::YELLOW;

  BoardState refill = Checkerboard();
  MatchableSwap dummy{cv::Point(0, 0), cv::Point(1, 0)};

  BoardState seeded;
  b.simulateMoveSeeded(dummy, refill, seeded);
  // After seeding, no EMPTY cells should remain (refill filled the top).
  CHECK(CountType(seeded, GemType::EMPTY) == 0);
}

void TestPendingBlocksExtendingExistingMatch() {
  g_case = "pending/cannot-extend";
  static const char *const rows[8] = {
      "yyyrybrg", "gbrgbrgb", "brgbrgbr", "rgbrgbrg",
      "gbrgbrgb", "brgbrgbr", "rgbrgbrg", "gbrgbrgb"};
  BoardState b = MakeBoard(rows);
  // row0 = y y y r y ... : swapping (0,3)<->(0,4) would make y y y y, but
  // cols 0..2 are an existing (committed) match.
  MatchableSwap swap{cv::Point(3, 0), cv::Point(4, 0)};

  // Without pending flags the swap looks valid.
  CHECK(b.swapCreatesMatch(swap));

  b.markPendingMatches();
  CHECK(!b.swapCreatesMatch(swap)); // locked gems can't be extended
}

} // namespace

int main() {
  TestCheckerboardHasNoMatch();
  TestLatinBoardHasNoMatch();
  TestExistingMatchDetected();
  TestFourRunMakesOneFlame();
  TestShape3Plus3MakesOneStar();
  TestIntersectingFourRunsMakeMultipleSpecials();
  TestGravityCompacts();
  TestResolveLeavesEmptyAtTop();
  TestSimulateMoveSeededFillsRefill();
  TestPendingBlocksExtendingExistingMatch();

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
