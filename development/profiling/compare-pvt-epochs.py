"""Compare measured PVT outputs at equal sample epochs and constellation counts.

This compares rounded log coordinates, not byte-identical GNSS observations.
Prediction-only epochs are excluded. No receiver settings are modified.
"""
import argparse
import csv
import json
import math
from pathlib import Path


def measured(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    result = {}
    for row in rows:
        gps, gal = int(row["GpsSatellites"]), int(row["GalileoSatellites"])
        if not gps + gal:
            continue
        key = (int(row["RxMs"]), gps, gal)
        if key in result:
            raise ValueError(f"Duplicate epoch {key} in {path}")
        result[key] = row
    return result


def metrics(rows):
    rows = list(rows)
    if not rows:
        return {"count": 0}
    east = [float(row["East"]) for row in rows]
    north = [float(row["North"]) for row in rows]
    up = [float(row["Up"]) for row in rows]
    mean_east, mean_north, mean_up = (sum(values) / len(rows) for values in (east, north, up))
    centered_horizontal = [math.hypot(e - mean_east, n - mean_north) for e, n in zip(east, north)]
    return {
        "count": len(rows),
        "horizontal_rms_m": math.sqrt(sum(float(r["Horizontal"]) ** 2 for r in rows) / len(rows)),
        "up_rms_m": math.sqrt(sum(float(r["Up"]) ** 2 for r in rows) / len(rows)),
        "east_mean_m": mean_east,
        "north_mean_m": mean_north,
        "up_mean_m": mean_up,
        "centered_horizontal_rms_m": math.sqrt(sum(value ** 2 for value in centered_horizontal) / len(rows)),
        "centered_horizontal_p95_m": sorted(centered_horizontal)[math.ceil(.95 * len(rows)) - 1],
        "up_population_sd_m": math.sqrt(sum((value - mean_up) ** 2 for value in up) / len(rows)),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    before, after = measured(args.before), measured(args.after)
    common = sorted(before.keys() & after.keys())
    common_kf = [key for key in common if before[key]["Solver"] == after[key]["Solver"] == "KF"]
    result = {
        "before_csv": str(args.before),
        "after_csv": str(args.after),
        "matching_rule": "RxMs, GPS count, Galileo count; measured outputs only",
        "before_all_measured": metrics(before.values()),
        "after_all_measured": metrics(after.values()),
        "before_common": metrics(before[key] for key in common),
        "after_common": metrics(after[key] for key in common),
        "before_common_kf_only": metrics(before[key] for key in common_kf),
        "after_common_kf_only": metrics(after[key] for key in common_kf),
        "common_epochs": common,
        "unmatched_before": sorted(before.keys() - after.keys()),
        "unmatched_after": sorted(after.keys() - before.keys()),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if "common" in key and key != "common_epochs"}))


if __name__ == "__main__":
    main()
