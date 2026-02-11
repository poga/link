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
      auto loopIndex = static_cast<size_t>(
        (elapsed / section.loopDurationSeconds)
        % static_cast<long long>(section.loops.size()));
      return ScheduleResult{i, loopIndex};
    }
  }
  return std::nullopt;
}

} // namespace linkradio
