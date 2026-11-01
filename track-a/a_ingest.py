#!/usr/bin/env python3
"""Speed Demon 2026 - Track A: Ingest."""

import os

INPUT_NAME = "requests.log"
OUTPUT_NAME = "summary.txt"


def main(data_dir="/data", out_dir="/out"):
    groups = {}

    input_path = os.path.join(data_dir, INPUT_NAME)

    # Binary mode avoids decoding 3.1 million lines into Python strings.
    with open(input_path, "rb", buffering=8 * 1024 * 1024) as f:
        for line in f:
            # Split into exactly the five fields.
            # Input format:
            # timestamp method endpoint status latency\n
            ts, method, endpoint, status, latency = line.split()

            # The first 13 bytes are YYYY-MM-DDTHH.
            hour = ts[:13]

            # Bytes are fast hashable dictionary keys.
            key = (hour, endpoint)

            group = groups.get(key)

            if group is None:
                # [latencies, errors]
                group = [[], 0]
                groups[key] = group

            group[0].append(int(latency))

            # Status is always exactly 3 digits.
            if status[0] == 53:  # ASCII '5'
                group[1] += 1

    # Sorting bytes gives exactly the required byte ordering.
    items = sorted(groups.items())

    output = []

    for (hour, endpoint), (values, errors) in items:
        values.sort()

        count = len(values)

        # Nearest-rank percentiles.
        p50 = values[(50 * count + 99) // 100 - 1]
        p95 = values[(95 * count + 99) // 100 - 1]
        p99 = values[(99 * count + 99) // 100 - 1]

        ppm = errors * 1_000_000 // count

        output.append(
            hour
            + b":00:00Z "
            + endpoint
            + b" "
            + str(count).encode()
            + b" "
            + str(p50).encode()
            + b" "
            + str(p95).encode()
            + b" "
            + str(p99).encode()
            + b" "
            + str(errors).encode()
            + b" "
            + str(ppm).encode()
            + b"\n"
        )

    os.makedirs(out_dir, exist_ok=True)

    output_path = os.path.join(out_dir, OUTPUT_NAME)

    with open(output_path, "wb", buffering=8 * 1024 * 1024) as f:
        f.write(b"".join(output))


if __name__ == "__main__":
    main()