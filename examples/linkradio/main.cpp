#include "Fetcher.hpp"
#include "LoopPlayer.hpp"
#include "Manifest.hpp"
#include "Scheduler.hpp"
#include "StemStore.hpp"
#include "WavLoader.hpp"

#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
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
      auto paths = {std::cref(loop.stems.drums), std::cref(loop.stems.bass),
                    std::cref(loop.stems.harmony), std::cref(loop.stems.melody)};
      for (auto& pathRef : paths)
      {
        auto& path = pathRef.get();
        if (!path.empty() && !store.has(path))
        {
          std::cout << "Downloading: " << path << std::endl;
          std::string tmpPath = "/tmp/linkradio_" +
            std::to_string(std::hash<std::string>{}(path)) + ".wav";
          if (linkradio::httpDownload(config.serverHost,
                                       "/" + path, tmpPath, config.serverPort))
          {
            auto wav = linkradio::loadWav(tmpPath);
            if (!wav.samples.empty())
            {
              store.put(path, wav.samples, wav.sampleRate);
              std::cout << "  Loaded: " << wav.samples.size() << " samples @ "
                        << wav.sampleRate << " Hz" << std::endl;
            }
          }
        }
      }
    }
  }
}

// Timer-driven audio thread that pushes samples into LinkAudioSinks
// at regular intervals, simulating an audio callback.
class AudioTimer
{
public:
  AudioTimer(linkradio::LoopPlayer<ableton::LinkAudio>& loopPlayer,
             ableton::LinkAudio& link,
             double sampleRate,
             size_t bufferSize)
    : mLoopPlayer(loopPlayer)
    , mLink(link)
    , mSampleRate(sampleRate)
    , mBufferSize(bufferSize)
    , mRunning(true)
  {
    mThread = std::thread([this]() { run(); });
  }

  ~AudioTimer()
  {
    mRunning = false;
    if (mThread.joinable()) mThread.join();
  }

private:
  void run()
  {
    using namespace std::chrono;
    const auto bufferDuration = microseconds(
      static_cast<long long>(1e6 * static_cast<double>(mBufferSize) / mSampleRate));

    // Advance hostTime by exact buffer duration rather than reading wall clock
    // each iteration. sleep_for overshoots unpredictably, causing beat positions
    // to advance faster than the audio content. The receiver then time-stretches
    // to compensate, resulting in slowed audio.
    auto hostTime = mLink.clock().micros();

    while (mRunning)
    {
      auto sessionState = mLink.captureAudioSessionState();
      mLink.commitAudioSessionState(sessionState);

      mLoopPlayer(mBufferSize, sessionState, mSampleRate, hostTime, 4.0);

      hostTime = hostTime + bufferDuration;

      // Sleep until next ideal host time, skipping if already late
      auto now = mLink.clock().micros();
      if (hostTime > now)
      {
        std::this_thread::sleep_for(hostTime - now);
      }
    }
  }

  linkradio::LoopPlayer<ableton::LinkAudio>& mLoopPlayer;
  ableton::LinkAudio& mLink;
  double mSampleRate;
  size_t mBufferSize;
  std::atomic<bool> mRunning;
  std::thread mThread;
};

} // namespace

int main(int argc, char* argv[])
{
  auto config = parseArgs(argc, argv);

  constexpr double kSampleRate = 44100.0;
  constexpr size_t kBufferSize = 512;

  ableton::LinkAudio link(120.0, "LinkRadio");
  link.enable(true);
  link.enableLinkAudio(true);
  link.enableStartStopSync(true);

  double sampleRate = kSampleRate;
  linkradio::StemStore stemStore;
  linkradio::LoopPlayer<ableton::LinkAudio> loopPlayer(link, sampleRate);
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

  // Find current section and download stems
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

  // Set tempo and start playing
  {
    auto sessionState = link.captureAppSessionState();
    sessionState.setTempo(currentLoop.bpm, link.clock().micros());
    sessionState.setIsPlaying(true, link.clock().micros());
    link.commitAppSessionState(sessionState);
  }

  std::cout << "Playing: " << currentLoop.id
            << " at " << currentLoop.bpm << " BPM\n";

  // Start the audio timer thread
  AudioTimer audioTimer(loopPlayer, link, kSampleRate, kBufferSize);

  // Main loop: manifest fetching and loop switching
  auto lastFetch = std::chrono::steady_clock::now();
  std::string lastLoopId = currentLoop.id;

  while (true)
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
}
