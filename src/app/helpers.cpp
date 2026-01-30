#include "helpers.h"

// Keep the game window active by sending activation messages
void KeepGameActive(HWND gameWindow)
{
  // Send WM_ACTIVATEAPP to make the game think it's active
  SendMessage(gameWindow, WM_ACTIVATEAPP, TRUE, 0);

  // Send WM_ACTIVATE with WA_ACTIVE
  SendMessage(gameWindow, WM_ACTIVATE, WA_ACTIVE, 0);

  // Optional: Send a fake mouse move to show "activity"
  POINT pt;
  GetCursorPos(&pt);
  ScreenToClient(gameWindow, &pt);
  SendMessage(gameWindow, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
}

// Set window to always on top
void SetWindowAlwaysOnTop(HWND hwnd, bool onTop)
{
  SetWindowPos(hwnd,
               onTop ? HWND_TOPMOST : HWND_NOTOPMOST,
               0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

TimerResolution::TimerResolution(UINT periodMs)
    : period(periodMs)
{
  timeBeginPeriod(period);
}

TimerResolution::~TimerResolution()
{
  timeEndPeriod(period);
}
