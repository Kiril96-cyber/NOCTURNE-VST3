#!/usr/bin/env python3
"""Run NOCTURNE's offline analyzer and compare its MIDI with reference MIDI files.

Manifest format:
{
  "tests": [
    {"name":"01 mono legato", "audio":"audio/01.wav",
     "reference":"reference/01.mid", "mode":"mono"}
  ]
}
Requires: pretty_midi
"""
import argparse
import json
import pathlib
import subprocess
import sys
import pretty_midi


def load_notes(path):
    midi = pretty_midi.PrettyMIDI(str(path))
    return sorted(
        [(int(n.pitch), float(n.start), float(n.end), int(n.velocity))
         for inst in midi.instruments for n in inst.notes],
        key=lambda n: (n[1], n[0])
    )


def compare(expected, actual, onset_tolerance=0.25):
    remaining = set(range(len(actual)))
    matched = []
    missing = 0
    for ref in expected:
        candidates = [
            (abs(actual[i][1] - ref[1]), i)
            for i in remaining if abs(actual[i][1] - ref[1]) <= onset_tolerance
        ]
        if not candidates:
            missing += 1
            continue
        _, idx = min(candidates)
        remaining.remove(idx)
        matched.append((ref, actual[idx]))

    pitch_ok = sum(a[0] == e[0] for e, a in matched)
    onset_errors = [abs(a[1] - e[1]) for e, a in matched]
    duration_errors = [abs((a[2] - a[1]) - (e[2] - e[1])) for e, a in matched]
    velocity_errors = [abs(a[3] - e[3]) for e, a in matched]
    mean = lambda xs: round(sum(xs) / len(xs), 4) if xs else None
    return {
        "reference_notes": len(expected), "detected_notes": len(actual),
        "matched_by_onset": len(matched), "missing_notes": missing,
        "extra_notes": len(remaining),
        "pitch_correct": pitch_ok,
        "pitch_accuracy_pct": round(100 * pitch_ok / len(matched), 1) if matched else 0.0,
        "onset_mae_seconds": mean(onset_errors),
        "duration_mae_seconds": mean(duration_errors),
        "velocity_mae": mean(velocity_errors),
        "notes": [
            {"expected_pitch": e[0], "actual_pitch": a[0],
             "expected_start": round(e[1], 4), "actual_start": round(a[1], 4),
             "expected_duration": round(e[2]-e[1], 4),
             "actual_duration": round(a[2]-a[1], 4),
             "expected_velocity": e[3], "actual_velocity": a[3]}
            for e, a in matched
        ]
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--analyzer", required=True, help="Path to NOCTURNEAnalyzer executable")
    ap.add_argument("--manifest", required=True, help="JSON manifest mapping audio to reference MIDI")
    ap.add_argument("--output-dir", default="nocturne-test-output")
    ap.add_argument("--onset-tolerance", type=float, default=0.25)
    args = ap.parse_args()

    manifest_path = pathlib.Path(args.manifest).resolve()
    root = manifest_path.parent
    out_dir = pathlib.Path(args.output_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    tests = json.loads(manifest_path.read_text(encoding="utf-8"))["tests"]
    report = {"tests": [], "summary": {}}

    for i, test in enumerate(tests, 1):
        audio = (root / test["audio"]).resolve()
        reference = (root / test["reference"]).resolve()
        mode = test.get("mode", "mono")
        actual_midi = out_dir / f"{i:02d}_{pathlib.Path(test['name']).stem}.mid"
        run = subprocess.run([args.analyzer, str(audio), str(actual_midi), mode],
                             text=True, capture_output=True)
        item = {"name": test["name"], "mode": mode, "command_exit_code": run.returncode,
                "stdout": run.stdout.strip(), "stderr": run.stderr.strip()}
        if run.returncode == 0 and actual_midi.exists() and reference.exists():
            item.update(compare(load_notes(reference), load_notes(actual_midi),
                                args.onset_tolerance))
        else:
            item["error"] = "Analyzer failed or input/reference file is missing"
        report["tests"].append(item)
        print(f"[{i}/{len(tests)}] {test['name']}: "
              f"{item.get('detected_notes', 'FAILED')} notes, "
              f"pitch accuracy {item.get('pitch_accuracy_pct', 'n/a')}%")

    valid = [t for t in report["tests"] if "reference_notes" in t]
    report["summary"] = {
        "tests_total": len(tests), "tests_completed": len(valid),
        "reference_notes": sum(t["reference_notes"] for t in valid),
        "detected_notes": sum(t["detected_notes"] for t in valid),
        "missing_notes": sum(t["missing_notes"] for t in valid),
        "extra_notes": sum(t["extra_notes"] for t in valid),
        "pitch_correct": sum(t["pitch_correct"] for t in valid),
        "matched_by_onset": sum(t["matched_by_onset"] for t in valid)
    }
    report_path = out_dir / "report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Detailed report: {report_path}")
    return 0 if len(valid) == len(tests) else 1


if __name__ == "__main__":
    sys.exit(main())
