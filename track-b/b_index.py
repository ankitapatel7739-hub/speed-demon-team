#!/usr/bin/env python3.13
"""Naive reference for Speed Demon 2026, Track B (Index).

    python3.13 reference/b_index.py [--data /data] [--out /out]

Reads docs.txt and queries.txt from the data directory and writes results.txt
to the output directory. The rules are in specs/b-index.md.

This is the obvious implementation, kept slow on purpose: a Python dict from
every term to the set of documents containing it, boolean operators applied
left to right with Python set operations, and phrases checked by
re-tokenizing every candidate document. It is single-threaded and keeps no
positions. Its output is the correctness gate, so it is exact, not fast.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import heapq
from functools import lru_cache

DOCS_FILE = "docs.txt"
QUERIES_FILE = "queries.txt"
RESULTS_FILE = "results.txt"

# A token is a maximal run of ASCII [a-z0-9] after ASCII-lowercasing the bytes.
# bytes.lower() only touches A-Z, so every byte >= 0x80 stays a separator.
TOKEN_RE = re.compile(rb"[a-z0-9]+")
EMPTY: frozenset[int] = frozenset()


def tokenize(text: bytes) -> list[bytes]:
    return TOKEN_RE.findall(text.lower())

def collect_query_terms(queries: list[bytes]) -> set[bytes]:
    needed_terms: set[bytes] = set()

    for line in queries:
        kind, _, rest = line.partition(b" ")

        if kind == b"BOOL":
            words = rest.split(b" ")
            needed_terms.update(words[::2])

        elif kind == b"PHRASE":
            if rest.startswith(b'"') and rest.endswith(b'"'):
                needed_terms.update(rest[1:-1].split(b" "))

    return needed_terms

def load_corpus(path: str, needed_terms: set[bytes]) -> tuple[list[bytes], dict[bytes, set[int]]]:
    """Read docs.txt. Return the raw texts and the term -> doc id set index."""
    texts: list[bytes] = []
    index: dict[bytes, set[int]] = {}
    with open(path, "rb") as f:
        for line in f:
            if line.endswith(b"\n"):
                line = line[:-1]
            id_field, _, text = line.partition(b"\t")
            doc_id = int(id_field)
            if doc_id != len(texts):
                raise ValueError(f"document ids must be 0..N-1 in order, got {doc_id}")
            texts.append(text)
            seen_terms: set[bytes] = set()

            for match in TOKEN_RE.finditer(text.lower()):
                term = match.group(0)

                if term not in needed_terms or term in seen_terms:
                    continue

                seen_terms.add(term)
                index.setdefault(term, set()).add(doc_id)
    return texts, index


def run_bool(index: dict[bytes, set[int]], words: list[bytes]):
    """words is t1 OP t2 OP t3 ...; apply each operator to the running result."""
    result = index.get(words[0], EMPTY)
    for op, term in zip(words[1::2], words[2::2]):
        postings = index.get(term, EMPTY)
        if op == b"AND":
            result = result & postings
        elif op == b"OR":
            result = result | postings
        elif op == b"NOT":
            result = result - postings
        else:
            raise ValueError(f"unknown operator {op!r}")
    return result


@lru_cache(maxsize=32768)
def compile_phrase_pattern(terms: tuple[bytes, ...]):
    pattern = (
        rb"(?<![a-z0-9])"
        + rb"[^a-z0-9]+".join(re.escape(term) for term in terms)
        + rb"(?![a-z0-9])"
    )
    return re.compile(pattern)


def run_phrase(index, texts, terms):
    # Start with the rarest term to reduce candidate documents.
    ordered_terms = sorted(
        terms,
        key=lambda term: len(index.get(term, EMPTY))
    )

    candidates = index.get(ordered_terms[0], EMPTY)

    for term in ordered_terms[1:]:
        candidates = candidates & index.get(term, EMPTY)
        if not candidates:
            return []

    pattern = compile_phrase_pattern(tuple(terms))

    return [
        doc_id
        for doc_id in candidates
        if pattern.search(texts[doc_id].lower())
    ]


def format_result(doc_ids):
    count = len(doc_ids)
    total = sum(doc_ids)

    if count <= 100:
        smallest = sorted(doc_ids)
    else:
        smallest = heapq.nsmallest(10, doc_ids)

    return " ".join(
        str(x) for x in [count, total] + smallest[:10]
    )


def answer(index, texts, line: bytes) -> str:
    kind, _, rest = line.partition(b" ")
    if kind == b"BOOL":
        return format_result(run_bool(index, rest.split(b" ")))
    if kind == b"PHRASE":
        if not (rest.startswith(b'"') and rest.endswith(b'"')):
            raise ValueError(f"bad phrase query {line!r}")
        return format_result(run_phrase(index, texts, rest[1:-1].split(b" ")))
    raise ValueError(f"bad query {line!r}")


def main(data_dir: str, out_dir: str) -> None:
    queries: list[bytes] = []

    with open(os.path.join(data_dir, QUERIES_FILE), "rb") as f:
        for line in f:
            if line.endswith(b"\n"):
                line = line[:-1]
            queries.append(line)

    needed_terms = collect_query_terms(queries)

    texts, index = load_corpus(
        os.path.join(data_dir, DOCS_FILE),
        needed_terms
    )

    results = [
        answer(index, texts, line)
        for line in queries
    ]

    os.makedirs(out_dir, exist_ok=True)

    with open(
        os.path.join(out_dir, RESULTS_FILE),
        "w",
        encoding="ascii",
        newline="\n"
    ) as f:
        f.write("".join(r + "\n" for r in results))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Track B (Index) naive reference.")
    parser.add_argument("--data", default="/data")
    parser.add_argument("--out", default="/out")
    args = parser.parse_args()
    main(args.data, args.out)
    sys.exit(0)
