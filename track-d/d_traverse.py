#!/usr/bin/env python3
"""Speed Demon 2026 - Track D: Traverse."""

import argparse
import heapq
import os


def read_graph(path):
    with open(path, "r", encoding="ascii") as f:
        n, m = map(int, f.readline().split())
        adj = [[] for _ in range(n)]

        for line in f:
            u, v, w = map(int, line.split())
            adj[u].append((v, w))

    return adj


def read_queries(path):
    with open(path, "r", encoding="ascii") as f:
        return [tuple(map(int, line.split())) for line in f]


def dijkstra(adj, source):
    n = len(adj)
    dist = [-1] * n
    dist[source] = 0

    heap = [(0, source)]

    while heap:
        d, u = heapq.heappop(heap)

        if d != dist[u]:
            continue

        for v, w in adj[u]:
            nd = d + w

            if dist[v] == -1 or nd < dist[v]:
                dist[v] = nd
                heapq.heappush(heap, (nd, v))

    return dist


def main(data_dir, out_dir):
    adj = read_graph(os.path.join(data_dir, "graph.txt"))
    queries = read_queries(os.path.join(data_dir, "queries.txt"))

    # Group queries by source
    grouped = {}

    for index, (s, t) in enumerate(queries):
        if s not in grouped:
            grouped[s] = []

        grouped[s].append((index, t))

    answers = [-1] * len(queries)

    # Run Dijkstra once for each unique source
    for source, items in grouped.items():
        dist = dijkstra(adj, source)

        for index, target in items:
            answers[index] = dist[target]

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