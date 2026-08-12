# LAMEglitch

Realtime MP3 encode/decode corruption effect for VST3, AU, and Standalone hosts.

In Real MP3 mode, LAMEglitch sends fixed-size PCM frames through a preallocated
single-producer/single-consumer queue to a dedicated worker. The worker runs the
bundled Shine encoder, corrupts the encoded MP3 payload bytes, decodes them with
dr_mp3, and returns fixed-size wet frames. During realtime playback,
`processBlock` performs no codec work, blocking wait, or heap allocation.
Simulation remains an explicit alternate mode
and an indicated fallback when Shine does not support the host sample rate.

## Identity

- Owner: EsionHsrahLatigid
- Company: EsionHsrahLatigid
- Manufacturer code: EHL_
- Plug-in code: LmGl
- Bundle ID: jp.ehl.lameglitch

## Build

Requirements:

- CMake 3.22 or newer
- A C++17 compiler
- Xcode on macOS for AU and Standalone builds

JUCE 8.0.15 is fetched automatically when a local `JUCE/` checkout is not
present. The MP3 codec sources are vendored under `libs/`.

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DLAMEGLITCH_BUILD_PLUGIN=ON \
  -DLAMEGLITCH_BUILD_TESTS=ON
cmake --build build/release --target LAMEglitch_Artifacts \
  LAMEglitchSmokeTests LAMEglitchMP3CodecTests \
  LAMEglitchRealtimeSafetyTests LAMEglitchWorkerTests --parallel 2
ctest --test-dir build/release --output-on-failure
```

Staged products are written to:

- `artifacts/Release/VST3/LAMEglitch.vst3`
- `artifacts/Release/AU/LAMEglitch.component` on macOS
- `artifacts/Release/Standalone/LAMEglitch.app` on macOS

## Parameters

| Parameter | Description |
| --- | --- |
| Corruption | Master corruption amount |
| Bit Flip | Random bit-flip probability |
| Byte Drop | Probability of replacing bytes with zero |
| Frame Repeat | Probability of byte/frame repetition artifacts |
| Bitrate | MP3 bitrate from 32 to 320 kbps |
| Mix | Dry/wet balance |
| Mode | Real MP3 worker path or explicit simulation mode |

## Realtime and latency contract

- Shine and dr_mp3 are owned exclusively by the worker thread.
- Audio callbacks exchange only fixed-capacity frames; queue underruns use the
  latency-aligned dry signal instead of blocking or shifting late audio.
- Offline rendering waits for each submitted worker frame so that rendering
  faster than wall clock does not discard the MP3 effect.
- The plug-in reports the fixed queue plus MP3 codec latency to the host and
  applies the same delay in both Real MP3 and Simulation modes.
- Changing Bitrate reinitializes Shine on the worker; the corruption controls are
  snapshotted for each encoded frame.

## Dependency Licenses

- JUCE: GPL/commercial license from JUCE
- Shine: LGPL, bundled in `libs/shine-3.1.1`
- dr_mp3: public domain / Unlicense, bundled in `libs/dr_libs-mp3-0.7.2`

## License

MIT for this plug-in code. See `LICENSE`.
