# Track A: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 32.05 | 228 | 0 |
| 1 | Optimized C++ implementation: buffered input, interned hour/endpoint identifiers, compact group lookup, and partial selection with `nth_element` for percentiles | Pending | Pending | Pending | Pending |

## Local validation

- Sample dataset: 310,000 records — exact output hash match.
- Full dataset: 3,100,000 records — exact output hash match.
- Full dataset output SHA-256: `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3`
- Docker image size: 176 MB.
- Local full-dataset execution: 2.91 seconds (including container startup; one verification rerun took 4.62 seconds).

*Local timings are not official leaderboard measurements.*

## Approach

The implementation uses C++17 with buffered file input to reduce input overhead. It interns repeated hour and endpoint strings into compact identifiers, groups records using a combined integer key, and uses `std::nth_element` to obtain the required percentile values without fully sorting every latency vector. The final output is sorted by hour and endpoint to preserve the reference format.

## Dependencies

None beyond the C++ standard library and the existing Python runtime wrapper.

## With another 24 hours

Benchmark on the official board, inspect memory usage, profile the hottest operations, and evaluate further optimizations while preserving exact output compatibility.