#include "Manifest.hpp"
#include <catch.hpp>

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
