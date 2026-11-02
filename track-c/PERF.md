# Track C: performance log

Every step from the reference to your final version, each with the live-board run ID that measured it.

| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 14.44 | 367 | 0 |
| 1 | Optimized C++ CSV parsing, parallel aggregation, compact country lookup, and hash-based distinct-name tracking | Pending | Pending | Pending | Pending |

## Step 1: Implementation

- Replaced the Python execution path with a compiled C++ implementation.
- Used buffered file parsing for large CSV inputs.
- Added parallel processing and per-thread aggregation.
- Used compact country lookup and hash-based distinct-name tracking.
- Merged partial aggregation results before writing sorted output.

## Local validation

| Dataset | Records | Expected SHA-256 | Result |
|---|---:|---|---|
| Sample | 332,000 | 9035fed36823232dd41ae2d1b9d6d21aa7c73f49d8e95647e953a89d5ebe9a66 | PASS |
| Full | 3,320,000 | d8088b1cd42ba1338b1345b107b55db7b96ef1f5066f3ff64fd26a8912a0f536 | PASS |

- Docker image size: 176 MB
- Sample local runtime: 1.93 seconds, including container startup.
- Full local runtime: 3.05 seconds, including container startup.
- Both outputs match their official expected SHA-256 hashes.

Note: Local runtimes are not official leaderboard measurements.
Update Run ID, T, M, and Points after obtaining an official leaderboard result.