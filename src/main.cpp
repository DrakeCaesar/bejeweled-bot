#include "core/application.h"
#include "config.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

// ============================================================================
// SIMD Support Detection
// ============================================================================

static void PrintSimdStatus() {
    std::cout << "=== SIMD Optimization Status ===" << std::endl;

#ifdef _MSC_VER
    std::cout << "Build: SIMD optimizations ENABLED (AVX2)" << std::endl;

    if (cv::checkHardwareSupport(CV_CPU_AVX2)) {
        std::cout << "Runtime: AVX2 instructions ARE available" << std::endl;
    } else {
        std::cout << "ERROR: AVX2 instructions NOT available (required)" << std::endl;
    }
#else
    std::cout << "Build: MSVC compiler required for AVX2 optimizations" << std::endl;
#endif

    std::cout << "===================================" << std::endl;
}

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
    // Print SIMD optimization status
    PrintSimdStatus();

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