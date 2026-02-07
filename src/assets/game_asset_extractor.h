#pragma once

#include <string>
#include <vector>
#include <Windows.h>

namespace AssetExtractor {

// Get the directory where the game executable is located
std::string GetGameExecutableDirectory(HWND gameWindow);

// Extract a specific file from the game's archive (XOR 0xf7 encrypted)
// Returns true if successful
bool ExtractFile(const std::string& gameDirectory,
                 const std::string& filePathInArchive,
                 const std::string& outputPath);

// Extract multiple files from the game's archive in a single pass
// Returns the number of files successfully extracted
int ExtractFiles(const std::string& gameDirectory,
                 const std::vector<std::pair<std::string, std::string>>& files);

}  // namespace AssetExtractor
