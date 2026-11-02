# Track D: Traverse

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/d-traverse.md).

## Approach

The program maps `graph.txt` into memory and parses it with several threads, each taking a newline-aligned chunk (a first pass counts lines per chunk so every thread writes straight into its own slice of the edge arrays). From the edge list it builds a forward and a reverse CSR graph with packed 8-byte `{head, weight}` arcs and 32-bit offsets. Because node ids are randomly relabelled, it then renumbers the nodes in BFS order, so the nodes a search touches sit close together in memory and stay in cache.

Each query is answered by bidirectional Dijkstra over the two CSR graphs, using monotone radix heaps instead of binary heaps. The two searches are balanced by how many nodes each has settled, which keeps the work bounded by about twice the cheaper one-directional search even when one side is much denser. Queries with an impossible source or target (no out-edges, no in-edges) are answered in O(1). Distances are 64-bit, and per-thread search state is reset through a touched-node list rather than an O(n) clear. Queries are shared between threads through an atomic counter, and all answers are formatted into a single buffer and written once.

## Dependencies

None.

## With another 24 hours

Parallelise the BFS renumbering, try a landmark (ALT) lower bound for the long queries, and pack heap keys into a single 64-bit word.
