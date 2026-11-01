#!/usr/bin/env python3
"""Speed Demon 2026 - Track A: Ingest."""

import os

INPUT_NAME = "requests.log"
OUTPUT_NAME = "summary.txt"


def main(data_dir="/data", out_dir="/out"):
    groups = {}

    input_path = os.path.join(data_dir, INPUT_NAME)

    with open(
        input_path,
        "r",
        encoding="ascii",
        buffering=1024 * 1024
    ) as f:

        for line in f:
            parts = line.split()

            hour = parts[0][:13]
            endpoint = parts[2]
            status = parts[3]
            latency = int(parts[4])

            key = hour + "\x00" + endpoint

            group = groups.get(key)

            if group is None:
                group = [hour, endpoint, [], 0]
                groups[key] = group

            group[2].append(latency)

            if status[0] == "5":
                group[3] += 1

    result = []

    for group in groups.values():
        hour = group[0]
        endpoint = group[1]
        values = group[2]
        errors = group[3]

        values.sort()

        count = len(values)

        p50 = values[(count * 50 + 99) // 100 - 1]
        p95 = values[(count * 95 + 99) // 100 - 1]
        p99 = values[(count * 99 + 99) // 100 - 1]

        ppm = errors * 1_000_000 // count

        result.append(
            (
                hour,
                f"{hour}:00:00Z {endpoint} {count} "
                f"{p50} {p95} {p99} {errors} {ppm}\n"
            )
        )

    result.sort()

    os.makedirs(out_dir, exist_ok=True)

    output_path = os.path.join(out_dir, OUTPUT_NAME)

    with open(
        output_path,
        "w",
        encoding="ascii",
        newline="\n",
    ) as f:
        f.write("".join(line for _, line in result))


if __name__ == "__main__":
    main()