# NOCTURNE offline regression tests

The command-line analyzer runs the same `NocturneAudioProcessor::analyseFile()`
path used by the plugin. It does not host the VST3 and does not require FL Studio.
It reads audio through JUCE's registered audio formats (WAV/MP3 and other basic
formats), applies the selected analysis mode, and exports a MIDI file.

## Build

On Windows, from the repository root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target NOCTURNEAnalyzer
```

The executable is produced in the CMake build tree as
`NOCTURNEAnalyzer.exe` (exact artefact directory depends on the generator).

## Prepare test data

Keep the reference MIDI and rendered audio together in a local folder. Do not
commit private recordings to the repository. Create a `manifest.json` in that
folder; paths are relative to the manifest:

```json
{
  "tests": [
    {
      "name": "01 mono legato",
      "audio": "audio/01_Mono_Legato_Scale.wav",
      "reference": "reference/01_Mono_Legato_Scale.mid",
      "mode": "mono"
    },
    {
      "name": "05 piano chords",
      "audio": "audio/05_Poly_Triad_Chords.wav",
      "reference": "reference/05_Poly_Triad_Chords.mid",
      "mode": "poly"
    }
  ]
}
```

Add the other five tests using the same structure. Use `mono` for monophonic
material and `poly` for the chord test. The analyzer keeps the plugin's default
parameter values, including the default input gate; MIDI quantization is
disabled for measurement.

## Run

Install the Python dependency once:

```powershell
py -m pip install -r Tests/requirements.txt
```

Then run:

```powershell
py Tests/compare_midi.py --analyzer "C:\path\to\NOCTURNEAnalyzer.exe" --manifest "D:\NOCTURNE_TESTS\manifest.json" --output-dir "D:\NOCTURNE_TESTS\output"
```

The runner writes one generated MIDI per test plus `report.json`. The report
includes note counts, missing/extra notes, pitch accuracy, onset MAE, duration
MAE, and velocity MAE. Matching currently pairs notes by nearest onset within
250 ms; this is a first-pass regression metric, not a substitute for listening
tests or detailed polyphonic note-assignment review.

## Important limits

- This tests the offline file-analysis path, not live recording latency or the
  VST3 host integration.
- The analyzer executable must be built from the same commit as the code being
  evaluated.
- Test results are meaningful only when the audio render corresponds to the
  supplied reference MIDI and the same analysis mode is selected.
