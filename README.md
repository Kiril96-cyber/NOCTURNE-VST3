# NOCTURNE — VST3 source prototype

NOCTURNE is a JUCE-based monophonic audio-to-MIDI VST3 prototype with a dark-fantasy minimal interface.

## Features in this prototype
- Import WAV / AIFF / FLAC / MP3 and estimate a monophonic melody.
- Analyse the DAW input in real time while RECORD is active.
- Tune read speed, confidence threshold (note accuracy), and pitch smoothing (glide).
- View detected note events on a piano-roll-like timeline and monitor input level.
- Export a Standard MIDI File (`.mid`) and copy detected note names to the clipboard.

## Important limitations
This is a functional starting-point prototype, not a production-ready commercial pitch-to-MIDI engine. The detector is a lightweight YIN-style monophonic pitch estimator. It works best with a clean, isolated vocal or instrument line. Polyphonic mixes, reverb-heavy vocals, breath/noise, vibrato and overlapping notes can cause errors. "Read speed" controls the analysis cadence parameter, not time-stretching. The timeline is a compact note overview; a production release should add waveform file display, editable MIDI notes, tempo/grid quantization, polyphonic separation, robust note-on/off segmentation and latency compensation.

The live path analyses the audio input supplied by the host. In a DAW, place the plugin on an audio track with the microphone routed to that track and enable input monitoring. MIDI export is currently the reliable transfer path; host-specific live MIDI routing varies.

## Build
1. Install a C++17 toolchain and CMake 3.22+.
2. Configure/build (JUCE 8.0.6 is fetched by CMake):
   ```bash
   cmake -S . -B build
   cmake --build build --config Release
   ```
3. The VST3 bundle will be in the build tree under `NOCTURNE_artefacts/Release/VST3/`.

Test in your DAW and rescan plugins. macOS/Windows/Linux builds depend on the local compiler and JUCE platform prerequisites.

## NOCTURNE v0.3 — editor update
- Imported audio now has a waveform preview in the plugin window.
- Notes are shown in a piano-roll style grid; drag a note vertically to change pitch and horizontally to change its start time.
- Added project BPM and MIDI export quantization (OFF, 1/4, 1/8, 1/16, 1/32).
- Quantization is applied during MIDI export; it does not rewrite the original detected note data.

The editor is still a prototype. Piano-roll editing currently moves note pitch and start time only; note length resizing, multi-note selection, undo, and DAW tempo sync are not implemented. The pitch detector remains monophonic.
- Added a Vibrato Tolerance control that increases pitch stabilization to reduce rapid semitone flips on sustained/vibrato vocals.


## Windows installer
See `INSTALLER_RU.md`. On Windows, run `build_installer_windows.bat` from the VS 2022 x64 Native Tools Command Prompt after installing CMake and Inno Setup 6. It creates `dist/NOCTURNE_Setup_x64.exe` with the NOCTURNE icon and installs the VST3 bundle to the system VST3 folder.


## Автоматическая сборка Windows EXE через GitHub Actions
См. `BUILD_ON_GITHUB_RU.md`. Workflow `.github/workflows/build-windows-installer.yml` собирает VST3 на Windows Server 2022 и публикует `NOCTURNE_Setup_x64.exe` в Artifacts.
