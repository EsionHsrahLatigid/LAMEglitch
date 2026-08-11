# LAMEglitch

Realtime MP3 encode/decode corruption effect for VST3, AU, and Standalone hosts.

LAMEglitch contains the bundled Shine encoder and dr_mp3 decoder, but the
realtime audio callback is constrained to the deterministic simulation path. The
real codec can allocate internally, so it is kept out of `processBlock` until a
bounded worker-thread/offline path is implemented.

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
cmake --build build/release --target LAMEglitch_Artifacts LAMEglitchSmokeTests --parallel 2
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
| Mode | User-facing safe simulation selector; realtime processing remains simulation-only |

## Dependency Licenses

- JUCE: GPL/commercial license from JUCE
- Shine: LGPL, bundled in `libs/shine-3.1.1`
- dr_mp3: public domain / Unlicense, bundled in `libs/dr_libs-mp3-0.7.2`

## License

MIT for this plug-in code. See `LICENSE`.
