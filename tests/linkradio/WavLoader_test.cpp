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

// Generate a 24-bit WAV file from 24-bit sample values (stored as int32_t)
std::vector<uint8_t> makeTestWav24(uint32_t sampleRate, uint16_t numChannels,
                                    const std::vector<int32_t>& samples)
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
  auto write24 = [&](int32_t v) {
    wav.push_back(static_cast<uint8_t>(v & 0xFF));
    wav.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    wav.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  };

  auto dataSize = static_cast<uint32_t>(samples.size() * 3);
  uint32_t fileSize = 36 + dataSize;

  writeStr("RIFF");
  write32(fileSize);
  writeStr("WAVE");
  writeStr("fmt ");
  write32(16);
  write16(1); // PCM
  write16(numChannels);
  write32(sampleRate);
  write32(sampleRate * numChannels * 3);
  write16(static_cast<uint16_t>(numChannels * 3));
  write16(24);
  writeStr("data");
  write32(dataSize);
  for (auto s : samples)
  {
    write24(s);
  }
  return wav;
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

TEST_CASE("WavLoader | Load mono 24-bit WAV")
{
  // 24-bit values, will be shifted >>8 to 16-bit range
  std::vector<int32_t> samples = {0, 256000, -256000, 8388607, -8388608};
  auto wav = makeTestWav24(44100, 1, samples);
  writeFile("/tmp/test_mono_24.wav", wav);

  auto result = linkradio::loadWav("/tmp/test_mono_24.wav");
  REQUIRE(result.sampleRate == 44100);
  REQUIRE(result.numChannels == 1);
  REQUIRE(result.samples.size() == samples.size());
  for (size_t i = 0; i < samples.size(); ++i)
  {
    auto expected = static_cast<int16_t>(samples[i] >> 8);
    CHECK(result.samples[i] == expected);
  }
}

TEST_CASE("WavLoader | Load stereo 16-bit WAV mixes to mono")
{
  // Interleaved stereo: L, R, L, R, ...
  std::vector<int16_t> stereoSamples = {1000, 3000, -2000, -4000, 0, 10000};
  auto wav = makeTestWav(44100, 2, 16, stereoSamples);
  writeFile("/tmp/test_stereo_16.wav", wav);

  auto result = linkradio::loadWav("/tmp/test_stereo_16.wav");
  REQUIRE(result.sampleRate == 44100);
  REQUIRE(result.numChannels == 1);
  REQUIRE(result.samples.size() == 3); // 6 samples / 2 channels = 3 frames
  CHECK(result.samples[0] == 2000);    // (1000 + 3000) / 2
  CHECK(result.samples[1] == -3000);   // (-2000 + -4000) / 2
  CHECK(result.samples[2] == 5000);    // (0 + 10000) / 2
}

TEST_CASE("WavLoader | Load stereo 24-bit WAV mixes to mono")
{
  // Interleaved stereo 24-bit: L, R, L, R
  std::vector<int32_t> stereoSamples = {256000, 0, -512000, 512000};
  auto wav = makeTestWav24(44100, 2, stereoSamples);
  writeFile("/tmp/test_stereo_24.wav", wav);

  auto result = linkradio::loadWav("/tmp/test_stereo_24.wav");
  REQUIRE(result.sampleRate == 44100);
  REQUIRE(result.numChannels == 1);
  REQUIRE(result.samples.size() == 2); // 4 samples / 2 channels = 2 frames
  // Frame 0: (256000>>8 + 0>>8) / 2 = 1000/2 = 500
  CHECK(result.samples[0] == 500);
  // Frame 1: (-512000>>8 + 512000>>8) / 2 = (-2000 + 2000) / 2 = 0
  CHECK(result.samples[1] == 0);
}

TEST_CASE("WavLoader | Load real 24-bit stereo fixture")
{
  auto result = linkradio::loadWav(LINKRADIO_TEST_FIXTURES "/short_24bit_stereo.wav");
  REQUIRE(result.sampleRate == 44100);
  REQUIRE(result.numChannels == 1);          // mixed down to mono
  REQUIRE(result.samples.size() == 4410);    // 0.1s * 44100 Hz
  // Verify samples are non-trivial (not all zeros)
  bool hasNonZero = false;
  for (auto s : result.samples)
  {
    if (s != 0) { hasNonZero = true; break; }
  }
  CHECK(hasNonZero);
}
