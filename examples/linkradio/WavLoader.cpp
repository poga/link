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
  uint16_t bitsPerSample = 0;
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
      bitsPerSample = read16();
      if (bitsPerSample != 16 && bitsPerSample != 24) return {};
      if (chunkSize > 16) file.seekg(chunkSize - 16, std::ios::cur);
    }
    else if (chunkId == "data")
    {
      uint32_t bytesPerSample = bitsPerSample / 8;
      uint32_t numChannels = result.numChannels > 0 ? result.numChannels : 1;
      auto numFrames = chunkSize / (bytesPerSample * numChannels);

      // Read raw data
      std::vector<uint8_t> raw(chunkSize);
      file.read(reinterpret_cast<char*>(raw.data()), chunkSize);

      // Convert to mono 16-bit
      result.samples.resize(numFrames);
      for (size_t i = 0; i < numFrames; ++i)
      {
        int32_t mixedSample = 0;
        for (uint32_t ch = 0; ch < numChannels; ++ch)
        {
          size_t offset = (i * numChannels + ch) * static_cast<size_t>(bytesPerSample);
          int32_t sample = 0;
          if (bitsPerSample == 16)
          {
            sample = static_cast<int16_t>(raw[offset] | (raw[offset + 1] << 8));
          }
          else // 24-bit
          {
            sample = raw[offset] | (raw[offset + 1] << 8) | (raw[offset + 2] << 16);
            if (sample & 0x800000) sample |= ~0xFFFFFF; // sign extend
            sample >>= 8; // scale 24-bit down to 16-bit range
          }
          mixedSample += sample;
        }
        result.samples[i] = static_cast<int16_t>(mixedSample / static_cast<int32_t>(numChannels));
      }
      result.numChannels = 1;
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
