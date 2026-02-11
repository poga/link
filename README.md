# LinkRadio

A multi-stem radio broadcaster built on [Ableton Link](http://ableton.github.io/link).

LinkRadio is a daemon that broadcasts tempo-synced, multi-stem audio loops over a local network. All listeners on the same Link session hear the same loop at the same time — determined by UTC clock, no central coordination needed.

## How it works

- Fetches a JSON **manifest** from an HTTP server describing scheduled loops
- Each loop has 4 stem channels: **drums**, **bass**, **harmony**, **melody**
- A UTC-based scheduler deterministically picks which loop to play — all instances worldwide agree without coordination
- Audio is broadcast via [Ableton Link](http://ableton.github.io/link) for beat/tempo sync
- Manifest is re-fetched every 60s, enabling live programming

## Manifest format

```json
{
  "sections": [{
    "start_time": "2020-01-01T00:00:00Z",
    "end_time": "2030-12-31T23:59:59Z",
    "loop_duration_seconds": 30,
    "loops": [{
      "id": "my_loop",
      "bpm": 120.0,
      "beats": 8,
      "stems": {
        "drums": "drums.wav",
        "bass": "bass.wav",
        "harmony": "",
        "melody": ""
      }
    }]
  }]
}
```

Empty stem paths = silence on that channel.

## Audio format

WAV files only. PCM 16-bit or 24-bit, mono or stereo (stereo is mixed down to mono).

## Building

```
git submodule update --init --recursive
mkdir build && cd build
cmake ..
cmake --build .
```

Binary goes to `build/bin/LinkRadio`.

### Requirements

| Platform | Compiler             |
|----------|----------------------|
| Mac      | Xcode 16.2.0+       |
| Linux    | Clang 13+ or GCC 10+|
| Windows  | MSVC 17 2022+       |

## Usage

```
LinkRadio <host> <manifest-path> [port]
```

Example:
```
LinkRadio example.com /manifest.json 8080
```

## Testing

A test server with a manifest and sample loops is included:

```
cd examples/linkradio/test_server
python3 generate_test_loops.py
python3 -m http.server 8080
```

Then in another terminal:
```
./build/bin/LinkRadio localhost /manifest.json 8080
```

## License

Ableton Link is dual [licensed][license] under GPLv2+ and a proprietary license. If you
would like to incorporate Link into a proprietary software application, please contact
<link-devs@ableton.com>.

[license]: LICENSE.md
