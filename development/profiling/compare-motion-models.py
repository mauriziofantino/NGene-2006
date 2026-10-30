"""Paired synthetic trajectories using archived/current extracted audit binaries.

Each binary must support --motion-series SEED. The receiver never sees truth;
truth is confined to the common measurement generator and error reporting.
"""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import statistics
import subprocess


def run(executable, seed):
    result = subprocess.run([str(executable), "--motion-series", str(seed)],
                            capture_output=True, text=True, check=True)
    return list(csv.DictReader(io.StringIO(result.stdout)))


def metrics(rows):
    e, n, u, v = ([float(row[key]) for row in rows] for key in
                  ("EastError", "NorthError", "UpError", "VelocityError"))
    me, mn, mu = (statistics.mean(values) for values in (e, n, u))
    return {
        "position_mse": statistics.mean(x*x + y*y + z*z for x, y, z in zip(e, n, u)),
        "horizontal_centered_variance": statistics.mean((x-me)**2 + (y-mn)**2 for x, y in zip(e, n)),
        "up_centered_variance": statistics.mean((z-mu)**2 for z in u),
        "velocity_mse": statistics.mean(x*x for x in v),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixed", type=Path)
    parser.add_argument("adaptive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--trials", type=int, default=20)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    phases = ("quiet", "accelerating", "cruise_east", "turning", "cruise_north", "braking", "quiet_after")
    values = {phase: {label: [] for label in ("WLS", "fixed", "adaptive")} for phase in phases}
    seeds = [194527 + 7919 * trial for trial in range(args.trials)]
    for seed in seeds:
        fixed, adaptive = run(args.fixed.resolve(), seed), run(args.adaptive.resolve(), seed)
        fixed_ls = [row for row in fixed if row["Solver"] == "WLS"]
        adaptive_ls = [row for row in adaptive if row["Solver"] == "WLS"]
        for before, after in zip(fixed_ls, adaptive_ls, strict=True):
            for key in ("Epoch", "EastError", "NorthError", "UpError", "VelocityError"):
                if before[key] != after[key]:
                    raise ValueError(f"WLS changed at seed={seed}, epoch={before['Epoch']}")
        for phase in phases:
            for label, rows, solver in (("WLS", fixed, "WLS"), ("fixed", fixed, "KF"),
                                         ("adaptive", adaptive, "KF")):
                selection = [row for row in rows if row["Solver"] == solver and
                             row["Phase"] == phase and int(row["Gps"]) + int(row["Gal"]) > 0 and
                             (phase != "quiet" or int(row["Epoch"]) >= 20)]
                values[phase][label].append(metrics(selection))
        if seed == seeds[0]:
            for label, rows in (("fixed", fixed), ("adaptive", adaptive)):
                with (args.output / f"motion-{label}.csv").open("w", newline="", encoding="utf-8") as stream:
                    writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
                    writer.writeheader()
                    writer.writerows(rows)
    report = {
        "seeds": seeds,
        "method": "Paired data; quiet epochs 20..59; no-measurement epochs excluded; per-trial centering.",
        "fixed_sha256": hashlib.sha256(args.fixed.read_bytes()).hexdigest(),
        "adaptive_sha256": hashlib.sha256(args.adaptive.read_bytes()).hexdigest(),
        "phases": {},
    }
    for phase in phases:
        report["phases"][phase] = {}
        for key in values[phase]["fixed"][0]:
            fixed = [value[key] for value in values[phase]["fixed"]]
            adaptive = [value[key] for value in values[phase]["adaptive"]]
            paired = [after-before for before, after in zip(fixed, adaptive)]
            error = 1.96 * statistics.stdev(paired) / math.sqrt(len(paired)) if len(paired) > 1 else 0
            report["phases"][phase][key] = {
                "WLS_rms_or_sd": math.sqrt(statistics.mean(value[key] for value in values[phase]["WLS"])),
                "fixed_rms_or_sd": math.sqrt(statistics.mean(fixed)),
                "adaptive_rms_or_sd": math.sqrt(statistics.mean(adaptive)),
                "paired_mean_difference_squared_units": statistics.mean(paired),
                "paired_normal_95_interval_squared_units": [statistics.mean(paired)-error, statistics.mean(paired)+error],
            }
    (args.output / "motion-comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for phase, result in report["phases"].items():
        print(phase, json.dumps({key: [value["fixed_rms_or_sd"], value["adaptive_rms_or_sd"]]
                                for key, value in result.items()}))


if __name__ == "__main__":
    main()
