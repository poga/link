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
