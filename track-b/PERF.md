# Track B: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 56.35 | 2,200 | 0 |
| 1 | Python: index only the terms that appear in queries, speed-up of tokenizing and phrase check | FILL_RUN_ID | 50.7 | 1,614 | 1.694 |
| 2 | Python: sorted arrays instead of sets, phrase candidates from list intersections, garbage collector off | FILL_RUN_ID | 41.0 | 198 | 11.660 |
| 3 | Rewrite in C++: memory-mapped file, multi-threaded read, phrases matched during the read, bitmaps for very common words | FILL_RUN_ID | FILL | FILL | FILL |

## Local verification

- Optimized implementation: `track-b/b_index.cpp`
- Official checker: FILL (PASS / FAIL)
- Docker image size: FILL
- Local checker time: FILL
- Output SHA-256: FILL
- Output matches the reference implementation exactly.
- Leaderboard Run ID: FILL