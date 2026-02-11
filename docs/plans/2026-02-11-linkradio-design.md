# LinkRadio Design

An always-on radio daemon that broadcasts synced multitrack audio loops over Ableton Link, so anyone on the local network can jam along with the same music playing worldwide.

## System Overview

Two components:

1. **Radio daemon** (C++ CLI) — built on `link_audio_hut`. Joins a Link session, broadcasts 4 audio channels, sets tempo.
2. **Playlist server** (HTTP) — serves a JSON manifest and WAV files. Can be a static file host or S3 bucket.

## Global Sync

All daemons worldwide play the same loop at the same time using deterministic UTC-based scheduling. No real-time coordination between daemons.

Given a section's `start_time`, `loop_duration_seconds`, and loop count:

```
elapsed = now_utc - start_time
loop_index = floor(elapsed / loop_duration_seconds) % num_loops
```

## Manifest Format

Fetched every 60 seconds. Contains contiguous time sections, each with its own loop pool.

```json
{
  "sections": [
    {
      "start_time": "2026-02-11T00:00:00Z",
      "end_time": "2026-02-11T01:00:00Z",
      "loop_duration_seconds": 120,
      "loops": [
        {
          "id": "funk_01",
          "bpm": 110,
          "beats": 16,
          "stems": {
            "drums": "loops/funk_01/drums.wav",
            "bass": "loops/funk_01/bass.wav",
            "harmony": "loops/funk_01/harmony.wav",
            "melody": "loops/funk_01/melody.wav"
          }
        }
      ]
    }
  ]
}
```

- Sections are contiguous (no gaps).
- Empty string for a stem means silence on that channel.
- Stem URLs are relative to the server root.
- Audio files: WAV, 16-bit signed PCM.

## Fixed Stem Layout

Every loop has the same 4 channels, like a consistent band:

| Channel | Role |
|---------|------|
| drums | Rhythmic foundation |
| bass | Low-end groove |
| harmony | Chords, pads, keys |
| melody | Lead line, hook |

## Daemon Architecture

### Components

- **ManifestFetcher** — HTTP client, polls every 60s, parses JSON, downloads missing stems.
- **Scheduler** — Pure function. Given manifest + UTC time, returns current section and loop index.
- **LoopPlayer** — Feeds 4 stem WAVs into 4 LinkAudioSink channels. Hard-cuts on beat boundary when switching loops. Sets Link session tempo to current loop's BPM.

### Memory

Keep current + next 2 sections' stems in memory. On manifest refetch, download and load new stems as needed to maintain this 3-section buffer. Evict anything older.

### Startup Flow

1. Fetch manifest
2. Download stems for current + next 2 sections
3. Start Link session
4. Announce 4 channels: drums, bass, harmony, melody
5. Determine current loop from UTC time + manifest
6. Set tempo, start playing
7. Main loop: play audio, refetch manifest every 60s, switch loops on schedule

### Loop Transitions

- Hard-cut on beat boundary (downbeat).
- Tempo snaps to the new loop's BPM immediately.
- All Link peers follow the tempo change automatically.

### Failure Handling

- If manifest refetch fails, keep using the last good manifest.
- If a stem download fails, play silence on that channel.
