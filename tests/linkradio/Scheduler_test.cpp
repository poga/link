#include "Scheduler.hpp"
#include <catch.hpp>

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
