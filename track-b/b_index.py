#!/usr/bin/env python3
import argparse, gc, os, sys, time
from array import array
from bisect import bisect_left
from multiprocessing import Pool

T = bytearray(b" " * 256)
for c in b"0123456789abcdefghijklmnopqrstuvwxyz":
    T[c] = c
for c in range(65, 91):
    T[c] = c + 32
T = bytes(T)
NEEDED = frozenset()


def init(needed):
    global NEEDED
    NEEDED = needed


def scan(job):
    path, a, b = job
    needed = NEEDED
    post = {t: array("I") for t in needed}
    pos = 0
    with open(path, "rb", buffering=1 << 20) as f:
        if a:
            f.seek(a - 1)
            pos = a - 1 + len(f.readline())
        for line in f:
            if pos >= b:
                break
            pos += len(line)
            idf, _, text = line.partition(b"\t")
            doc = int(idf)
            for t in needed.intersection(text.translate(T).split()):
                post[t].append(doc)
    return {t: p for t, p in post.items() if p}


def inter(a, b):
    if len(a) > len(b):
        a, b = b, a
    if not a:
        return []
    if len(a) * 24 < len(b):
        out, i, n = [], 0, len(b)
        for x in a:
            i = bisect_left(b, x, i)
            if i == n:
                break
            if b[i] == x:
                out.append(x)
        return out
    return sorted(set(a).intersection(b))


def diff(a, b):
    if not a or not b:
        return a
    if len(a) * 24 < len(b):
        out, i, n = [], 0, len(b)
        for x in a:
            i = bisect_left(b, x, i)
            if i < n and b[i] == x:
                continue
            out.append(x)
        return out
    return sorted(set(a).difference(b))


def union(a, b):
    if not a:
        return b
    if not b:
        return a
    return sorted(set(a).union(b))


def run_bool(post, words):
    if not words:
        return []
    res = post.get(words[0], [])
    for op, term in zip(words[1::2], words[2::2]):
        other = post.get(term, [])
        if op == b"AND":
            res = inter(res, other)
        elif op == b"OR":
            res = union(res, other)
        elif op == b"NOT":
            res = diff(res, other)
        else:
            raise ValueError(f"Unknown operator: {op!r}")
    return res


def fmt(ids):
    return " ".join(map(str, [len(ids), sum(ids), *ids[:10]]))


def main(data_dir, out_dir):
    gc.disable()
    t0 = time.perf_counter()
    docs = os.path.join(data_dir, "docs.txt")
    with open(os.path.join(data_dir, "queries.txt"), "rb") as f:
        queries = [l[:-1] if l.endswith(b"\n") else l for l in f]

    needed, phrases = set(), set()
    for line in queries:
        kind, _, rest = line.partition(b" ")
        if kind == b"BOOL":
            needed.update(rest.split()[::2])
        elif kind == b"PHRASE" and rest.startswith(b'"') and rest.endswith(b'"'):
            terms = tuple(rest[1:-1].split())
            if terms:
                phrases.add(terms)
                needed.update(terms)
    needed = frozenset(needed)

    size = os.path.getsize(docs)
    workers = int(os.environ.get("WORKERS", 1))
    if size < (4 << 20):
        workers = 1
    cuts = [size * i // workers for i in range(workers + 1)]
    jobs = [(docs, cuts[i], cuts[i + 1]) for i in range(workers)]
    if workers == 1:
        init(needed)
        parts = [scan(jobs[0])]
    else:
        with Pool(workers, initializer=init, initargs=(needed,)) as pool:
            parts = pool.map(scan, jobs)
    post = {}
    for p in parts:
        for t, arr in p.items():
            if t in post:
                post[t].extend(arr)
            else:
                post[t] = arr
    t1 = time.perf_counter()

    hits = {terms: [] for terms in phrases}
    needle = {terms: b" " + b" ".join(terms) + b" " for terms in phrases}
    cand = {}
    for terms in phrases:
        lists = [post.get(t) for t in terms]
        if not all(lists):
            continue
        lists.sort(key=len)
        c = lists[0]
        for other in lists[1:]:
            c = inter(c, other)
            if not c:
                break
        for d in c:
            cand.setdefault(d, []).append(terms)
    if cand:
        last = max(cand)
        with open(docs, "rb", buffering=1 << 20) as f:
            for i, line in enumerate(f):
                if i > last:
                    break
                ps = cand.get(i)
                if ps:
                    norm = b" " + b" ".join(
                        line.partition(b"\t")[2].translate(T).split()) + b" "
                    for terms in ps:
                        if needle[terms] in norm:
                            hits[terms].append(i)
    t2 = time.perf_counter()

    os.makedirs(out_dir, exist_ok=True)
    cache, outl = {}, []
    for line in queries:
        r = cache.get(line)
        if r is None:
            kind, _, rest = line.partition(b" ")
            if kind == b"BOOL":
                r = fmt(run_bool(post, rest.split()))
            elif kind == b"PHRASE" and rest.startswith(b'"') and rest.endswith(b'"'):
                r = fmt(hits[tuple(rest[1:-1].split())])
            else:
                raise ValueError(f"Invalid query: {line!r}")
            cache[line] = r
        outl.append(r)
    with open(os.path.join(out_dir, "results.txt"), "w", encoding="ascii",
              newline="\n") as out:
        out.write("\n".join(outl) + "\n")
    if os.environ.get("DEBUG"):
        t3 = time.perf_counter()
        print(f"pass1 {t1-t0:.1f}s  phrases {t2-t1:.1f}s  answers {t3-t2:.1f}s",
              file=sys.stderr)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="/data")
    ap.add_argument("--out", default="/out")
    a = ap.parse_args()
    main(a.data, a.out)