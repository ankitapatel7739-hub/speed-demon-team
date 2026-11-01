#!/usr/bin/env python3
"""Speed Demon 2026 - Track A: Ingest."""

import os

INPUT_NAME = "requests.log"
OUTPUT_NAME = "summary.txt"


def percentile(sorted_values, p):
    n = len(sorted_values)
    return sorted_values[(p * n + 99) // 100 - 1]


def main(data_dir="/data", out_dir="/out"):
    groups = {}

    input_path = os.path.join(data_dir, INPUT_NAME)

    with open(input_path, "r", encoding="ascii", buffering=1024 * 1024) as f:
        for line in f:
            # Fixed timestamp format:
            # YYYY-MM-DDTHH:MM:SS.ffffffZ
            hour = line[:13]

            parts = line.split()
            endpoint = parts[2]
            status = parts[3]
            latency = int(parts[4])

            key = (hour, endpoint)

            group = groups.get(key)
            if group is None:
                group = [[], 0]
                groups[key] = group

            group[0].append(latency)

            if status[0] == "5":
                group[1] += 1

    lines = []

    for (hour, endpoint), (values, errors) in sorted(groups.items()):
        values.sort()

        count = len(values)

        p50 = values[(50 * count + 99) // 100 - 1]
        p95 = values[(95 * count + 99) // 100 - 1]
        p99 = values[(99 * count + 99) // 100 - 1]

        ppm = errors * 1_000_000 // count

        lines.append(
            f"{hour}:00:00Z {endpoint} {count} "
            f"{p50} {p95} {p99} {errors} {ppm}\n"
        )

    os.makedirs(out_dir, exist_ok=True)

    with open(
        os.path.join(out_dir, OUTPUT_NAME),
        "w",
        encoding="ascii",
        newline="\n",
    ) as f:
        f.write("".join(lines))


if __name__ == "__main__":
    main()