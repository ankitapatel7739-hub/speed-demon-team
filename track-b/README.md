# Track B: Index

The task, the input files and the exact output format are in the [spec](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/b-index.md).

## Approach

One C++17 program, no dependencies.

1. `queries.txt` is read first. From it we collect every term we will ever need (the BOOL operands and the PHRASE words) and every distinct phrase. Identical query lines are answered once.
2. `docs.txt` is memory-mapped and cut into byte ranges, one per thread. Each thread tokenizes its range (`[a-z0-9]+`, lowercased, using a 256-entry table) and looks every token up in an open-addressing hash table that contains only the needed terms. A hit appends the document id to that term's posting list. Because the ranges are in file order, concatenating the thread results keeps every posting list sorted.
3. The same pass answers PHRASE queries. Adjacent token ids are checked against a small bit filter of word pairs that occur in some phrase, and only then against a hash table of the distinct phrases. A matching document id goes straight into that phrase's posting list, so the text is never read a second time.
4. BOOL queries run left to right on sorted arrays. AND and NOT use a galloping search when one side is much shorter. Terms that appear in more than 1/16 of the documents also get a bitmap, so operations on very common words become word-wise AND/OR/AND-NOT. Queries run in parallel.
5. Each answer is the count, the sum of the matching ids and the ten smallest ids, exactly as the reference prints them.

Output is byte-identical to the reference implementation.

## Dependencies

None.

## With another 24 hours

- Build the posting lists with a counting sort (count per term, then fill) instead of growing vectors, to cut allocation and cache misses in the reading pass.
- A faster tokenizer (SIMD to find word boundaries) and a perfect-hash vocabulary.
- Compressed or block-based posting lists for the medium-frequency terms.
- Tune the thread count against the grader's real core limit.