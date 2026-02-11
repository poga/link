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
