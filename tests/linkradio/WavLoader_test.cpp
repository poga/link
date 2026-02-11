#include "WavLoader.hpp"
#include <catch.hpp>
#include <cmath>
#include <fstream>

namespace
{

// Generate a minimal valid WAV file in memory
std::vector<uint8_t> makeTestWav(uint32_t sampleRate, uint16_t numChannels,
                                  uint16_t bitsPerSample, const std::vector<int16_t>& samples)
{
  std::vector<uint8_t> wav;
  auto write16 = [&](uint16_t v) {
    wav.push_back(static_cast<uint8_t>(v & 0xFF));
    wav.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  };
  auto write32 = [&](uint32_t v) {
    wav.push_back(static_cast<uint8_t>(v & 0xFF));
    wav.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    wav.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    wav.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  auto writeStr = [&](const char* s) {
    for (int i = 0; i < 4; ++i) wav.push_back(static_cast<uint8_t>(s[i]));
  };

  auto dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
  uint32_t fileSize = 36 + dataSize;

  writeStr("RIFF");
  write32(fileSize);
  writeStr("WAVE");
  writeStr("fmt ");
  write32(16); // chunk size
  write16(1);  // PCM
  write16(numChannels);
  write32(sampleRate);
  write32(sampleRate * numChannels * bitsPerSample / 8);
  write16(static_cast<uint16_t>(numChannels * bitsPerSample / 8));
  write16(bitsPerSample);
  writeStr("data");
  write32(dataSize);
  for (auto s : samples)
  {
    write16(static_cast<uint16_t>(s));
  }
  return wav;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data)
{
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(data.data()),
          static_cast<std::streamsize>(data.size()));
}

} // namespace

TEST_CASE("WavLoader | Load mono 16-bit WAV")
{
  std::vector<int16_t> samples = {0, 1000, -1000, 32767, -32768};
  auto wav = makeTestWav(44100, 1, 16, samples);
  writeFile("/tmp/test_mono.wav", wav);

  auto result = linkradio::loadWav("/tmp/test_mono.wav");
  REQUIRE(result.sampleRate == 44100);
  REQUIRE(result.numChannels == 1);
  REQUIRE(result.samples.size() == samples.size());
  for (size_t i = 0; i < samples.size(); ++i)
  {
    CHECK(result.samples[i] == samples[i]);
  }
}

TEST_CASE("WavLoader | Empty path returns empty samples")
{
  auto result = linkradio::loadWav("");
  CHECK(result.samples.empty());
  CHECK(result.sampleRate == 0);
}

TEST_CASE("WavLoader | Missing file returns empty samples")
{
  auto result = linkradio::loadWav("/tmp/nonexistent_file_12345.wav");
  CHECK(result.samples.empty());
}
