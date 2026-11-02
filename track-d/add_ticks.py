with open(r'C:\Users\USER\Desktop\speed-demon-team\speed-demon-starter\track-d\main_optimized.cpp') as f:
    s = f.read()
ticks = [
    ('munmap((void*)gdata, glen);', 'munmap((void*)gdata, glen);\n    if (getenv("SD_TIME")) fprintf(stderr, "parse: %.3f\\n", now() - t0);'),
    ('delete[] deg_in;', 'delete[] deg_in;\n    if (getenv("SD_TIME")) fprintf(stderr, "csr: %.3f\\n", now() - t0);'),
    ('vector<u32>().swap(inv);', 'vector<u32>().swap(inv);\n    if (getenv("SD_TIME")) fprintf(stderr, "reorder: %.3f\\n", now() - t0);'),
    ('munmap((void*)qdata, qlen);', 'munmap((void*)qdata, qlen);\n    if (getenv("SD_TIME")) fprintf(stderr, "qload: %.3f\\n", now() - t0);'),
    ('atomic<size_t> next_q(0);', 'atomic<size_t> next_q(0); double tq = now();'),
    ('fwrite(outbuf.data(), 1, outbuf.size(), f);', 'if (getenv("SD_TIME")) fprintf(stderr, "queries: %.3f\\n", now() - tq);\n    fwrite(outbuf.data(), 1, outbuf.size(), f);')
]
for o, n in ticks:
    assert o in s, f'{o} not in s'
    s = s.replace(o, n, 1)
with open(r'C:\Users\USER\Desktop\speed-demon-team\speed-demon-starter\track-d\main_timed.cpp', 'w') as f:
    f.write(s)
print('timed written successfully')