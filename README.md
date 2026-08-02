# WebRTC AEC3

A standalone extraction of the WebRTC Acoustic Echo Canceller 3 (AEC3) module.

## Build

This project uses [pixi](https://pixi.sh) with CMake and Ninja.

```bash
pixi run -e build configure
pixi run -e build build
```

This produces `libwebrtc_aec3.dylib` (macOS), `libwebrtc_aec3.so` (Linux), or `webrtc_aec3.dll` (Windows) in `.build/`.

## Test

```bash
pixi run -e build configure-test
pixi run -e build build-test
pixi run -e build run-test
```

The test loads 16 kHz mono WAV files (far-end and near-end), processes them through the AEC3, and writes the output to `tests/cancelled.wav`.

## Waveform Visualization

![](https://github.com/user-attachments/assets/9b18c7be-82f9-41f5-bac2-2c6868010e8d)

## License

BSD 3-Clause (same as WebRTC). See [LICENSE](LICENSE).