# NOCTURNE — VST3 Audio-to-MIDI prototype

NOCTURNE is a JUCE-based VST3 audio-to-MIDI plugin with a custom dark-fantasy interface. It is designed around real-time vocal pitch tracking, an editable in-plugin piano roll, and MIDI export.

## Features
- Import WAV / AIFF / FLAC / MP3 and estimate a dominant monophonic melody.
- Analyse the live audio input supplied by the DAW while RECORD is active.
- Choose `VOCAL` analysis for monophonic voice and melodic lines, or `POLYPHONIC` analysis for simultaneous instrument notes and chords.
- Estimate the imported recording's major/minor key from its chroma profile and show the result in the piano-roll footer.
- Use a denser offline analysis hop and faster transition decisions to retain short vocal syllables and quick note changes.
- Display the incoming waveform and detected notes while recording.
- Apply a manually adjustable input gate in dB to both live capture and imported-audio analysis.
- Close the current note cleanly when recording stops.
- Stabilize note transitions with confidence gating, pitch smoothing, vibrato tolerance, a configurable transition-stability control, and short-dropout hold.
- Normalize YIN analysis frames and remove DC before pitch detection to reduce sensitivity to hot input levels.
- Clean imported MIDI detections by removing tiny ghost notes and merging short split sustains.
- Optional Chord Mode: map each stable detected root note to a selected major, minor, diminished, augmented, sus2, or sus4 voicing, emitted as simultaneous MIDI notes.
- Export a Standard MIDI File (.mid), with BPM, quantization, and per-note velocity derived from the source amplitude.
- Drag the generated `.mid` directly from the plugin into compatible DAW targets, including FL Studio's Piano roll or Channel Rack.
- Edit note pitch and start time in the plugin piano roll; automatically fit the displayed pitch range to the detected register.

## Important signal and chord limitations
The current pitch detector is monophonic: it follows one dominant pitch source. Chord Mode generates a configured chord from that detected root; it does **not** perform full polyphonic source separation or transcribe every independent pitch from an arbitrary recorded chord or mixed audio file.

The detector works best with a clean, isolated vocal or instrument line. Reverb-heavy vocals, strong backing tracks, breath/noise, extreme vibrato, and overlapping sources can still cause errors. The post-capture cleanup reduces short tracking glitches but cannot replace a dedicated polyphonic transcription model.

## FL Studio setup
Insert NOCTURNE as an effect on a Mixer Insert receiving the microphone or vocal audio. Select the correct physical input and enable input monitoring. Verify that the Mixer Insert level meter moves and that the NOCTURNE live waveform responds before pressing RECORD.

The plugin records notes internally. MIDI output routing varies by DAW and host configuration; exporting a .mid file is the reliable way to move the captured notes into a DAW piano roll. See [FL_STUDIO_SETUP_RU.md](FL_STUDIO_SETUP_RU.md) for the Windows / FL Studio walkthrough.

## Build
1. Install a C++17 toolchain and CMake 3.22+.
2. Configure/build (JUCE 8.0.6 is fetched by CMake):
   ```bash
   cmake -S . -B build
   cmake --build build --config Release
   ```
3. The VST3 bundle will be in the build tree under `NOCTURNE_artefacts/Release/VST3/`.

Windows installer instructions are in [INSTALLER_RU.md](INSTALLER_RU.md). GitHub Actions workflow `.github/workflows/build-windows-installer.yml` can build the Windows installer through the Actions tab.

## Current prototype status
The UI and core capture path are prototypes. A release build still needs to be compiled and tested against real microphone and imported-audio samples in the target DAW. The currently implemented Chord Mode is chord mapping from a monophonic detected root, not true polyphonic chord recognition.
