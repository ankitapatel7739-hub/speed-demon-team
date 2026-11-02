#!/usr/bin/env python3
"""Speed Demon 2026 - Track A: Ingest - optimized."""

import os

INPUT_NAME = "requests.log"
OUTPUT_NAME = "summary.txt"


def main(data_dir="/data", out_dir="/out"):
    groups = {}

    input_path = os.path.join(data_dir, INPUT_NAME)

    # Read as bytes: much faster than decoding every line to Python strings
    with open(input_path, "rb", buffering=1024 * 1024) as f:
        for line in f:
            # Hour = first 13 bytes: YYYY-MM-DDTHH
            hour = line[:13]

            # Format:
            # timestamp METHOD endpoint status latency\n
            #
            # Find spaces without line.split()
            p1 = line.find(b" ")
            p2 = line.find(b" ", p1 + 1)
            p3 = line.find(b" ", p2 + 1)
            p4 = line.find(b" ", p3 + 1)

            endpoint = line[p2 + 1:p3]

            # Status is always 3 digits
            is_error = line[p3 + 1] == 53  # ASCII '5'

            # Parse latency manually
            latency = 0
            for c in line[p4 + 1:-1]:
                latency = latency * 10 + (c - 48)

            key = (hour, endpoint)

            group = groups.get(key)
            if group is None:
                # [latencies, error_count]
                groups[key] = [[latency], 1 if is_error else 0]
            else:
                group[0].append(latency)
                if is_error:
                    group[1] += 1

    out_lines = []

    for (hour, endpoint), group in sorted(groups.items()):
        values = group[0]
        errors = group[1]

        values.sort()
        n = len(values)

        p50 = values[(50 * n + 99) // 100 - 1]
        p95 = values[(95 * n + 99) // 100 - 1]
        p99 = values[(99 * n + 99) // 100 - 1]

        ppm = errors * 1_000_000 // n

        out_lines.append(
            hour + b":00:00Z " +
            endpoint + b" " +
            str(n).encode() + b" " +
            str(p50).encode() + b" " +
            str(p95).encode() + b" " +
            str(p99).encode() + b" " +
            str(errors).encode() + b" " +
            str(ppm).encode() + b"\n"
        )

    os.makedirs(out_dir, exist_ok=True)

    with open(
        os.path.join(out_dir, OUTPUT_NAME),
        "wb",
        buffering=1024 * 1024
    ) as f:
        f.write(b"".join(out_lines))


if __name__ == "__main__":
    main()