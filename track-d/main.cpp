// Speed Demon 2026 - Track D: Traverse  (C++17, no dependencies)
//
// Pipeline:
//   1. mmap graph.txt, parse it in parallel (newline-aligned chunks).
//   2. Build forward + reverse CSR (packed {node, weight} edges, 32-bit offsets).
//   3. Renumber nodes in BFS order so a search ball is contiguous in memory
//      (input ids are randomly relabelled, so this is a big cache win).
//   4. Answer queries in parallel with bidirectional Dijkstra
//      (per-thread state, reset by touched-list instead of O(n) clears).
//   5. Format all answers into one buffer and write once.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t i64;

static const u64 INF = ~0ULL;

// ---------------------------------------------------------------- file mapping
struct Mapped {
    const char* p = nullptr;
    size_t len = 0;
    bool mapped = false;
    vector<char> fallback;
};

static bool map_file(const string& path, Mapped& m) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return false; }
    m.len = (size_t)st.st_size;
    if (m.len == 0) { close(fd); m.p = ""; return true; }
    void* a = mmap(nullptr, m.len, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
    if (a == MAP_FAILED) {
        // fallback: read()
        m.fallback.resize(m.len);
        size_t got = 0;
        while (got < m.len) {
            ssize_t r = read(fd, m.fallback.data() + got, m.len - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        close(fd);
        m.p = m.fallback.data();
        return true;
    }
    madvise(a, m.len, MADV_SEQUENTIAL);
    close(fd);
    m.p = (const char*)a;
    m.mapped = true;
    return true;
}

static void unmap_file(Mapped& m) {
    if (m.mapped) munmap((void*)m.p, m.len);
    m.p = nullptr;
    m.mapped = false;
    vector<char>().swap(m.fallback);
}

static int cpu_count() {
    cpu_set_t set;
    CPU_ZERO(&set);
    int n = 0;
    if (sched_getaffinity(0, sizeof(set), &set) == 0) n = CPU_COUNT(&set);
    if (n <= 0) n = (int)thread::hardware_concurrency();
    if (n <= 0) n = 1;
    return n;
}

// Parse one unsigned integer starting at p (skipping non-digits). Returns new p.
static inline const char* parse_u(const char* p, const char* end, u64& out) {
    while (p < end && (unsigned)(*p - '0') > 9u) ++p;
    u64 v = 0;
    while (p < end && (unsigned)(*p - '0') <= 9u) {
        v = v * 10 + (u64)(*p - '0');
        ++p;
    }
    out = v;
    return p;
}

// ---------------------------------------------------------------- graph
struct Arc { u32 to, w; };

struct CSR {
    vector<u32> start;  // n+1
    vector<Arc> arcs;   // m
};

struct EdgeList { vector<u32> u, v, w; };

template <class F>
static void run_parallel(int T, F f) {
    if (T <= 1) { f(0); return; }
    vector<thread> th;
    th.reserve(T - 1);
    for (int i = 1; i < T; ++i) th.emplace_back(f, i);
    f(0);
    for (auto& t : th) t.join();
}

// Build one CSR direction from the edge list.  forward: key=u, to=v.
static void build_csr(const EdgeList& e, u32 n, size_t m, bool forward, CSR& g) {
    const u32* key = forward ? e.u.data() : e.v.data();
    const u32* oth = forward ? e.v.data() : e.u.data();
    const u32* wt = e.w.data();
    g.start.assign((size_t)n + 1, 0);
    for (size_t i = 0; i < m; ++i) ++g.start[key[i] + 1];
    for (u32 i = 0; i < n; ++i) g.start[i + 1] += g.start[i];
    g.arcs.resize(m);
    vector<u32> pos(g.start.begin(), g.start.begin() + n);
    for (size_t i = 0; i < m; ++i) {
        u32 p = pos[key[i]]++;
        g.arcs[p] = {oth[i], wt[i]};
    }
}

// Read graph.txt into an edge list (parallel parse).
static bool read_graph(const string& path, u32& n, size_t& m, EdgeList& e, int T) {
    Mapped mf;
    if (!map_file(path, mf)) return false;
    const char* base = mf.p;
    const char* end = base + mf.len;

    u64 a, b;
    const char* p = parse_u(base, end, a);
    p = parse_u(p, end, b);
    n = (u32)a;
    size_t mh = (size_t)b;
    // move to start of first edge line
    while (p < end && *p != '\n') ++p;
    if (p < end) ++p;
    const char* body = p;

    size_t blen = (size_t)(end - body);
    if (blen < (1u << 20)) T = 1;

    // newline-aligned chunk boundaries
    vector<const char*> cut(T + 1);
    cut[0] = body;
    cut[T] = end;
    for (int i = 1; i < T; ++i) {
        const char* c = body + blen / T * i;
        if (c < cut[i - 1]) c = cut[i - 1];
        while (c < end && *c != '\n') ++c;
        if (c < end) ++c;
        cut[i] = c;
    }

    // pass 1: count lines per chunk
    vector<size_t> cnt(T, 0);
    run_parallel(T, [&](int t) {
        size_t c = 0;
        const char* q = cut[t];
        const char* qe = cut[t + 1];
        while (q < qe) {
            const char* nl = (const char*)memchr(q, '\n', (size_t)(qe - q));
            if (!nl) { ++c; break; }  // last line without trailing newline
            ++c;
            q = nl + 1;
        }
        cnt[t] = c;
    });
    vector<size_t> off(T + 1, 0);
    for (int t = 0; t < T; ++t) off[t + 1] = off[t] + cnt[t];
    m = off[T];
    if (m > mh) m = mh;  // header is authoritative upper bound
    (void)mh;

    e.u.resize(m);
    e.v.resize(m);
    e.w.resize(m);

    // pass 2: parse
    run_parallel(T, [&](int t) {
        const char* q = cut[t];
        const char* qe = cut[t + 1];
        size_t i = off[t];
        size_t lim = min(off[t + 1], m);
        while (q < qe && i < lim) {
            u64 x, y, z;
            q = parse_u(q, qe, x);
            q = parse_u(q, qe, y);
            q = parse_u(q, qe, z);
            e.u[i] = (u32)x;
            e.v[i] = (u32)y;
            e.w[i] = (u32)z;
            ++i;
            while (q < qe && *q != '\n') ++q;
            if (q < qe) ++q;
        }
    });
    unmap_file(mf);
    return true;
}

// BFS order over the undirected skeleton; returns perm[old] = new.
static void bfs_order(const CSR& f, const CSR& r, u32 n, vector<u32>& perm) {
    perm.assign(n, ~0u);
    vector<u32> order;
    order.reserve(n);
    for (u32 s = 0; s < n; ++s) {
        if (perm[s] != ~0u) continue;
        perm[s] = (u32)order.size();
        order.push_back(s);
        size_t head = order.size() - 1;
        while (head < order.size()) {
            u32 u = order[head++];
            for (u32 i = f.start[u]; i < f.start[u + 1]; ++i) {
                u32 v = f.arcs[i].to;
                if (perm[v] == ~0u) { perm[v] = (u32)order.size(); order.push_back(v); }
            }
            for (u32 i = r.start[u]; i < r.start[u + 1]; ++i) {
                u32 v = r.arcs[i].to;
                if (perm[v] == ~0u) { perm[v] = (u32)order.size(); order.push_back(v); }
            }
        }
    }
}

// Rebuild a CSR under a node permutation (perm[old]=new) with inv[new]=old.
static void renumber(const CSR& g, const vector<u32>& perm, const vector<u32>& inv, u32 n, CSR& out) {
    out.start.assign((size_t)n + 1, 0);
    out.arcs.resize(g.arcs.size());
    u32 pos = 0;
    for (u32 ni = 0; ni < n; ++ni) {
        u32 o = inv[ni];
        out.start[ni] = pos;
        for (u32 i = g.start[o]; i < g.start[o + 1]; ++i)
            out.arcs[pos++] = {perm[g.arcs[i].to], g.arcs[i].w};
    }
    out.start[n] = pos;
}

// ---------------------------------------------------------------- search
struct Item {
    u64 d;
    u32 v;
};

// Monotone radix heap (keys never smaller than the last popped key).
struct RadixHeap {
    vector<Item> b[65];
    u64 last = 0;
    size_t sz = 0;

    void clear() {
        for (int i = 0; i < 65; ++i) b[i].clear();
        last = 0;
        sz = 0;
    }
    static inline int bucket(u64 key, u64 last) {
        return key == last ? 0 : 64 - __builtin_clzll(key ^ last);
    }
    inline void push(u64 key, u32 v) {
        b[bucket(key, last)].push_back({key, v});
        ++sz;
    }
    // make bucket 0 hold the minimum keys; requires sz > 0
    inline void normalize() {
        if (!b[0].empty()) return;
        int i = 1;
        while (b[i].empty()) ++i;
        vector<Item>& src = b[i];
        u64 mn = src[0].d;
        for (size_t k = 1; k < src.size(); ++k) if (src[k].d < mn) mn = src[k].d;
        last = mn;
        for (const Item& it : src) b[bucket(it.d, last)].push_back(it);
        src.clear();
    }
};

struct Searcher {
    const CSR* F;
    const CSR* R;
    u32 n;
    vector<u64> df, db;
    vector<u32> touched_f, touched_b;
    RadixHeap hf, hb;

    Searcher(const CSR* f, const CSR* r, u32 n_) : F(f), R(r), n(n_), df(n_, INF), db(n_, INF) {
        touched_f.reserve(1 << 16);
        touched_b.reserve(1 << 16);
    }

    // Discard stale tops. Returns true if a live minimum is available in b[0].
    static inline bool clean(RadixHeap& h, const vector<u64>& dist) {
        while (h.sz) {
            h.normalize();
            const Item& it = h.b[0].back();
            if (it.d == dist[it.v]) return true;
            h.b[0].pop_back();
            --h.sz;
        }
        return false;
    }

    i64 query(u32 s, u32 t) {
        if (s == t) return 0;
        if (F->start[s] == F->start[s + 1]) return -1;
        if (R->start[t] == R->start[t + 1]) return -1;

        hf.clear();
        hb.clear();
        df[s] = 0; touched_f.push_back(s);
        db[t] = 0; touched_b.push_back(t);
        hf.push(0, s);
        hb.push(0, t);

        u64 best = INF;
        size_t cf = 0, cb = 0;  // settled counts: expand the side that has done less work
        const Arc* fa = F->arcs.data();
        const u32* fs = F->start.data();
        const Arc* ra = R->arcs.data();
        const u32* rs = R->start.data();

        for (;;) {
            if (!clean(hf, df) || !clean(hb, db)) break;
            u64 minf = hf.last, minb = hb.last;
            if (best != INF && minf + minb >= best) break;

            if (cf <= cb) {
                ++cf;
                Item it = hf.b[0].back();
                hf.b[0].pop_back();
                --hf.sz;
                u32 u = it.v;
                u64 d = it.d;
                for (u32 i = fs[u], e = fs[u + 1]; i < e; ++i) {
                    u32 v = fa[i].to;
                    u64 nd = d + fa[i].w;
                    u64 old = df[v];
                    if (nd < old) {
                        if (old == INF) touched_f.push_back(v);
                        df[v] = nd;
                        hf.push(nd, v);
                        u64 ob = db[v];
                        if (ob != INF && nd + ob < best) best = nd + ob;
                    }
                }
            } else {
                ++cb;
                Item it = hb.b[0].back();
                hb.b[0].pop_back();
                --hb.sz;
                u32 u = it.v;
                u64 d = it.d;
                for (u32 i = rs[u], e = rs[u + 1]; i < e; ++i) {
                    u32 v = ra[i].to;
                    u64 nd = d + ra[i].w;
                    u64 old = db[v];
                    if (nd < old) {
                        if (old == INF) touched_b.push_back(v);
                        db[v] = nd;
                        hb.push(nd, v);
                        u64 of = df[v];
                        if (of != INF && nd + of < best) best = nd + of;
                    }
                }
            }
        }

        for (u32 v : touched_f) df[v] = INF;
        for (u32 v : touched_b) db[v] = INF;
        touched_f.clear();
        touched_b.clear();
        return best == INF ? -1 : (i64)best;
    }
};

// ---------------------------------------------------------------- main
static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
#define TICK(name) if(getenv("SD_TIME")) fprintf(stderr,"%-10s %.3f\n",name,now()-t0)
int main(int argc, char** argv) {
    double t0=now();
    string data_dir = "/data", out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
    }

    int T = min(cpu_count(), 8);
    if (const char* e = getenv("SD_THREADS")) T = max(1, atoi(e));

    u32 n = 0;
    size_t m = 0;
    EdgeList el;
    if (!read_graph(data_dir + "/graph.txt", n, m, el, T)) {
        fprintf(stderr, "Cannot open %s/graph.txt\n", data_dir.c_str());
        return 1;
    }

    TICK("parse");
    // forward and reverse CSR built concurrently
    CSR gf, gr;
    {
        thread t([&] { build_csr(el, n, m, false, gr); });
        build_csr(el, n, m, true, gf);
        t.join();
    }
    vector<u32>().swap(el.u);
    vector<u32>().swap(el.v);
    vector<u32>().swap(el.w);

    TICK("csr");
    // locality: renumber nodes in BFS order
    vector<u32> perm, inv;
    bfs_order(gf, gr, n, perm);
    inv.assign(n, 0);
    for (u32 o = 0; o < n; ++o) inv[perm[o]] = o;
    CSR nf, nr;
    {
        thread t([&] { renumber(gr, perm, inv, n, nr); });
        renumber(gf, perm, inv, n, nf);
        t.join();
    }
    vector<Arc>().swap(gf.arcs);
    vector<u32>().swap(gf.start);
    vector<Arc>().swap(gr.arcs);
    vector<u32>().swap(gr.start);
    vector<u32>().swap(inv);

    TICK("reorder");
    // queries
    vector<u32> qs, qt;
    {
        Mapped mq;
        if (!map_file(data_dir + "/queries.txt", mq)) {
            fprintf(stderr, "Cannot open %s/queries.txt\n", data_dir.c_str());
            return 1;
        }
        const char* p = mq.p;
        const char* end = mq.p + mq.len;
        for (;;) {
            while (p < end && (unsigned)(*p - '0') > 9u) ++p;
            if (p >= end) break;
            u64 s, t;
            p = parse_u(p, end, s);
            p = parse_u(p, end, t);
            qs.push_back(perm[(u32)s]);
            qt.push_back(perm[(u32)t]);
        }
        unmap_file(mq);
    }
    size_t Q = qs.size();
    vector<i64> ans(Q, -1);

    // Dispatch: heaviest queries are unknown in advance, so use a shared counter.
    int QT = (int)min<size_t>((size_t)T, max<size_t>(1, Q));
    atomic<size_t> next(0);
    run_parallel(QT, [&](int) {
        Searcher S(&nf, &nr, n);
        for (;;) {
            size_t i = next.fetch_add(1, memory_order_relaxed);
            if (i >= Q) break;
            ans[i] = S.query(qs[i], qt[i]);
        }
    });

    TICK("queries");
    // output
    string outbuf;
    outbuf.reserve(Q * 12 + 16);
    char tmp[24];
    for (size_t i = 0; i < Q; ++i) {
        i64 v = ans[i];
        if (v < 0) {
            outbuf += "-1\n";
        } else {
            int l = 0;
            u64 x = (u64)v;
            if (x == 0) tmp[l++] = '0';
            while (x) { tmp[l++] = (char)('0' + x % 10); x /= 10; }
            while (l) outbuf.push_back(tmp[--l]);
            outbuf.push_back('\n');
        }
    }

    string cmd_dir = out_dir;
    mkdir(cmd_dir.c_str(), 0777);  // ok if it already exists
    string path = out_dir + "/distances.txt";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        // maybe parent dirs missing
        string mk = "mkdir -p '" + out_dir + "'";
        if (system(mk.c_str()) != 0) {}
        f = fopen(path.c_str(), "wb");
        if (!f) { fprintf(stderr, "Cannot write %s\n", path.c_str()); return 1; }
    }
    fwrite(outbuf.data(), 1, outbuf.size(), f);
    fclose(f);
    return 0;
}
