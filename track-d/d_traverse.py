#!/usr/bin/env python3
"""Speed Demon 2026 - Track D: Traverse."""

import argparse
import heapq
import os


def read_graph(path):
    with open(path, "r", encoding="ascii", buffering=1024 * 1024) as f:
        n, m = map(int, f.readline().split())
        adj = [[] for _ in range(n)]

        for line in f:
            u, v, w = map(int, line.split())
            adj[u].append((v, w))

    return adj


def read_queries(path):
    with open(path, "r", encoding="ascii", buffering=1024 * 1024) as f:
        return [tuple(map(int, line.split())) for line in f]


def dijkstra_targets(adj, source, targets):
    """
    Run Dijkstra from source and stop when every required target
    has been settled.
    """

    n = len(adj)
    dist = [-1] * n
    dist[source] = 0

    heap = [(0, source)]

    remaining = set(targets)
    results = {}

    # Handle source itself as a target
    if source in remaining:
        results[source] = 0
        remaining.remove(source)

    while heap and remaining:
        d, u = heapq.heappop(heap)

        if d != dist[u]:
            continue

        # We reached a required target
        if u in remaining:
            results[u] = d
            remaining.remove(u)

            if not remaining:
                break

        for v, w in adj[u]:
            nd = d + w

            if dist[v] == -1 or nd < dist[v]:
                dist[v] = nd
                heapq.heappush(heap, (nd, v))

    return results


def main(data_dir, out_dir):
    adj = read_graph(os.path.join(data_dir, "graph.txt"))
    queries = read_queries(os.path.join(data_dir, "queries.txt"))

    # Group queries by source
    grouped = {}

    for index, (source, target) in enumerate(queries):
        if source not in grouped:
            grouped[source] = []

        grouped[source].append((index, target))

    answers = [-1] * len(queries)

    # Run Dijkstra once per source,
    # stopping after all required targets are found
    for source, items in grouped.items():

        targets = {target for _, target in items}

        results = dijkstra_targets(
            adj,
            source,
            targets
        )

        for index, target in items:
            answers[index] = results.get(target, -1)

    os.makedirs(out_dir, exist_ok=True)

    output_path = os.path.join(out_dir, "distances.txt")

    with open(
        output_path,
        "w",
        encoding="ascii",
        newline="\n",
    ) as f:
        f.write("\n".join(map(str, answers)))

        if answers:
            f.write("\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--data",
        default="/data"
    )

    parser.add_argument(
        "--out",
        default="/out"
    )

    args = parser.parse_args()

    main(args.data, args.out)