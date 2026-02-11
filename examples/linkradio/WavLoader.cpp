#include "WavLoader.hpp"
#include <cstring>
#include <fstream>

namespace linkradio
{

WavData loadWav(const std::string& path)
{
  if (path.empty()) return {};

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return {};

  auto read16 = [&]() -> uint16_t {
    uint8_t buf[2];
    file.read(reinterpret_cast<char*>(buf), 2);
    return static_cast<uint16_t>(buf[0] | (buf[1] << 8));
  };
  auto read32 = [&]() -> uint32_t {
    uint8_t buf[4];
    file.read(reinterpret_cast<char*>(buf), 4);
    return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8)
           | (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
  };
  auto readTag = [&]() -> std::string {
    char buf[4];
    file.read(buf, 4);
    return std::string(buf, 4);
  };

  if (readTag() != "RIFF") return {};
  read32(); // file size
  if (readTag() != "WAVE") return {};

  WavData result;
  while (file.good())
  {
    auto chunkId = readTag();
    auto chunkSize = read32();
    if (chunkId == "fmt ")
    {
      auto format = read16();
      if (format != 1) return {}; // PCM only
      result.numChannels = read16();
      result.sampleRate = read32();
      read32(); // byte rate
      read16(); // block align
      auto bitsPerSample = read16();
      if (bitsPerSample != 16) return {};
      if (chunkSize > 16) file.seekg(chunkSize - 16, std::ios::cur);
    }
    else if (chunkId == "data")
    {
      auto numSamples = chunkSize / sizeof(int16_t);
      result.samples.resize(numSamples);
      file.read(reinterpret_cast<char*>(result.samples.data()), chunkSize);
      break;
    }
    else
    {
      file.seekg(chunkSize, std::ios::cur);
    }
  }
  return result;
}

} // namespace linkradio
