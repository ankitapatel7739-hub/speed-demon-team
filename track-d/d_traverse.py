#!/usr/bin/env python3
"""Speed Demon 2026 - Track D: Traverse."""

import argparse
import heapq
import os


def read_input(graph_path, queries_path):
    with open(graph_path, "rb", buffering=1024 * 1024) as f:
        n, m = map(int, f.readline().split())

        adj = [[] for _ in range(n)]
        rev = [[] for _ in range(n)]

        for line in f:
            u, v, w = map(int, line.split())
            adj[u].append((v, w))
            rev[v].append((u, w))

    with open(queries_path, "rb", buffering=1024 * 1024) as f:
        queries = [tuple(map(int, line.split())) for line in f]

    return adj, rev, queries


def dijkstra_targets(graph, start, targets):
    """
    Dijkstra that stops as soon as every required target is settled.
    """
    dist = {start: 0}
    heap = [(0, start)]

    remaining = set(targets)
    found = {}

    while heap and remaining:
        d, u = heapq.heappop(heap)

        if dist.get(u) != d:
            continue

        if u in remaining:
            found[u] = d
            remaining.remove(u)

            if not remaining:
                break

        for v, w in graph[u]:
            nd = d + w

            old = dist.get(v)
            if old is None or nd < old:
                dist[v] = nd
                heapq.heappush(heap, (nd, v))

    return found


def main(data_dir, out_dir):
    graph_path = os.path.join(data_dir, "graph.txt")
    queries_path = os.path.join(data_dir, "queries.txt")

    adj, rev, queries = read_input(graph_path, queries_path)

    answers = [-1] * len(queries)

    # Count unique sources and targets
    sources = {}
    targets = {}

    for i, (s, t) in enumerate(queries):
        sources.setdefault(s, []).append((i, t))
        targets.setdefault(t, []).append((i, s))

    # Choose the direction requiring fewer Dijkstra runs
    if len(sources) <= len(targets):

        # Forward search from each unique source
        for source, items in sources.items():

            needed = {target for _, target in items}

            result = dijkstra_targets(
                adj,
                source,
                needed
            )

            for index, target in items:
                answers[index] = result.get(target, -1)

    else:

        # Reverse search from each unique target
        # Distance target -> source in reversed graph
        # equals source -> target in original graph
        for target, items in targets.items():

            needed = {source for _, source in items}

            result = dijkstra_targets(
                rev,
                target,
                needed
            )

            for index, source in items:
                answers[index] = result.get(source, -1)

    os.makedirs(out_dir, exist_ok=True)

    with open(
        os.path.join(out_dir, "distances.txt"),
        "w",
        encoding="ascii",
        newline="\n",
    ) as f:
        f.write("\n".join(map(str, answers)))
        if answers:
            f.write("\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="/data")
    parser.add_argument("--out", default="/out")

    args = parser.parse_args()
    main(args.data, args.out)