#include "game_asset_extractor.h"
#include <Psapi.h>
#include <filesystem>
#include <fstream>
#include <vector>

namespace AssetExtractor {

namespace {

// File entry in the 7×7M archive
struct FileEntry {
  std::string name;
  uint32_t size;
  uint64_t timestamp;
  uint32_t offset;
};

// Apply XOR 0xf7 decryption to buffer
void ApplyXor(std::vector<uint8_t> &buffer, size_t size) {
  for (size_t i = 0; i < size; i++) {
    buffer[i] ^= 0xf7;
  }
}

// Read little-endian uint32
uint32_t ReadUInt32(const std::vector<uint8_t> &buffer, size_t &pos) {
  uint32_t value = buffer[pos] | (buffer[pos + 1] << 8) |
                   (buffer[pos + 2] << 16) | (buffer[pos + 3] << 24);
  pos += 4;
  return value;
}

// Read little-endian uint64
uint64_t ReadUInt64(const std::vector<uint8_t> &buffer, size_t &pos) {
  uint64_t low = ReadUInt32(buffer, pos);
  uint64_t high = ReadUInt32(buffer, pos);
  return (high << 32) | low;
}

// Read byte
uint8_t ReadByte(const std::vector<uint8_t> &buffer, size_t &pos) {
  return buffer[pos++];
}

// Read string
std::string ReadString(const std::vector<uint8_t> &buffer, size_t &pos,
                       size_t len) {
  std::string str(len, '\0');
  for (size_t i = 0; i < len; i++) {
    str[i] = buffer[pos++];
  }
  return str;
}

// Parse the 7×7M archive and return file entries and decrypted data
bool Parse7x7MArchive(const std::string &archivePath,
                      std::vector<FileEntry> &entries,
                      std::vector<uint8_t> &decryptedData) {
  // Read entire file
  std::ifstream file(archivePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }

  size_t fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  decryptedData.resize(fileSize);
  if (!file.read(reinterpret_cast<char *>(decryptedData.data()), fileSize)) {
    return false;
  }
  file.close();

  // Apply XOR 0xf7 decryption to entire archive
  ApplyXor(decryptedData, fileSize);

  // Read header
  size_t pos = 0;
  uint32_t signature = ReadUInt32(decryptedData, pos);

  if (signature != 0xbac04ac0) {
    return false;
  }

  // First pass: read file table
  while (pos < fileSize) {
    uint8_t flags = ReadByte(decryptedData, pos);

    if (flags & 0x80) {
      // End of file table, file data starts here
      break;
    }

    uint8_t nameSize = ReadByte(decryptedData, pos);
    std::string fileName = ReadString(decryptedData, pos, nameSize);
    uint32_t size = ReadUInt32(decryptedData, pos);
    uint64_t timestamp = ReadUInt64(decryptedData, pos);

    FileEntry entry;
    entry.name = fileName;
    entry.size = size;
    entry.timestamp = timestamp;
    entry.offset = 0; // Will be set after we know where file data starts

    entries.push_back(entry);
  }

  // File data starts right after the end marker (current pos)
  uint32_t fileDataStart = static_cast<uint32_t>(pos);

  // Set offsets for all entries - they're stored sequentially
  uint32_t currentOffset = fileDataStart;
  for (size_t i = 0; i < entries.size(); i++) {
    entries[i].offset = currentOffset;
    currentOffset += entries[i].size;
  }

  return true;
}

// Extract a specific file from the decrypted archive data
bool ExtractFileFromArchive(const std::vector<uint8_t> &decryptedData,
                            const FileEntry &entry,
                            const std::string &outputPath) {
  // Validate offset and size
  if (entry.offset + entry.size > decryptedData.size()) {
    return false;
  }

  // Create output directory if needed
  std::filesystem::path outPath(outputPath);
  std::filesystem::create_directories(outPath.parent_path());

  // Write decrypted file data directly from buffer
  std::ofstream outFile(outputPath, std::ios::binary);
  if (!outFile.is_open()) {
    return false;
  }

  outFile.write(
      reinterpret_cast<const char *>(decryptedData.data() + entry.offset),
      entry.size);
  outFile.close();

  return true;
}

} // anonymous namespace

std::string GetGameExecutableDirectory(HWND gameWindow) {
  if (!gameWindow) {
    return "";
  }

  // Get process ID from window
  DWORD processId = 0;
  GetWindowThreadProcessId(gameWindow, &processId);

  if (processId == 0) {
    return "";
  }

  // Open process
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                FALSE, processId);
  if (!hProcess) {
    return "";
  }

  // Get executable path
  char exePath[MAX_PATH];
  DWORD size = GetModuleFileNameExA(hProcess, NULL, exePath, MAX_PATH);
  CloseHandle(hProcess);

  if (size == 0) {
    return "";
  }

  // Extract directory
  std::filesystem::path path(exePath);
  return path.parent_path().string();
}

bool ExtractFile(const std::string &gameDirectory,
                 const std::string &filePathInArchive,
                 const std::string &outputPath) {
  // Single file extraction using the batch method
  std::vector<std::pair<std::string, std::string>> files;
  files.emplace_back(filePathInArchive, outputPath);
  return ExtractFiles(gameDirectory, files) > 0;
}

int ExtractFiles(
    const std::string &gameDirectory,
    const std::vector<std::pair<std::string, std::string>> &files) {
  // Construct archive path
  std::string archivePath = gameDirectory + "\\main.pak.bac";

  if (!std::filesystem::exists(archivePath)) {
    return 0;
  }

  // Parse archive and decrypt all data (once for all files)
  std::vector<FileEntry> entries;
  std::vector<uint8_t> decryptedData;
  if (!Parse7x7MArchive(archivePath, entries, decryptedData)) {
    return 0;
  }

  // Extract all requested files
  int extractedCount = 0;
  for (const auto &[filePathInArchive, outputPath] : files) {
    // Find the requested file
    for (const auto &entry : entries) {
      if (entry.name == filePathInArchive) {
        if (ExtractFileFromArchive(decryptedData, entry, outputPath)) {
          extractedCount++;
        }
        break;
      }
    }
  }

  return extractedCount;
}

} // namespace AssetExtractor
