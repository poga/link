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
