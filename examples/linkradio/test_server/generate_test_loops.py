#!/usr/bin/env python3
"""Generate test WAV stems and a manifest for LinkRadio testing.

Run this script, then serve files with:
  python3 -m http.server 8080

Then run LinkRadio:
  ./build/bin/LinkRadio localhost /manifest.json 8080
"""

import json
import os
import struct
import wave
from datetime import datetime, timedelta, timezone

STEMS = ["drums", "bass", "harmony", "melody"]
LOOPS = [
    {"id": "test_loop_01", "bpm": 120, "beats": 4},
    {"id": "test_loop_02", "bpm": 90, "beats": 8},
]

def generate():
    for loop in LOOPS:
        loop_dir = os.path.join("loops", loop["id"])
        os.makedirs(loop_dir, exist_ok=True)
        num_samples = int(44100 * loop["beats"] * 60.0 / loop["bpm"])
        for stem in STEMS:
            path = os.path.join(loop_dir, f"{stem}.wav")
            with wave.open(path, "w") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(44100)
                # Generate silence (could be replaced with actual audio)
                w.writeframes(b"\x00\x00" * num_samples)
            print(f"  Generated {path} ({num_samples} samples)")

    now = datetime.now(timezone.utc)
    start = now.replace(hour=0, minute=0, second=0, microsecond=0)
    end = start + timedelta(days=1)

    manifest = {
        "sections": [{
            "start_time": start.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "end_time": end.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "loop_duration_seconds": 30,
            "loops": [{
                **loop,
                "stems": {s: f"loops/{loop['id']}/{s}.wav" for s in STEMS}
            } for loop in LOOPS]
        }]
    }

    with open("manifest.json", "w") as f:
        json.dump(manifest, f, indent=2)

    print(f"\nGenerated manifest.json covering {start.date()}")
    print(f"  {len(LOOPS)} loops, switching every 30 seconds")
    print(f"\nTo serve: python3 -m http.server 8080")
    print(f"To play:  ./build/bin/LinkRadio localhost /manifest.json 8080")

if __name__ == "__main__":
    generate()
