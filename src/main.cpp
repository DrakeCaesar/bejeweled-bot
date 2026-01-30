#include "core/application.h"
#include "types/timing.h"
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// ============================================================================
// RAII wrapper for high-resolution timer
// ============================================================================

struct TimerResolution {
  explicit TimerResolution(UINT periodMs) : period(periodMs) {
    timeBeginPeriod(period);
  }
  ~TimerResolution() { timeEndPeriod(period); }
  UINT period;
};

// ============================================================================
// Main Entry Point
// ============================================================================

int main() {
  // Enable high-resolution timing
  TimerResolution timerResolution(1);

  // Create and run application
  Application app;

  if (!app.Initialize()) {
    return -1;
  }

  app.Run();

  return 0;
}
