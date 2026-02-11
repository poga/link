# LinkRadio Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build an always-on CLI daemon that fetches a loop manifest from an HTTP server and broadcasts 4 multitrack audio channels (drums, bass, harmony, melody) over LinkAudio, with UTC-based global sync.

**Architecture:** A new example app `linkradio` built alongside `linkaudiohut`. It reuses the same audio platform layer but replaces the metronome engine with a loop player that reads WAV stems and feeds them into 4 `LinkAudioSink` channels. A manifest fetcher thread polls an HTTP server every 60s. A scheduler maps UTC time to the current section/loop.

**Tech Stack:** C++17, ASIO (HTTP + networking, already a dependency), nlohmann/json (header-only, new dependency), WAV parsing (minimal custom code — WAV headers are trivial). Built with CMake.

**Design doc:** `docs/plans/2026-02-11-linkradio-design.md`

---

### Task 1: Add nlohmann/json dependency

**Files:**
- Create: `modules/json/include/nlohmann/json.hpp` (single-header download)
- Modify: `cmake_include/ConfigureJson.cmake` (new file)
- Modify: `CMakeLists.txt` (root, add include)

**Step 1: Download nlohmann/json single header**

```bash
mkdir -p modules/json/include/nlohmann
curl -L -o modules/json/include/nlohmann/json.hpp \
  https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
```

**Step 2: Create CMake config**

Create `cmake_include/ConfigureJson.cmake`:
```cmake
add_library(Json IMPORTED INTERFACE)
set_target_properties(Json PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/modules/json/include"
)
```

**Step 3: Include in root CMakeLists.txt**

Add after the ASIO include line in `CMakeLists.txt`:
```cmake
include(cmake_include/ConfigureJson.cmake)
```

**Step 4: Verify it compiles**

```bash
cd build && cmake .. && make -j4
```
Expected: Build succeeds (no targets use it yet).

**Step 5: Commit**

```bash
git add modules/json cmake_include/ConfigureJson.cmake CMakeLists.txt
git commit -m "Add nlohmann/json header-only dependency for LinkRadio"
```

---

### Task 2: WAV file loader

**Files:**
- Create: `examples/linkradio/WavLoader.hpp`
- Create: `examples/linkradio/WavLoader.cpp`
- Create: `tests/linkradio/WavLoader_test.cpp`

**Step 1: Write the failing test**

Create `tests/linkradio/WavLoader_test.cpp`:
```cpp
#include "WavLoader.hpp"
#include <catch/catch.hpp>
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
    wav.push_back(v & 0xFF);
    wav.push_back((v >> 8) & 0xFF);
  };
  auto write32 = [&](uint32_t v) {
    wav.push_back(v & 0xFF);
    wav.push_back((v >> 8) & 0xFF);
    wav.push_back((v >> 16) & 0xFF);
    wav.push_back((v >> 24) & 0xFF);
  };
  auto writeStr = [&](const char* s) {
    for (int i = 0; i < 4; ++i) wav.push_back(s[i]);
  };

  uint32_t dataSize = samples.size() * sizeof(int16_t);
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
  write16(numChannels * bitsPerSample / 8);
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
  f.write(reinterpret_cast<const char*>(data.data()), data.size());
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
```

**Step 2: Write the implementation**

Create `examples/linkradio/WavLoader.hpp`:
```cpp
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
```

Create `examples/linkradio/WavLoader.cpp`:
```cpp
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
    return buf[0] | (buf[1] << 8);
  };
  auto read32 = [&]() -> uint32_t {
    uint8_t buf[4];
    file.read(reinterpret_cast<char*>(buf), 4);
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
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
```

**Step 3: Add to CMake and run test**

Add a test target in `src/CMakeLists.txt` (follow existing test pattern):
```cmake
# In the test section
if(LINK_BUILD_TESTS)
  add_executable(LinkRadioTest
    ${CMAKE_SOURCE_DIR}/tests/linkradio/WavLoader_test.cpp
    ${CMAKE_SOURCE_DIR}/examples/linkradio/WavLoader.cpp
  )
  target_include_directories(LinkRadioTest PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/linkradio
  )
  target_link_libraries(LinkRadioTest Catch)
  add_test(NAME LinkRadioTest COMMAND LinkRadioTest)
endif()
```

```bash
cd build && cmake .. && make LinkRadioTest && ./bin/LinkRadioTest
```
Expected: All 3 tests pass.

**Step 4: Commit**

```bash
git add examples/linkradio/WavLoader.hpp examples/linkradio/WavLoader.cpp \
        tests/linkradio/WavLoader_test.cpp src/CMakeLists.txt
git commit -m "Add WAV file loader for LinkRadio"
```

---

### Task 3: Manifest parser

**Files:**
- Create: `examples/linkradio/Manifest.hpp`
- Create: `examples/linkradio/Manifest.cpp`
- Create: `tests/linkradio/Manifest_test.cpp`

**Step 1: Write the failing test**

Create `tests/linkradio/Manifest_test.cpp`:
```cpp
#include "Manifest.hpp"
#include <catch/catch.hpp>

TEST_CASE("Manifest | Parse valid manifest")
{
  std::string json = R"({
    "sections": [
      {
        "start_time": "2026-02-11T00:00:00Z",
        "end_time": "2026-02-11T01:00:00Z",
        "loop_duration_seconds": 120,
        "loops": [
          {
            "id": "funk_01",
            "bpm": 110.0,
            "beats": 16,
            "stems": {
              "drums": "loops/funk_01/drums.wav",
              "bass": "loops/funk_01/bass.wav",
              "harmony": "loops/funk_01/harmony.wav",
              "melody": ""
            }
          }
        ]
      }
    ]
  })";

  auto manifest = linkradio::parseManifest(json);
  REQUIRE(manifest.sections.size() == 1);
  auto& section = manifest.sections[0];
  REQUIRE(section.loops.size() == 1);
  CHECK(section.loopDurationSeconds == 120);
  auto& loop = section.loops[0];
  CHECK(loop.id == "funk_01");
  CHECK(loop.bpm == Approx(110.0));
  CHECK(loop.beats == 16);
  CHECK(loop.stems.drums == "loops/funk_01/drums.wav");
  CHECK(loop.stems.bass == "loops/funk_01/bass.wav");
  CHECK(loop.stems.harmony == "loops/funk_01/harmony.wav");
  CHECK(loop.stems.melody == "");
}

TEST_CASE("Manifest | Invalid JSON returns empty manifest")
{
  auto manifest = linkradio::parseManifest("not json");
  CHECK(manifest.sections.empty());
}
```

**Step 2: Write the implementation**

Create `examples/linkradio/Manifest.hpp`:
```cpp
#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace linkradio
{

struct Stems
{
  std::string drums;
  std::string bass;
  std::string harmony;
  std::string melody;
};

struct Loop
{
  std::string id;
  double bpm = 120.0;
  int beats = 16;
  Stems stems;
};

struct Section
{
  std::chrono::system_clock::time_point startTime;
  std::chrono::system_clock::time_point endTime;
  int loopDurationSeconds = 120;
  std::vector<Loop> loops;
};

struct Manifest
{
  std::vector<Section> sections;
};

Manifest parseManifest(const std::string& json);

} // namespace linkradio
```

Create `examples/linkradio/Manifest.cpp`:
```cpp
#include "Manifest.hpp"
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>

namespace linkradio
{

namespace
{

std::chrono::system_clock::time_point parseIso8601(const std::string& str)
{
  std::tm tm = {};
  std::istringstream ss(str);
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (ss.fail()) return {};
  return std::chrono::system_clock::from_time_t(timegm(&tm));
}

} // namespace

Manifest parseManifest(const std::string& json)
{
  Manifest manifest;
  try
  {
    auto j = nlohmann::json::parse(json);
    for (auto& sectionJson : j["sections"])
    {
      Section section;
      section.startTime = parseIso8601(sectionJson["start_time"]);
      section.endTime = parseIso8601(sectionJson["end_time"]);
      section.loopDurationSeconds = sectionJson["loop_duration_seconds"];
      for (auto& loopJson : sectionJson["loops"])
      {
        Loop loop;
        loop.id = loopJson["id"];
        loop.bpm = loopJson["bpm"];
        loop.beats = loopJson["beats"];
        auto& s = loopJson["stems"];
        loop.stems.drums = s.value("drums", "");
        loop.stems.bass = s.value("bass", "");
        loop.stems.harmony = s.value("harmony", "");
        loop.stems.melody = s.value("melody", "");
        section.loops.push_back(std::move(loop));
      }
      manifest.sections.push_back(std::move(section));
    }
  }
  catch (...)
  {
  }
  return manifest;
}

} // namespace linkradio
```

**Step 3: Add test to CMake, run**

Update the `LinkRadioTest` target in `src/CMakeLists.txt` to add:
```cmake
add_executable(LinkRadioTest
  ${CMAKE_SOURCE_DIR}/tests/linkradio/WavLoader_test.cpp
  ${CMAKE_SOURCE_DIR}/tests/linkradio/Manifest_test.cpp
  ${CMAKE_SOURCE_DIR}/examples/linkradio/WavLoader.cpp
  ${CMAKE_SOURCE_DIR}/examples/linkradio/Manifest.cpp
)
target_include_directories(LinkRadioTest PRIVATE
  ${CMAKE_SOURCE_DIR}/examples/linkradio
)
target_link_libraries(LinkRadioTest Catch Json)
```

```bash
cd build && cmake .. && make LinkRadioTest && ./bin/LinkRadioTest
```
Expected: All 5 tests pass.

**Step 4: Commit**

```bash
git add examples/linkradio/Manifest.hpp examples/linkradio/Manifest.cpp \
        tests/linkradio/Manifest_test.cpp src/CMakeLists.txt
git commit -m "Add manifest parser for LinkRadio"
```

---

### Task 4: Scheduler (UTC-based loop selection)

**Files:**
- Create: `examples/linkradio/Scheduler.hpp`
- Create: `tests/linkradio/Scheduler_test.cpp`

**Step 1: Write the failing test**

Create `tests/linkradio/Scheduler_test.cpp`:
```cpp
#include "Scheduler.hpp"
#include <catch/catch.hpp>

TEST_CASE("Scheduler | Returns correct section and loop index")
{
  linkradio::Manifest manifest;
  linkradio::Section section;
  section.startTime = std::chrono::system_clock::from_time_t(0);   // epoch
  section.endTime = std::chrono::system_clock::from_time_t(3600);  // +1hr
  section.loopDurationSeconds = 60;

  linkradio::Loop loop1; loop1.id = "a";
  linkradio::Loop loop2; loop2.id = "b";
  section.loops = {loop1, loop2};
  manifest.sections = {section};

  // At t=0, loop_index = 0 % 2 = 0
  auto now = std::chrono::system_clock::from_time_t(0);
  auto result = linkradio::schedule(manifest, now);
  REQUIRE(result.has_value());
  CHECK(result->sectionIndex == 0);
  CHECK(result->loopIndex == 0);

  // At t=60, loop_index = 1 % 2 = 1
  now = std::chrono::system_clock::from_time_t(60);
  result = linkradio::schedule(manifest, now);
  REQUIRE(result.has_value());
  CHECK(result->loopIndex == 1);

  // At t=120, loop_index = 2 % 2 = 0 (wraps)
  now = std::chrono::system_clock::from_time_t(120);
  result = linkradio::schedule(manifest, now);
  REQUIRE(result.has_value());
  CHECK(result->loopIndex == 0);
}

TEST_CASE("Scheduler | Returns nullopt when no section matches")
{
  linkradio::Manifest manifest;
  auto now = std::chrono::system_clock::now();
  auto result = linkradio::schedule(manifest, now);
  CHECK(!result.has_value());
}
```

**Step 2: Write the implementation**

Create `examples/linkradio/Scheduler.hpp`:
```cpp
#pragma once

#include "Manifest.hpp"
#include <optional>

namespace linkradio
{

struct ScheduleResult
{
  size_t sectionIndex;
  size_t loopIndex;
};

inline std::optional<ScheduleResult> schedule(
  const Manifest& manifest,
  std::chrono::system_clock::time_point now)
{
  for (size_t i = 0; i < manifest.sections.size(); ++i)
  {
    auto& section = manifest.sections[i];
    if (now >= section.startTime && now < section.endTime && !section.loops.empty())
    {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - section.startTime).count();
      auto loopIndex = (elapsed / section.loopDurationSeconds)
                        % static_cast<long>(section.loops.size());
      return ScheduleResult{i, static_cast<size_t>(loopIndex)};
    }
  }
  return std::nullopt;
}

} // namespace linkradio
```

**Step 3: Add test, run**

Add `Scheduler_test.cpp` to `LinkRadioTest` sources in `src/CMakeLists.txt`.

```bash
cd build && cmake .. && make LinkRadioTest && ./bin/LinkRadioTest
```
Expected: All 7 tests pass.

**Step 4: Commit**

```bash
git add examples/linkradio/Scheduler.hpp tests/linkradio/Scheduler_test.cpp src/CMakeLists.txt
git commit -m "Add UTC-based scheduler for LinkRadio"
```

---

### Task 5: HTTP manifest fetcher

**Files:**
- Create: `examples/linkradio/Fetcher.hpp`
- Create: `examples/linkradio/Fetcher.cpp`

This uses ASIO's TCP socket to make HTTP GET requests. No test for this task (network I/O) — we test it via integration.

**Step 1: Write the implementation**

Create `examples/linkradio/Fetcher.hpp`:
```cpp
#pragma once

#include <string>
#include <vector>

namespace linkradio
{

// Fetch text content from an HTTP URL (http:// only, no TLS).
// Returns empty string on failure.
std::string httpGet(const std::string& host, const std::string& path, int port = 80);

// Download binary content to a local file path.
// Returns true on success.
bool httpDownload(const std::string& host, const std::string& path,
                  const std::string& localPath, int port = 80);

} // namespace linkradio
```

Create `examples/linkradio/Fetcher.cpp`:
```cpp
#include "Fetcher.hpp"
#include <asio.hpp>
#include <fstream>
#include <sstream>

namespace linkradio
{

std::string httpGet(const std::string& host, const std::string& path, int port)
{
  try
  {
    asio::io_context io;
    asio::ip::tcp::resolver resolver(io);
    asio::ip::tcp::socket socket(io);

    auto endpoints = resolver.resolve(host, std::to_string(port));
    asio::connect(socket, endpoints);

    std::string request = "GET " + path + " HTTP/1.0\r\n"
                          "Host: " + host + "\r\n"
                          "Connection: close\r\n\r\n";
    asio::write(socket, asio::buffer(request));

    std::string response;
    asio::error_code ec;
    asio::streambuf buf;
    while (asio::read(socket, buf, ec))
    {
    }
    std::ostringstream ss;
    ss << &buf;
    response = ss.str();

    // Strip HTTP headers
    auto headerEnd = response.find("\r\n\r\n");
    if (headerEnd != std::string::npos)
    {
      return response.substr(headerEnd + 4);
    }
    return {};
  }
  catch (...)
  {
    return {};
  }
}

bool httpDownload(const std::string& host, const std::string& path,
                  const std::string& localPath, int port)
{
  try
  {
    asio::io_context io;
    asio::ip::tcp::resolver resolver(io);
    asio::ip::tcp::socket socket(io);

    auto endpoints = resolver.resolve(host, std::to_string(port));
    asio::connect(socket, endpoints);

    std::string request = "GET " + path + " HTTP/1.0\r\n"
                          "Host: " + host + "\r\n"
                          "Connection: close\r\n\r\n";
    asio::write(socket, asio::buffer(request));

    // Read entire response
    asio::error_code ec;
    asio::streambuf buf;
    while (asio::read(socket, buf, ec))
    {
    }
    std::string response;
    {
      std::ostringstream ss;
      ss << &buf;
      response = ss.str();
    }

    auto headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    std::ofstream file(localPath, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(response.data() + headerEnd + 4,
               response.size() - headerEnd - 4);
    return true;
  }
  catch (...)
  {
    return false;
  }
}

} // namespace linkradio
```

**Step 2: Verify it compiles**

```bash
cd build && cmake .. && make -j4
```
Expected: Compiles without errors.

**Step 3: Commit**

```bash
git add examples/linkradio/Fetcher.hpp examples/linkradio/Fetcher.cpp
git commit -m "Add HTTP fetcher for LinkRadio manifest and stems"
```

---

### Task 6: StemStore (in-memory stem buffer management)

**Files:**
- Create: `examples/linkradio/StemStore.hpp`
- Create: `tests/linkradio/StemStore_test.cpp`

**Step 1: Write the failing test**

Create `tests/linkradio/StemStore_test.cpp`:
```cpp
#include "StemStore.hpp"
#include <catch/catch.hpp>

TEST_CASE("StemStore | Store and retrieve stems")
{
  linkradio::StemStore store;
  std::vector<int16_t> samples = {100, 200, 300};
  store.put("funk_01/drums", samples, 44100);

  auto* data = store.get("funk_01/drums");
  REQUIRE(data != nullptr);
  CHECK(data->samples == samples);
  CHECK(data->sampleRate == 44100);
}

TEST_CASE("StemStore | Get missing returns nullptr")
{
  linkradio::StemStore store;
  CHECK(store.get("missing") == nullptr);
}

TEST_CASE("StemStore | Clear removes all entries")
{
  linkradio::StemStore store;
  store.put("a", {1, 2}, 44100);
  store.clear();
  CHECK(store.get("a") == nullptr);
}
```

**Step 2: Write the implementation**

Create `examples/linkradio/StemStore.hpp`:
```cpp
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace linkradio
{

struct StemData
{
  std::vector<int16_t> samples;
  uint32_t sampleRate;
};

class StemStore
{
public:
  void put(const std::string& key, const std::vector<int16_t>& samples, uint32_t sampleRate)
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mStems[key] = {samples, sampleRate};
  }

  const StemData* get(const std::string& key) const
  {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mStems.find(key);
    return it != mStems.end() ? &it->second : nullptr;
  }

  bool has(const std::string& key) const
  {
    std::lock_guard<std::mutex> lock(mMutex);
    return mStems.count(key) > 0;
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mStems.clear();
  }

  void erase(const std::string& key)
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mStems.erase(key);
  }

private:
  mutable std::mutex mMutex;
  std::unordered_map<std::string, StemData> mStems;
};

} // namespace linkradio
```

**Step 3: Add test, run**

Add `StemStore_test.cpp` to `LinkRadioTest` sources in `src/CMakeLists.txt`.

```bash
cd build && cmake .. && make LinkRadioTest && ./bin/LinkRadioTest
```
Expected: All 10 tests pass.

**Step 4: Commit**

```bash
git add examples/linkradio/StemStore.hpp tests/linkradio/StemStore_test.cpp src/CMakeLists.txt
git commit -m "Add in-memory stem store for LinkRadio"
```

---

### Task 7: LoopPlayer (feeds stems into 4 LinkAudioSinks)

**Files:**
- Create: `examples/linkradio/LoopPlayer.hpp`
- Create: `examples/linkradio/LoopPlayer.ipp`

This is the audio-thread component. It replaces the metronome in `AudioEngine`. Each of the 4 sinks (drums, bass, harmony, melody) gets fed PCM samples from the current loop's stems, looping back to the start when reaching the end.

**Step 1: Write the implementation**

Create `examples/linkradio/LoopPlayer.hpp`:
```cpp
#pragma once

#if defined(LINK_AUDIO)

#include "Manifest.hpp"
#include "StemStore.hpp"
#include <ableton/LinkAudio.hpp>
#include <array>
#include <string>

namespace linkradio
{

static const std::array<std::string, 4> kStemNames = {"drums", "bass", "harmony", "melody"};

template <typename Link>
class LoopPlayer
{
public:
  LoopPlayer(Link& link, double& sampleRate);

  void setCurrentLoop(const Loop& loop);

  void operator()(size_t numFrames,
                  typename Link::SessionState sessionState,
                  double sampleRate,
                  std::chrono::microseconds hostTime,
                  double quantum);

  void setStemStore(StemStore* store) { mpStemStore = store; }

private:
  Link& mLink;
  double& mSampleRate;
  std::array<LinkAudioSink, 4> mSinks;
  Loop mCurrentLoop;
  std::array<size_t, 4> mPlayPositions = {};
  StemStore* mpStemStore = nullptr;
};

} // namespace linkradio

#include "LoopPlayer.ipp"

#endif // LINK_AUDIO
```

Create `examples/linkradio/LoopPlayer.ipp`:
```cpp
#include <algorithm>

namespace linkradio
{

template <typename Link>
LoopPlayer<Link>::LoopPlayer(Link& link, double& sampleRate)
  : mLink(link)
  , mSampleRate(sampleRate)
  , mSinks{
      LinkAudioSink(mLink, "drums", 4096),
      LinkAudioSink(mLink, "bass", 4096),
      LinkAudioSink(mLink, "harmony", 4096),
      LinkAudioSink(mLink, "melody", 4096)}
{
}

template <typename Link>
void LoopPlayer<Link>::setCurrentLoop(const Loop& loop)
{
  mCurrentLoop = loop;
  mPlayPositions = {};
}

template <typename Link>
void LoopPlayer<Link>::operator()(size_t numFrames,
                                   typename Link::SessionState sessionState,
                                   double sampleRate,
                                   std::chrono::microseconds hostTime,
                                   double quantum)
{
  if (!mpStemStore) return;

  const std::array<std::string, 4> stemPaths = {
    mCurrentLoop.stems.drums,
    mCurrentLoop.stems.bass,
    mCurrentLoop.stems.harmony,
    mCurrentLoop.stems.melody,
  };

  const auto beatsAtBufferBegin = sessionState.beatAtTime(hostTime, quantum);

  for (size_t ch = 0; ch < 4; ++ch)
  {
    auto buffer = LinkAudioSink::BufferHandle(mSinks[ch]);
    if (!buffer) continue;

    const StemData* stem = nullptr;
    if (!stemPaths[ch].empty())
    {
      stem = mpStemStore->get(stemPaths[ch]);
    }

    if (stem && !stem->samples.empty())
    {
      for (size_t i = 0; i < numFrames; ++i)
      {
        buffer.samples[i] = stem->samples[mPlayPositions[ch]];
        mPlayPositions[ch] = (mPlayPositions[ch] + 1) % stem->samples.size();
      }
    }
    else
    {
      std::fill_n(buffer.samples, numFrames, int16_t{0});
    }

    buffer.commit(sessionState,
                  beatsAtBufferBegin,
                  quantum,
                  static_cast<uint32_t>(numFrames),
                  1, // mono
                  static_cast<uint32_t>(sampleRate));
  }
}

} // namespace linkradio
```

**Step 2: Verify it compiles**

(Will be compiled as part of the main app in Task 8.)

**Step 3: Commit**

```bash
git add examples/linkradio/LoopPlayer.hpp examples/linkradio/LoopPlayer.ipp
git commit -m "Add LoopPlayer: feeds 4 stems into LinkAudioSinks"
```

---

### Task 8: Main app (linkradio daemon)

**Files:**
- Create: `examples/linkradio/main.cpp`
- Modify: `examples/CMakeLists.txt` (add LinkRadio target)

**Step 1: Write main.cpp**

Create `examples/linkradio/main.cpp`:
```cpp
#include "Fetcher.hpp"
#include "LoopPlayer.hpp"
#include "Manifest.hpp"
#include "Scheduler.hpp"
#include "StemStore.hpp"
#include "WavLoader.hpp"

#include <ableton/LinkAudio.hpp>
#include <ableton/link/HostTimeFilter.hpp>
#include <ableton/platforms/Config.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace
{

struct Config
{
  std::string serverHost;
  std::string manifestPath;
  int serverPort = 80;
};

Config parseArgs(int argc, char* argv[])
{
  Config config;
  if (argc < 3)
  {
    std::cerr << "Usage: linkradio <host> <manifest-path> [port]\n";
    std::cerr << "  Example: linkradio example.com /manifest.json 8080\n";
    std::exit(1);
  }
  config.serverHost = argv[1];
  config.manifestPath = argv[2];
  if (argc > 3) config.serverPort = std::atoi(argv[3]);
  return config;
}

void ensureStems(const linkradio::Manifest& manifest,
                 size_t startSection, size_t count,
                 linkradio::StemStore& store,
                 const Config& config)
{
  for (size_t i = startSection; i < startSection + count && i < manifest.sections.size(); ++i)
  {
    for (auto& loop : manifest.sections[i].loops)
    {
      for (auto& path : {loop.stems.drums, loop.stems.bass,
                         loop.stems.harmony, loop.stems.melody})
      {
        if (!path.empty() && !store.has(path))
        {
          std::cout << "Downloading: " << path << std::endl;
          auto body = linkradio::httpGet(config.serverHost,
                                          "/" + path, config.serverPort);
          if (!body.empty())
          {
            // Parse WAV from downloaded bytes
            // Write to temp file, then load
            std::string tmpPath = "/tmp/linkradio_" +
              std::to_string(std::hash<std::string>{}(path)) + ".wav";
            linkradio::httpDownload(config.serverHost,
                                    "/" + path, tmpPath, config.serverPort);
            auto wav = linkradio::loadWav(tmpPath);
            if (!wav.samples.empty())
            {
              store.put(path, wav.samples, wav.sampleRate);
            }
          }
        }
      }
    }
  }
}

} // namespace

int main(int argc, char* argv[])
{
  auto config = parseArgs(argc, argv);

  ableton::LinkAudio link(120.0, "LinkRadio");
  link.enable(true);
  link.enableLinkAudio(true);
  link.enableStartStopSync(true);

  linkradio::StemStore stemStore;
  linkradio::LoopPlayer<ableton::LinkAudio> loopPlayer(link, /* sampleRate placeholder */);
  loopPlayer.setStemStore(&stemStore);

  // Fetch initial manifest
  std::cout << "Fetching manifest from " << config.serverHost
            << config.manifestPath << std::endl;
  auto manifestJson = linkradio::httpGet(config.serverHost,
                                          config.manifestPath,
                                          config.serverPort);
  auto manifest = linkradio::parseManifest(manifestJson);
  if (manifest.sections.empty())
  {
    std::cerr << "Error: empty or invalid manifest\n";
    return 1;
  }
  std::cout << "Loaded " << manifest.sections.size() << " sections\n";

  // Download stems for current + next 2 sections
  auto now = std::chrono::system_clock::now();
  auto scheduleResult = linkradio::schedule(manifest, now);
  if (!scheduleResult)
  {
    std::cerr << "Error: no section matches current time\n";
    return 1;
  }
  ensureStems(manifest, scheduleResult->sectionIndex, 3, stemStore, config);

  // Set initial loop
  auto& currentLoop = manifest.sections[scheduleResult->sectionIndex]
                               .loops[scheduleResult->loopIndex];
  loopPlayer.setCurrentLoop(currentLoop);

  // Set tempo
  {
    auto sessionState = link.captureAppSessionState();
    sessionState.setTempo(currentLoop.bpm, link.clock().micros());
    sessionState.setIsPlaying(true, link.clock().micros());
    link.commitAppSessionState(sessionState);
  }

  std::cout << "Playing: " << currentLoop.id
            << " at " << currentLoop.bpm << " BPM\n";

  // Main loop
  std::atomic<bool> running{true};
  auto lastFetch = std::chrono::steady_clock::now();
  std::string lastLoopId = currentLoop.id;

  // Note: The audio callback integration with the platform audio engine
  // needs to call loopPlayer() from the audio thread. This main loop
  // handles manifest fetching and loop switching.

  while (running)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Refetch manifest every 60 seconds
    auto elapsed = std::chrono::steady_clock::now() - lastFetch;
    if (elapsed > std::chrono::seconds(60))
    {
      auto json = linkradio::httpGet(config.serverHost,
                                      config.manifestPath,
                                      config.serverPort);
      if (!json.empty())
      {
        auto newManifest = linkradio::parseManifest(json);
        if (!newManifest.sections.empty())
        {
          manifest = std::move(newManifest);
        }
      }
      lastFetch = std::chrono::steady_clock::now();
    }

    // Check if loop has changed
    now = std::chrono::system_clock::now();
    auto result = linkradio::schedule(manifest, now);
    if (result)
    {
      ensureStems(manifest, result->sectionIndex, 3, stemStore, config);
      auto& loop = manifest.sections[result->sectionIndex]
                           .loops[result->loopIndex];
      if (loop.id != lastLoopId)
      {
        loopPlayer.setCurrentLoop(loop);
        auto sessionState = link.captureAppSessionState();
        sessionState.setTempo(loop.bpm, link.clock().micros());
        link.commitAppSessionState(sessionState);
        std::cout << "Switched to: " << loop.id
                  << " at " << loop.bpm << " BPM\n";
        lastLoopId = loop.id;
      }
    }
  }

  return 0;
}
```

**Step 2: Add CMake target**

Add to `examples/CMakeLists.txt` after the `LinkAudioHut` target:
```cmake
if(LINK_BUILD_AUDIO)
  add_executable(LinkRadio
    linkradio/main.cpp
    linkradio/WavLoader.cpp
    linkradio/Manifest.cpp
    linkradio/Fetcher.cpp
    ${linkhut_audio_SOURCES}
  )
  target_include_directories(LinkRadio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/linkradio
    ${CMAKE_CURRENT_SOURCE_DIR}/linkaudio
  )
  target_link_libraries(LinkRadio Ableton::Link Json)
  target_compile_definitions(LinkRadio PRIVATE LINK_AUDIO=1)
endif()
```

**Step 3: Verify it compiles**

```bash
cd build && cmake .. && make LinkRadio -j4
```
Expected: Compiles. (Won't fully work yet — audio callback integration needed in Task 9.)

**Step 4: Commit**

```bash
git add examples/linkradio/main.cpp examples/CMakeLists.txt
git commit -m "Add LinkRadio main app skeleton"
```

---

### Task 9: Audio platform integration

**Files:**
- Modify: `examples/linkradio/main.cpp` — integrate with platform audio engine

The existing `AudioEngine` template drives the audio callback and calls `LinkAudioRenderer`. For LinkRadio, we need it to call `LoopPlayer` instead. The cleanest approach: create a minimal `RadioAudioEngine` that uses the same `AudioPlatform` but calls `LoopPlayer` in its audio callback.

**Step 1: Create RadioAudioEngine**

Create `examples/linkradio/RadioAudioEngine.hpp`:
```cpp
#pragma once

#include "LoopPlayer.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

namespace linkradio
{

template <typename Link>
class RadioAudioEngine
{
public:
  RadioAudioEngine(Link& link);

  void audioCallback(std::chrono::microseconds hostTime, std::size_t numSamples);

  double sampleRate() const { return mSampleRate; }
  void setSampleRate(double sr) { mSampleRate = sr; }
  void setBufferSize(std::size_t sz);
  std::size_t bufferSize() const;

  LoopPlayer<Link>& loopPlayer() { return mLoopPlayer; }

  // Required by AudioPlatform interface
  std::chrono::microseconds outputLatency() const
  {
    return std::chrono::microseconds(mOutputLatency.load());
  }
  void setOutputLatency(std::chrono::microseconds latency)
  {
    mOutputLatency = latency.count();
  }

private:
  Link& mLink;
  double mSampleRate = 44100.0;
  LoopPlayer<Link> mLoopPlayer;
  std::atomic<int64_t> mOutputLatency{0};
};

template <typename Link>
RadioAudioEngine<Link>::RadioAudioEngine(Link& link)
  : mLink(link)
  , mLoopPlayer(link, mSampleRate)
{
}

template <typename Link>
void RadioAudioEngine<Link>::setBufferSize(std::size_t)
{
}

template <typename Link>
std::size_t RadioAudioEngine<Link>::bufferSize() const
{
  return 512;
}

template <typename Link>
void RadioAudioEngine<Link>::audioCallback(
  const std::chrono::microseconds hostTime,
  const std::size_t numSamples)
{
  auto sessionState = mLink.captureAudioSessionState();
  mLink.commitAudioSessionState(sessionState);

  mLoopPlayer(numSamples, sessionState, mSampleRate, hostTime, 4.0);
}

} // namespace linkradio
```

**Step 2: Update main.cpp to use RadioAudioEngine with AudioPlatform**

Replace the main.cpp content to wire `RadioAudioEngine` through the platform audio driver (CoreAudio/JACK/etc.), similar to how `link_audio_hut` uses `AudioEngine` with `AudioPlatform`.

The key integration point: `AudioPlatform<LinkAudio>` takes a reference to an engine that has `audioCallback(hostTime, numSamples)`, `setSampleRate()`, `setBufferSize()`, `setOutputLatency()`.

`RadioAudioEngine` provides all of these. Update `main.cpp` to instantiate:
```cpp
linkradio::RadioAudioEngine<ableton::LinkAudio> engine(link);
ableton::linkaudio::AudioPlatform<ableton::LinkAudio> audioPlatform(engine);
```

Then the main loop only handles manifest fetching and loop switching — the audio callback runs on the platform's audio thread.

**Step 3: Build and test manually**

```bash
cd build && cmake .. && make LinkRadio -j4
# Start a simple HTTP server with test manifest and stems
# ./bin/LinkRadio localhost /manifest.json 8080
```

**Step 4: Commit**

```bash
git add examples/linkradio/RadioAudioEngine.hpp examples/linkradio/main.cpp
git commit -m "Integrate LinkRadio with platform audio engine"
```

---

### Task 10: End-to-end test with local HTTP server

**Files:**
- Create: `examples/linkradio/test_server/` — simple test manifest and stems

**Step 1: Create test manifest and a Python test server script**

Create `examples/linkradio/test_server/manifest.json` with a manifest covering a long time window and a couple of test loops.

Create `examples/linkradio/test_server/generate_test_loops.py` — a Python script that generates silent WAV stems for testing:

```python
import struct, wave, os, json, datetime

STEMS = ["drums", "bass", "harmony", "melody"]
LOOPS = [
    {"id": "test_loop_01", "bpm": 120, "beats": 4},
    {"id": "test_loop_02", "bpm": 90, "beats": 8},
]

for loop in LOOPS:
    os.makedirs(f"loops/{loop['id']}", exist_ok=True)
    num_samples = int(44100 * loop["beats"] * 60.0 / loop["bpm"])
    for stem in STEMS:
        path = f"loops/{loop['id']}/{stem}.wav"
        with wave.open(path, "w") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(44100)
            w.writeframes(b"\x00\x00" * num_samples)

now = datetime.datetime.utcnow()
start = now.replace(hour=0, minute=0, second=0, microsecond=0)
end = start + datetime.timedelta(days=1)
manifest = {
    "sections": [{
        "start_time": start.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "end_time": end.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "loop_duration_seconds": 30,
        "loops": [{
            **loop,
            "stems": {s: f"loops/{loop['id']}/{s}.wav" for s in STEMS}
        } for loop in LOOPS]
    }]
}
with open("manifest.json", "w") as f:
    json.dump(manifest, f, indent=2)
print("Generated test manifest and WAV files")
```

**Step 2: Test manually**

```bash
cd examples/linkradio/test_server
python3 generate_test_loops.py
python3 -m http.server 8080 &
cd ../../../build
./bin/LinkRadio localhost /manifest.json 8080
```

Expected: Daemon starts, fetches manifest, downloads stems, begins playing. Link-enabled apps on the same network should see the session.

**Step 3: Commit**

```bash
git add examples/linkradio/test_server/
git commit -m "Add test server and loop generator for LinkRadio"
```
