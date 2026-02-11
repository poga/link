#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace linkradio
{

struct WavData
{
  std::vector<int16_t> samples;
  uint32_t sampleRate = 0;
  uint16_t numChannels = 0;
};

WavData loadWav(const std::string& path);

} // namespace linkradio
