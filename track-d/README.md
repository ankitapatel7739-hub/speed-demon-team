# Track D: Traverse
The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/d-traverse.md).
## Approach
The solution implements an end-to-end parallel traversal pipeline optimized for memory bandwidth and cache locality:
1. **Parallel Single-Pass Parsing & Atomic Counting**: Instead of multi-pass line scanning, threads map `graph.txt` and parse newline-aligned chunks in parallel using branchless integer parsing. As edges are parsed, outgoing and incoming node degrees are accumulated in lock-free arrays using relaxed atomic increments, avoiding thread-local degree reduction passes.
2. **Lock-Free Concurrent CSR Construction**: Global forward and backward CSR offset pointers are computed in a single prefix-sum pass. Memory for degree tracking is reused directly for offset tracking, and threads concurrently scatter edges into packed `{to, weight}` arcs using relaxed atomic increments.
3. **Cache-Locality BFS Renumbering**: Because input node IDs are uniformly scrambled, an unweighted forward BFS traverses the graph once, mapping nodes in discovery order into a compact lookup table (`perm`) and inverse order table (`inv`).
4. **Parallel CSR Renumbering**: All worker threads renumber the forward and reverse CSR graphs concurrently across contiguous node partitions, keeping related nodes adjacent in memory for cache-hot traversals.
5. **Bidirectional Dijkstra with Monotone Radix Heaps**: Queries are processed concurrently using thread-local monotone radix heaps ($O(1)$ amortized push/pop). Search frontiers are balanced by settled counts, pruned aggressively against the best-known path distance (`nd >= best`), and reset between queries in $O(\text{touched})$ operations. Impossible sources and targets are checked in $O(1)$.
6. **Zero-Allocation Output Formatting**: Results are formatted directly into a single pre-allocated string buffer and written in one sequential write.
## Dependencies
None (C++17 standard library and POSIX system calls only).
## With another 24 hours
Explore landmark-based A* (ALT) heuristics for long-distance queries and implement SIMD-accelerated bitmask vectorization for radix heap bucket scanning.