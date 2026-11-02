// Speed Demon 2026 - Track D: Traverse
// Optimized C++ solution:
// - Parallel single-pass parsing with atomic degree counting
// - Lock-free parallel CSR construction with memory reuse
// - Fast forward BFS renumbering for cache locality
// - Parallel renumbering of CSR across all worker threads
// - Bidirectional Dijkstra with monotone radix heaps and branch pruning

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
typedef uint8_t u8;
typedef int64_t i64;

static const u64 INF = ~0ULL;

struct Arc {
    u32 to, w;
};

struct CSR {
    vector<u32> start;
    vector<Arc> arcs;
};

struct Edge {
    u32 u, v, w;
};

inline u32 parse_u32(const char*& p) {
    u32 v = (u8)*p++ - '0';
    while ((u8)*p >= '0') {
        v = v * 10 + ((u8)*p++ - '0');
    }
    ++p; // skips ' ' or '\n'
    return v;
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

template <class F>
static void run_parallel(int T, F f) {
    if (T <= 1) { f(0); return; }
    vector<thread> th;
    th.reserve(T - 1);
    for (int i = 1; i < T; ++i) th.emplace_back(f, i);
    f(0);
    for (auto& t : th) t.join();
}

struct Item {
    u64 d;
    u32 v;
};

// Monotone radix heap
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

    inline void normalize() {
        if (!b[0].empty()) return;
        int i = 1;
        while (b[i].empty()) ++i;
        vector<Item>& src = b[i];
        u64 mn = src[0].d;
        for (size_t k = 1; k < src.size(); ++k) {
            if (src[k].d < mn) mn = src[k].d;
        }
        last = mn;
        for (const Item& it : src) {
            b[bucket(it.d, last)].push_back(it);
        }
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

    Searcher(const CSR* f, const CSR* r, u32 n_)
        : F(f), R(r), n(n_), df(n_, INF), db(n_, INF) {
        touched_f.reserve(1 << 16);
        touched_b.reserve(1 << 16);
    }

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
        size_t cf = 0, cb = 0;
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
                if (d >= best) continue;

                for (u32 i = fs[u], e = fs[u + 1]; i < e; ++i) {
                    u32 v = fa[i].to;
                    u64 nd = d + fa[i].w;
                    if (nd >= best) continue;
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
                if (d >= best) continue;

                for (u32 i = rs[u], e = rs[u + 1]; i < e; ++i) {
                    u32 v = ra[i].to;
                    u64 nd = d + ra[i].w;
                    if (nd >= best) continue;
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

static double now() {
    return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    double t0 = now();
    string data_dir = "/data", out_dir = "/out";
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
    }

    int T = min(cpu_count(), 8);
    if (const char* e = getenv("SD_THREADS")) T = max(1, atoi(e));

    // Map graph.txt
    string gpath = data_dir + "/graph.txt";
    int fd = open(gpath.c_str(), O_RDONLY);
    if (fd < 0) return 1;
    struct stat st;
    fstat(fd, &st);
    size_t glen = st.st_size;
    const char* gdata = (const char*)mmap(nullptr, glen, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    const char* p = gdata;
    u32 n = parse_u32(p);
    u32 m = parse_u32(p);

    const char* body = p;
    size_t blen = (gdata + glen) - body;

    vector<const char*> cut(T + 1);
    cut[0] = body;
    cut[T] = gdata + glen;
    for (int i = 1; i < T; ++i) {
        const char* c = body + (blen / T) * i;
        while (c < gdata + glen && *c != '\n') ++c;
        if (c < gdata + glen) ++c;
        cut[i] = c;
    }

    atomic<u32>* deg_out = new atomic<u32>[n];
    atomic<u32>* deg_in = new atomic<u32>[n];
    for (u32 i = 0; i < n; ++i) {
        deg_out[i].store(0, memory_order_relaxed);
        deg_in[i].store(0, memory_order_relaxed);
    }
    vector<vector<Edge>> thread_edges(T);

    run_parallel(T, [&](int t) {
        const char* cur = cut[t];
        const char* end = cut[t + 1];
        auto& edges = thread_edges[t];
        edges.reserve(m / T + 10000);
        while (cur < end) {
            u32 u = parse_u32(cur);
            u32 v = parse_u32(cur);
            u32 w = parse_u32(cur);
            edges.push_back({u, v, w});
            deg_out[u].fetch_add(1, memory_order_relaxed);
            deg_in[v].fetch_add(1, memory_order_relaxed);
        }
    });
    munmap((void*)gdata, glen);

    CSR gf, gr;
    gf.start.resize(n + 1);
    gr.start.resize(n + 1);

    atomic<u32>* pos_out = deg_out;
    atomic<u32>* pos_in = deg_in;

    u32 sum_out = 0, sum_in = 0;
    for (u32 i = 0; i < n; ++i) {
        gf.start[i] = sum_out;
        gr.start[i] = sum_in;
        u32 do_cnt = pos_out[i].load(memory_order_relaxed);
        u32 di_cnt = pos_in[i].load(memory_order_relaxed);
        pos_out[i].store(sum_out, memory_order_relaxed);
        pos_in[i].store(sum_in, memory_order_relaxed);
        sum_out += do_cnt;
        sum_in += di_cnt;
    }
    gf.start[n] = sum_out;
    gr.start[n] = sum_in;

    gf.arcs.resize(m);
    gr.arcs.resize(m);

    run_parallel(T, [&](int t) {
        Arc* fa = gf.arcs.data();
        Arc* ra = gr.arcs.data();
        for (const auto& e : thread_edges[t]) {
            u32 po = pos_out[e.u].fetch_add(1, memory_order_relaxed);
            u32 pi = pos_in[e.v].fetch_add(1, memory_order_relaxed);
            fa[po] = {e.v, e.w};
            ra[pi] = {e.u, e.w};
        }
    });

    delete[] deg_out;
    delete[] deg_in;
    for (int t = 0; t < T; ++t) {
        vector<Edge>().swap(thread_edges[t]);
    }

    // BFS ordering over forward skeleton
    vector<u32> perm(n, ~0u);
    vector<u32> inv(n);
    u32 order_sz = 0;

    for (u32 s = 0; s < n; ++s) {
        if (perm[s] != ~0u) continue;
        perm[s] = order_sz;
        inv[order_sz] = s;
        order_sz++;

        size_t head = order_sz - 1;
        while (head < order_sz) {
            u32 u = inv[head++];
            for (u32 i = gf.start[u], e = gf.start[u + 1]; i < e; ++i) {
                u32 v = gf.arcs[i].to;
                if (perm[v] == ~0u) {
                    perm[v] = order_sz;
                    inv[order_sz] = v;
                    order_sz++;
                }
            }
        }
    }

    // Renumber CSR in parallel
    CSR nf, nr;
    nf.start.assign(n + 1, 0);
    nr.start.assign(n + 1, 0);

    run_parallel(T, [&](int t) {
        u32 u0 = (u64)n * t / T;
        u32 u1 = (u64)n * (t + 1) / T;
        for (u32 ni = u0; ni < u1; ++ni) {
            u32 o = inv[ni];
            nf.start[ni + 1] = gf.start[o + 1] - gf.start[o];
            nr.start[ni + 1] = gr.start[o + 1] - gr.start[o];
        }
    });

    for (u32 i = 0; i < n; ++i) {
        nf.start[i + 1] += nf.start[i];
        nr.start[i + 1] += nr.start[i];
    }

    nf.arcs.resize(m);
    nr.arcs.resize(m);

    run_parallel(T, [&](int t) {
        u32 u0 = (u64)n * t / T;
        u32 u1 = (u64)n * (t + 1) / T;
        for (u32 ni = u0; ni < u1; ++ni) {
            u32 o = inv[ni];
            u32 pf = nf.start[ni];
            for (u32 i = gf.start[o], e = gf.start[o + 1]; i < e; ++i) {
                nf.arcs[pf++] = {perm[gf.arcs[i].to], gf.arcs[i].w};
            }
            u32 pr = nr.start[ni];
            for (u32 i = gr.start[o], e = gr.start[o + 1]; i < e; ++i) {
                nr.arcs[pr++] = {perm[gr.arcs[i].to], gr.arcs[i].w};
            }
        }
    });

    vector<Arc>().swap(gf.arcs);
    vector<u32>().swap(gf.start);
    vector<Arc>().swap(gr.arcs);
    vector<u32>().swap(gr.start);
    vector<u32>().swap(inv);

    // Read queries
    string qpath = data_dir + "/queries.txt";
    int qfd = open(qpath.c_str(), O_RDONLY);
    struct stat qst;
    fstat(qfd, &qst);
    size_t qlen = qst.st_size;
    const char* qdata = (const char*)mmap(nullptr, qlen, PROT_READ, MAP_SHARED, qfd, 0);
    close(qfd);

    vector<u32> qs, qt;
    const char* qp = qdata;
    const char* qend = qdata + qlen;
    while (qp < qend) {
        while (qp < qend && (u8)*qp <= ' ') ++qp;
        if (qp >= qend) break;
        u32 s = parse_u32(qp);
        u32 t = parse_u32(qp);
        qs.push_back(perm[s]);
        qt.push_back(perm[t]);
    }
    munmap((void*)qdata, qlen);

    size_t Q = qs.size();
    vector<i64> ans(Q, -1);

    int QT = (int)min<size_t>((size_t)T, max<size_t>(1, Q));
    atomic<size_t> next_q(0);
    run_parallel(QT, [&](int) {
        Searcher S(&nf, &nr, n);
        for (;;) {
            size_t i = next_q.fetch_add(1, memory_order_relaxed);
            if (i >= Q) break;
            ans[i] = S.query(qs[i], qt[i]);
        }
    });

    // Write output
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

    mkdir(out_dir.c_str(), 0777);
    string path = out_dir + "/distances.txt";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        system(("mkdir -p '" + out_dir + "'").c_str());
        f = fopen(path.c_str(), "wb");
    }
    fwrite(outbuf.data(), 1, outbuf.size(), f);
    fclose(f);

    if (getenv("SD_TIME")) {
        fprintf(stderr, "Total wall time: %.3f s\n", now() - t0);
    }
    return 0;
}
