#include "StemStore.hpp"
#include <catch.hpp>

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
