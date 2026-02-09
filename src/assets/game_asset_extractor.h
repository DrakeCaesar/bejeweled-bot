#pragma once

#include <Windows.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace AssetExtractor {

struct ExtractRequest {
  std::string archivePath;
  std::string outputPath;
  std::string key;
};

// Get the directory where the game executable is located
std::string GetGameExecutableDirectory(HWND gameWindow);

// Extract a specific file from the game's archive (XOR 0xf7 encrypted)
// Returns true if successful
bool ExtractFile(const std::string &gameDirectory,
                 const std::string &filePathInArchive,
                 const std::string &outputPath);

// Extract multiple files from the game's archive in a single pass
// Returns the number of files successfully extracted
int ExtractFiles(const std::string &gameDirectory,
                 const std::vector<std::pair<std::string, std::string>> &files);

// Extract multiple files into memory (keyed by request key)
// Returns number of files successfully extracted into memory
int ExtractFilesToMemory(
    const std::string &gameDirectory, const std::vector<ExtractRequest> &files,
    std::unordered_map<std::string, std::vector<uint8_t>> &outData);

// Write previously extracted in-memory files to disk
// Returns number of files successfully written
int WriteExtractedFiles(
    const std::vector<ExtractRequest> &files,
    const std::unordered_map<std::string, std::vector<uint8_t>> &data);

} // namespace AssetExtractor
