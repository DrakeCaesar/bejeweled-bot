#pragma once

#include <string>
#include <Windows.h>

namespace AssetExtractor {

// Get the directory where the game executable is located
std::string GetGameExecutableDirectory(HWND gameWindow);

// Extract a specific file from the game's archive (XOR 0xf7 encrypted)
// Returns true if successful
bool ExtractFile(const std::string& gameDirectory, 
                 const std::string& filePathInArchive,
                 const std::string& outputPath);

}  // namespace AssetExtractor
