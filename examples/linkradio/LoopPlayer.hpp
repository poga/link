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
  std::array<ableton::LinkAudioSink, 4> mSinks;
  Loop mCurrentLoop;
  std::array<size_t, 4> mPlayPositions = {};
  StemStore* mpStemStore = nullptr;
};

} // namespace linkradio

#include "LoopPlayer.ipp"

#endif // LINK_AUDIO
