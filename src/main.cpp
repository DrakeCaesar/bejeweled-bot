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

#if defined(_MSC_VER) && defined(ENABLE_AVX2)
    std::cout << "Build: SIMD optimizations ENABLED (AVX2)" << std::endl;

    if (cv::checkHardwareSupport(CV_CPU_AVX2)) {
        std::cout << "Runtime: AVX2 instructions ARE available" << std::endl;
    } else {
        std::cout << "Runtime: AVX2 instructions NOT available (will use scalar fallback)" << std::endl;
    }
#else
    std::cout << "Build: SIMD optimizations DISABLED" << std::endl;
    #ifdef _MSC_VER
        std::cout << "Compiler: MSVC (_MSC_VER defined)" << std::endl;
    #else
        std::cout << "Compiler: NOT MSVC (no _MSC_VER)" << std::endl;
    #endif
    #ifdef ENABLE_AVX2
        std::cout << "AVX2 flag: ENABLED (ENABLE_AVX2 defined)" << std::endl;
    #else
        std::cout << "AVX2 flag: DISABLED (ENABLE_AVX2 not defined)" << std::endl;
    #endif
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