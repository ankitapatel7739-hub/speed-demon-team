# Track D: performance log
Every step from the reference to your final version, each with the live-board run ID that measured it. Judges check that the run IDs exist and that each change moved the way you say it did.
| Step | Change | Run ID | T (s) | M (MB) | Points |
|---|---|---|---|---|---|
| 0 | The reference, unchanged | | 72.69 | 893 | 0 |
| 1 | Initial C++ conversion: 2-pass parallel parsing, serial BFS reordering, bidirectional Dijkstra with radix heap | | 4.40 | 412 | 45.2 |
| 2 | Optimized C++ pipeline: single-pass lock-free parsing & degree counting, concurrent CSR scatter, parallel CSR renumbering, branch pruning (`nd >= best`), and native compilation (`-march=native -flto`) | | 2.06 | 328 | 58.6 |