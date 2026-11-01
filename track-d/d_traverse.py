#!/usr/bin/env python3
"""Speed Demon 2026 - Track D: Traverse."""

import argparse
import heapq
import os
from array import array


def read_input(graph_path, queries_path):
    # Read graph into compact CSR representation.
    with open(graph_path, "rb", buffering=1024 * 1024) as f:
        n, m = map(int, f.readline().split())

        us = array("I")
        vs = array("I")
        ws = array("I")

        out_count = array("I", [0]) * n
        in_count = array("I", [0]) * n

        for line in f:
            u, v, w = map(int, line.split())

            us.append(u)
            vs.append(v)
            ws.append(w)

            out_count[u] += 1
            in_count[v] += 1

    # CSR offsets
    out_start = array("Q", [0]) * (n + 1)
    in_start = array("Q", [0]) * (n + 1)

    total = 0
    for i in range(n):
        out_start[i] = total
        total += out_count[i]
    out_start[n] = total

    total = 0
    for i in range(n):
        in_start[i] = total
        total += in_count[i]
    in_start[n] = total

    # Compact forward graph
    out_to = array("I", [0]) * m
    out_w = array("I", [0]) * m

    # Compact reverse graph
    in_to = array("I", [0]) * m
    in_w = array("I", [0]) * m

    out_pos = array("Q", out_start)
    in_pos = array("Q", in_start)

    for i in range(m):
        u = us[i]
        v = vs[i]
        w = ws[i]

        p = out_pos[u]
        out_to[p] = v
        out_w[p] = w
        out_pos[u] = p + 1

        p = in_pos[v]
        in_to[p] = u
        in_w[p] = w
        in_pos[v] = p + 1

    with open(queries_path, "rb", buffering=1024 * 1024) as f:
        queries = [tuple(map(int, line.split())) for line in f]

    return (
        n,
        out_start,
        out_to,
        out_w,
        in_start,
        in_to,
        in_w,
        queries,
    )


class BidirectionalDijkstra:
    def __init__(
        self,
        n,
        out_start,
        out_to,
        out_w,
        in_start,
        in_to,
        in_w,
    ):
        self.n = n

        self.out_start = out_start
        self.out_to = out_to
        self.out_w = out_w

        self.in_start = in_start
        self.in_to = in_to
        self.in_w = in_w

        # Reusable per-query arrays.
        self.dist_f = array("Q", [0]) * n
        self.dist_b = array("Q", [0]) * n

        self.mark_f = array("I", [0]) * n
        self.mark_b = array("I", [0]) * n

        self.query_id = 0

    def shortest_path(self, source, target):
        if source == target:
            return 0

        # Immediate unreachable cases.
        if self.out_start[source] == self.out_start[source + 1]:
            return -1

        if self.in_start[target] == self.in_start[target + 1]:
            return -1

        self.query_id += 1
        qid = self.query_id

        dist_f = self.dist_f
        dist_b = self.dist_b
        mark_f = self.mark_f
        mark_b = self.mark_b

        heap_f = [(0, source)]
        heap_b = [(0, target)]

        dist_f[source] = 0
        mark_f[source] = qid

        dist_b[target] = 0
        mark_b[target] = qid

        best = None

        out_start = self.out_start
        out_to = self.out_to
        out_w = self.out_w

        in_start = self.in_start
        in_to = self.in_to
        in_w = self.in_w

        while heap_f and heap_b:

            # Remove stale forward heap entries.
            while heap_f:
                d, u = heap_f[0]
                if mark_f[u] == qid and dist_f[u] == d:
                    break
                heapq.heappop(heap_f)

            # Remove stale backward heap entries.
            while heap_b:
                d, u = heap_b[0]
                if mark_b[u] == qid and dist_b[u] == d:
                    break
                heapq.heappop(heap_b)

            if not heap_f or not heap_b:
                break

            min_f = heap_f[0][0]
            min_b = heap_b[0][0]

            # No better path can be found.
            if best is not None and min_f + min_b >= best:
                break

            # Expand the smaller frontier.
            if min_f <= min_b:
                d, u = heapq.heappop(heap_f)

                # If backward search has reached u.
                if mark_b[u] == qid:
                    candidate = d + dist_b[u]
                    if best is None or candidate < best:
                        best = candidate

                start = out_start[u]
                end = out_start[u + 1]

                for i in range(start, end):
                    v = out_to[i]
                    nd = d + out_w[i]

                    if mark_f[v] != qid or nd < dist_f[v]:
                        dist_f[v] = nd
                        mark_f[v] = qid
                        heapq.heappush(heap_f, (nd, v))

                        if mark_b[v] == qid:
                            candidate = nd + dist_b[v]
                            if best is None or candidate < best:
                                best = candidate

            else:
                d, u = heapq.heappop(heap_b)

                # If forward search has reached u.
                if mark_f[u] == qid:
                    candidate = d + dist_f[u]
                    if best is None or candidate < best:
                        best = candidate

                start = in_start[u]
                end = in_start[u + 1]

                for i in range(start, end):
                    v = in_to[i]
                    nd = d + in_w[i]

                    if mark_b[v] != qid or nd < dist_b[v]:
                        dist_b[v] = nd
                        mark_b[v] = qid
                        heapq.heappush(heap_b, (nd, v))

                        if mark_f[v] == qid:
                            candidate = nd + dist_f[v]
                            if best is None or candidate < best:
                                best = candidate

        return -1 if best is None else best


def main(data_dir="/data", out_dir="/out"):
    graph_path = os.path.join(data_dir, "graph.txt")
    queries_path = os.path.join(data_dir, "queries.txt")

    (
        n,
        out_start,
        out_to,
        out_w,
        in_start,
        in_to,
        in_w,
        queries,
    ) = read_input(graph_path, queries_path)

    solver = BidirectionalDijkstra(
        n,
        out_start,
        out_to,
        out_w,
        in_start,
        in_to,
        in_w,
    )

    answers = [-1] * len(queries)

    # Solve duplicate queries only once.
    unique = {}

    for index, query in enumerate(queries):
        if query in unique:
            unique[query].append(index)
        else:
            unique[query] = [index]

    for (source, target), indexes in unique.items():
        answer = solver.shortest_path(source, target)

        for index in indexes:
            answers[index] = answer

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