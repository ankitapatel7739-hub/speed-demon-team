#!/usr/bin/env python3
"""Speed Demon 2026 - Track D: Traverse.

Bidirectional Dijkstra for shortest directed paths.
"""

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

    return n, adj, rev, queries


def shortest_path_bidirectional(adj, rev, source, target):
    """Bidirectional Dijkstra."""

    if source == target:
        return 0

    # Distance from source
    dist_f = {source: 0}

    # Distance backwards from target
    dist_b = {target: 0}

    heap_f = [(0, source)]
    heap_b = [(0, target)]

    best = None

    while heap_f and heap_b:

        # Current lower bound
        min_f = heap_f[0][0]
        min_b = heap_b[0][0]

        # If no better path is possible
        if best is not None and min_f + min_b >= best:
            break

        # Expand the smaller frontier
        if min_f <= min_b:

            d, u = heapq.heappop(heap_f)

            if dist_f.get(u) != d:
                continue

            # Searches meet
            other = dist_b.get(u)
            if other is not None:
                candidate = d + other

                if best is None or candidate < best:
                    best = candidate

            for v, w in adj[u]:

                nd = d + w
                old = dist_f.get(v)

                if old is None or nd < old:
                    dist_f[v] = nd
                    heapq.heappush(heap_f, (nd, v))

                    other = dist_b.get(v)

                    if other is not None:
                        candidate = nd + other

                        if best is None or candidate < best:
                            best = candidate

        else:

            d, u = heapq.heappop(heap_b)

            if dist_b.get(u) != d:
                continue

            # Searches meet
            other = dist_f.get(u)

            if other is not None:
                candidate = d + other

                if best is None or candidate < best:
                    best = candidate

            # Reverse graph traversal
            for v, w in rev[u]:

                nd = d + w
                old = dist_b.get(v)

                if old is None or nd < old:
                    dist_b[v] = nd
                    heapq.heappush(heap_b, (nd, v))

                    other = dist_f.get(v)

                    if other is not None:
                        candidate = nd + other

                        if best is None or candidate < best:
                            best = candidate

    return -1 if best is None else best


def main(data_dir="/data", out_dir="/out"):

    graph_path = os.path.join(data_dir, "graph.txt")
    queries_path = os.path.join(data_dir, "queries.txt")

    n, adj, rev, queries = read_input(
        graph_path,
        queries_path
    )

    answers = [-1] * len(queries)

    # Avoid calculating identical queries repeatedly
    unique = {}

    for index, (source, target) in enumerate(queries):
        key = (source, target)

        if key not in unique:
            unique[key] = []

        unique[key].append(index)

    # Solve each unique query once
    for (source, target), indexes in unique.items():

        distance = shortest_path_bidirectional(
            adj,
            rev,
            source,
            target
        )

        for index in indexes:
            answers[index] = distance

    os.makedirs(out_dir, exist_ok=True)

    output_path = os.path.join(
        out_dir,
        "distances.txt"
    )

    with open(
        output_path,
        "w",
        encoding="ascii",
        newline="\n"
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