#!/usr/bin/env python3
"""Speed Demon 2026 - Track D: Traverse."""

import argparse
import heapq
import os
from array import array


def read_input(graph_path, queries_path):
    # ---------- Read graph ----------
    with open(graph_path, "rb", buffering=1024 * 1024) as f:
        n, m = map(int, f.readline().split())

        # Temporary edge storage
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

    # ---------- Build CSR offsets ----------
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

    # ---------- Build forward/reverse CSR ----------
    out_to = array("I", [0]) * m
    out_w = array("I", [0]) * m

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

    # ---------- Read queries ----------
    with open(queries_path, "rb", buffering=1024 * 1024) as f:
        queries = [tuple(map(int, line.split())) for line in f]

    return (
        n,
        out_start, out_to, out_w,
        in_start, in_to, in_w,
        queries
    )


class Solver:
    def __init__(
        self,
        n,
        out_start, out_to, out_w,
        in_start, in_to, in_w
    ):
        self.n = n

        self.out_start = out_start
        self.out_to = out_to
        self.out_w = out_w

        self.in_start = in_start
        self.in_to = in_to
        self.in_w = in_w

        # Reusable arrays
        self.dist1 = array("Q", [0]) * n
        self.dist2 = array("Q", [0]) * n

        self.mark1 = array("I", [0]) * n
        self.mark2 = array("I", [0]) * n

        self.qid = 0

    def multi_target_dijkstra(self, start, targets, reverse=False):
        """
        Run Dijkstra once and stop when all required targets are settled.
        """

        self.qid += 1
        qid = self.qid

        dist = self.dist1
        mark = self.mark1

        if reverse:
            offsets = self.in_start
            to = self.in_to
            weights = self.in_w
        else:
            offsets = self.out_start
            to = self.out_to
            weights = self.out_w

        remaining = set(targets)
        results = {}

        dist[start] = 0
        mark[start] = qid

        heap = [(0, start)]
        heappush = heapq.heappush
        heappop = heapq.heappop

        while heap and remaining:
            d, u = heappop(heap)

            if mark[u] != qid or dist[u] != d:
                continue

            if u in remaining:
                results[u] = d
                remaining.remove(u)

                if not remaining:
                    break

            begin = offsets[u]
            end = offsets[u + 1]

            for i in range(begin, end):
                v = to[i]
                nd = d + weights[i]

                if mark[v] != qid or nd < dist[v]:
                    mark[v] = qid
                    dist[v] = nd
                    heappush(heap, (nd, v))

        return results

    def bidirectional(self, source, target):
        if source == target:
            return 0

        out_start = self.out_start
        out_to = self.out_to
        out_w = self.out_w

        in_start = self.in_start
        in_to = self.in_to
        in_w = self.in_w

        # Quick impossible cases
        if out_start[source] == out_start[source + 1]:
            return -1

        if in_start[target] == in_start[target + 1]:
            return -1

        self.qid += 1
        qid = self.qid

        dist_f = self.dist1
        dist_b = self.dist2

        mark_f = self.mark1
        mark_b = self.mark2

        dist_f[source] = 0
        mark_f[source] = qid

        dist_b[target] = 0
        mark_b[target] = qid

        heap_f = [(0, source)]
        heap_b = [(0, target)]

        push = heapq.heappush
        pop = heapq.heappop

        best = -1

        while heap_f and heap_b:

            # Remove stale entries
            while heap_f:
                d, u = heap_f[0]
                if mark_f[u] == qid and dist_f[u] == d:
                    break
                pop(heap_f)

            while heap_b:
                d, u = heap_b[0]
                if mark_b[u] == qid and dist_b[u] == d:
                    break
                pop(heap_b)

            if not heap_f or not heap_b:
                break

            min_f = heap_f[0][0]
            min_b = heap_b[0][0]

            if best >= 0 and min_f + min_b >= best:
                break

            # Expand smaller frontier
            if min_f <= min_b:

                d, u = pop(heap_f)

                if mark_b[u] == qid:
                    candidate = d + dist_b[u]
                    if best < 0 or candidate < best:
                        best = candidate

                begin = out_start[u]
                end = out_start[u + 1]

                for i in range(begin, end):
                    v = out_to[i]
                    nd = d + out_w[i]

                    if mark_f[v] != qid or nd < dist_f[v]:
                        mark_f[v] = qid
                        dist_f[v] = nd
                        push(heap_f, (nd, v))

                        if mark_b[v] == qid:
                            candidate = nd + dist_b[v]
                            if best < 0 or candidate < best:
                                best = candidate

            else:

                d, u = pop(heap_b)

                if mark_f[u] == qid:
                    candidate = d + dist_f[u]
                    if best < 0 or candidate < best:
                        best = candidate

                begin = in_start[u]
                end = in_start[u + 1]

                for i in range(begin, end):
                    v = in_to[i]
                    nd = d + in_w[i]

                    if mark_b[v] != qid or nd < dist_b[v]:
                        mark_b[v] = qid
                        dist_b[v] = nd
                        push(heap_b, (nd, v))

                        if mark_f[v] == qid:
                            candidate = nd + dist_f[v]
                            if best < 0 or candidate < best:
                                best = candidate

        return best


def main(data_dir="/data", out_dir="/out"):

    graph_path = os.path.join(data_dir, "graph.txt")
    queries_path = os.path.join(data_dir, "queries.txt")

    (
        n,
        out_start, out_to, out_w,
        in_start, in_to, in_w,
        queries
    ) = read_input(graph_path, queries_path)

    solver = Solver(
        n,
        out_start, out_to, out_w,
        in_start, in_to, in_w
    )

    answers = [-1] * len(queries)

    # Group by source and target
    by_source = {}
    by_target = {}

    # Also remove exact duplicate queries
    unique_queries = {}

    for index, (s, t) in enumerate(queries):
        key = (s, t)

        if key in unique_queries:
            unique_queries[key].append(index)
        else:
            unique_queries[key] = [index]

    unique_items = list(unique_queries.items())

    for item_index, ((s, t), _) in enumerate(unique_items):
        by_source.setdefault(s, []).append(item_index)
        by_target.setdefault(t, []).append(item_index)

    unique_answers = [-1] * len(unique_items)

    # Choose grouping only when it meaningfully reduces searches.
    unique_count = len(unique_items)
    source_count = len(by_source)
    target_count = len(by_target)

    if source_count < unique_count // 2:

        # Many queries share sources
        for source, item_ids in by_source.items():

            targets = [
                unique_items[i][0][1]
                for i in item_ids
            ]

            result = solver.multi_target_dijkstra(
                source,
                targets,
                reverse=False
            )

            for i in item_ids:
                target = unique_items[i][0][1]
                unique_answers[i] = result.get(target, -1)

    elif target_count < unique_count // 2:

        # Many queries share targets: reverse graph
        for target, item_ids in by_target.items():

            sources = [
                unique_items[i][0][0]
                for i in item_ids
            ]

            result = solver.multi_target_dijkstra(
                target,
                sources,
                reverse=True
            )

            for i in item_ids:
                source = unique_items[i][0][0]
                unique_answers[i] = result.get(source, -1)

    else:

        # Mostly unique queries: bidirectional Dijkstra
        for i, ((source, target), _) in enumerate(unique_items):
            unique_answers[i] = solver.bidirectional(source, target)

    # Expand duplicate queries
    for i, (_, indexes) in enumerate(unique_items):
        answer = unique_answers[i]

        for index in indexes:
            answers[index] = answer

    os.makedirs(out_dir, exist_ok=True)

    output_path = os.path.join(out_dir, "distances.txt")

    with open(
        output_path,
        "w",
        encoding="ascii",
        newline="\n",
        buffering=1024 * 1024
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