#include <algorithm>

namespace linkradio
{

template <typename Link>
LoopPlayer<Link>::LoopPlayer(Link& link, double& sampleRate)
  : mLink(link)
  , mSampleRate(sampleRate)
  , mSinks{
      ableton::LinkAudioSink(mLink, "drums", 4096),
      ableton::LinkAudioSink(mLink, "bass", 4096),
      ableton::LinkAudioSink(mLink, "harmony", 4096),
      ableton::LinkAudioSink(mLink, "melody", 4096)}
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
    auto buffer = ableton::LinkAudioSink::BufferHandle(mSinks[ch]);
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
                  numFrames,
                  1, // mono
                  static_cast<uint32_t>(sampleRate));
  }
}

} // namespace linkradio
