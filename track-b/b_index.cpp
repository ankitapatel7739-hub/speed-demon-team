// Speed Demon Track B - C++17
// Reads docs.txt / queries.txt, writes results.txt (same format as the Python reference).
// Build: g++ -O2 -std=c++17 -pthread b_index.cpp -o b_index
// Run:   ./b_index --data /data --out /out        (env WORKERS=n overrides thread count)
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define HAVE_MMAP 1
#endif

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef std::vector<u32> V;

[[noreturn]] static void die(const std::string& m) {
    fprintf(stderr, "error: %s\n", m.c_str());
    exit(1);
}

// ---------- file access ----------
struct Mapped { const char* d; size_t n; };

static Mapped map_file(const std::string& path) {
#ifdef HAVE_MMAP
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) die("cannot open " + path);
    struct stat st;
    if (fstat(fd, &st) != 0) die("cannot stat " + path);
    size_t n = (size_t)st.st_size;
    if (n == 0) { close(fd); return {"", 0}; }
    void* p = mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) die("mmap failed for " + path);
    close(fd);
    return {(const char*)p, n};
#else
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) die("cannot open " + path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(n > 0 ? n : 1);
    if (n > 0 && fread(buf, 1, n, f) != (size_t)n) die("read failed " + path);
    fclose(f);
    return {buf, (size_t)n};
#endif
}

// ---------- tokenizing / term table ----------
static const size_t MAXTOK = 256;
static u8 LC[256];
static const u64 HOFF = 14695981039346656037ULL;

static inline u64 hstep(u64 h, u8 c) { return (h ^ c) * 1099511628211ULL; }

struct Table {
    std::vector<int32_t> slot;
    u64 mask = 0;
    std::vector<u64> hash;
    std::vector<u32> off, len;
    std::vector<char> pool;
    size_t maxlen = 0;
    u32 n = 0;

    void build(const std::vector<std::string_view>& terms) {
        n = (u32)terms.size();
        size_t cap = 16;
        while (cap < (size_t)n * 2 + 2) cap <<= 1;
        slot.assign(cap, -1);
        mask = cap - 1;
        hash.resize(n); off.resize(n); len.resize(n);
        for (u32 i = 0; i < n; i++) {
            std::string_view t = terms[i];
            off[i] = (u32)pool.size();
            len[i] = (u32)t.size();
            pool.insert(pool.end(), t.begin(), t.end());
            maxlen = std::max(maxlen, t.size());
            u64 h = HOFF;
            for (char ch : t) h = hstep(h, (u8)ch);
            h ^= h >> 32;
            hash[i] = h;
            size_t ix = h & mask;
            while (slot[ix] >= 0) ix = (ix + 1) & mask;
            slot[ix] = (int32_t)i;
        }
        if (maxlen > MAXTOK) die("query term too long");
    }
    inline int find(u64 h, const u8* s, size_t l) const {
        size_t ix = h & mask;
        for (;;) {
            int32_t id = slot[ix];
            if (id < 0) return -1;
            if (hash[id] == h && len[id] == l && memcmp(pool.data() + off[id], s, l) == 0) return id;
            ix = (ix + 1) & mask;
        }
    }
    int find_raw(std::string_view w) const {
        u64 h = HOFF;
        for (char ch : w) h = hstep(h, (u8)ch);
        h ^= h >> 32;
        return find(h, (const u8*)w.data(), w.size());
    }
};

// calls f(term_id) for every [a-z0-9]+ token (lowercased); term_id = -1 if not a query term
template <class F>
static inline void tokenize(const char* p, const char* e, const Table& T, F&& f) {
    u8 buf[MAXTOK];
    const size_t ml = T.maxlen;
    while (p < e) {
        u8 c = 0;
        while (p < e && !(c = LC[(u8)*p])) ++p;
        if (p >= e) break;
        size_t n = 0;
        u64 h = HOFF;
        while (p < e && (c = LC[(u8)*p])) {
            if (n < ml) { buf[n] = c; h = hstep(h, c); }
            ++n; ++p;
        }
        if (n > ml) { f(-1); continue; }
        h ^= h >> 32;
        f(T.find(h, buf, n));
    }
}

// ---------- sorted-list set operations ----------
static inline size_t gallop(const u32* b, size_t n, size_t i, u32 x) {
    if (i >= n) return n;
    if (b[i] >= x) return i;
    size_t lo = i, step = 1, hi = lo + 1;
    while (hi < n && b[hi] < x) { lo = hi; step <<= 1; hi = lo + step; }
    if (hi > n) hi = n;
    return std::lower_bound(b + lo + 1, b + hi, x) - b;
}

static void op_and(const V& a0, const V& b0, V& out) {
    const V* a = &a0; const V* b = &b0;
    if (a->size() > b->size()) std::swap(a, b);
    out.clear();
    if (a->empty()) return;
    const u32* ad = a->data(); const u32* bd = b->data();
    size_t an = a->size(), bn = b->size();
    if (an * 16 < bn) {
        size_t j = 0;
        for (size_t i = 0; i < an; i++) {
            j = gallop(bd, bn, j, ad[i]);
            if (j == bn) break;
            if (bd[j] == ad[i]) out.push_back(ad[i]);
        }
    } else {
        size_t i = 0, j = 0;
        while (i < an && j < bn) {
            u32 x = ad[i], y = bd[j];
            if (x < y) i++;
            else if (x > y) j++;
            else { out.push_back(x); i++; j++; }
        }
    }
}

static void op_or(const V& a, const V& b, V& out) {
    out.clear();
    if (a.empty()) { out = b; return; }
    if (b.empty()) { out = a; return; }
    out.reserve(a.size() + b.size());
    std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
}

static void op_not(const V& a, const V& b, V& out) {
    out.clear();
    if (a.empty()) return;
    if (b.empty()) { out = a; return; }
    if (a.size() * 16 < b.size()) {
        const u32* bd = b.data(); size_t bn = b.size(), j = 0;
        for (u32 x : a) {
            j = gallop(bd, bn, j, x);
            if (j < bn && bd[j] == x) continue;
            out.push_back(x);
        }
    } else {
        out.reserve(a.size());
        std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    }
}

static void fmt(const V& v, std::string& out) {
    u64 s = 0;
    for (u32 x : v) s += x;
    out = std::to_string(v.size());
    out += ' ';
    out += std::to_string(s);
    size_t k = std::min<size_t>(10, v.size());
    for (size_t i = 0; i < k; i++) { out += ' '; out += std::to_string(v[i]); }
}

// ---------- queries ----------
static inline bool isws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\x0b' || c == '\x0c'; }

static std::vector<std::string_view> split_ws(std::string_view s) {
    std::vector<std::string_view> w;
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && isws(s[i])) i++;
        size_t j = i;
        while (j < n && !isws(s[j])) j++;
        if (j > i) w.push_back(s.substr(i, j - i));
        i = j;
    }
    return w;
}

// ---------- dense (bitmap) support for very common words ----------
static u8 PC8[256];
static u8 SP8[256];
static void init_tables() {
    for (int b = 0; b < 256; b++) {
        int pc = 0, sp = 0;
        for (int j = 0; j < 8; j++) if ((b >> j) & 1) { pc++; sp += j; }
        PC8[b] = (u8)pc; SP8[b] = (u8)sp;
    }
}
static inline int ctz64(u64 x) {
#if defined(__GNUC__)
    return __builtin_ctzll(x);
#else
    int n = 0;
    while (!(x & 1)) { x >>= 1; n++; }
    return n;
#endif
}
static inline bool getbit(const u64* b, u32 i) { return (b[i >> 6] >> (i & 63)) & 1; }
static inline void setbit(u64* b, u32 i) { b[i >> 6] |= 1ULL << (i & 63); }
static inline void clrbit(u64* b, u32 i) { b[i >> 6] &= ~(1ULL << (i & 63)); }

// A set is either a sorted list (l) or, when b != nullptr, a bitmap (a dense term also keeps its list).
struct SetV { const V* l; const u64* b; };

static SetV apply_op(int op, const SetV& a, const SetV& b, V& ol, std::vector<u64>& ob, size_t W) {
    if (op == 0) {  // AND
        if (a.b && b.b) {
            ob.resize(W);
            for (size_t i = 0; i < W; i++) ob[i] = a.b[i] & b.b[i];
            return {nullptr, ob.data()};
        }
        if (a.b || b.b) {
            const u64* bits = a.b ? a.b : b.b;
            const V& lst = a.b ? *b.l : *a.l;
            ol.clear();
            for (u32 x : lst) if (getbit(bits, x)) ol.push_back(x);
            return {&ol, nullptr};
        }
        op_and(*a.l, *b.l, ol);
        return {&ol, nullptr};
    }
    if (op == 1) {  // OR
        if (a.b && b.b) {
            ob.resize(W);
            for (size_t i = 0; i < W; i++) ob[i] = a.b[i] | b.b[i];
            return {nullptr, ob.data()};
        }
        if (a.b || b.b) {
            const u64* bits = a.b ? a.b : b.b;
            const V& lst = a.b ? *b.l : *a.l;
            ob.assign(bits, bits + W);
            for (u32 x : lst) setbit(ob.data(), x);
            return {nullptr, ob.data()};
        }
        op_or(*a.l, *b.l, ol);
        return {&ol, nullptr};
    }
    // NOT: a \ b
    if (a.b && b.b) {
        ob.resize(W);
        for (size_t i = 0; i < W; i++) ob[i] = a.b[i] & ~b.b[i];
        return {nullptr, ob.data()};
    }
    if (a.b) {
        ob.assign(a.b, a.b + W);
        for (u32 x : *b.l) clrbit(ob.data(), x);
        return {nullptr, ob.data()};
    }
    if (b.b) {
        ol.clear();
        for (u32 x : *a.l) if (!getbit(b.b, x)) ol.push_back(x);
        return {&ol, nullptr};
    }
    op_not(*a.l, *b.l, ol);
    return {&ol, nullptr};
}

static void fmt_bits(const u64* b, size_t W, std::string& out) {
    u64 cnt = 0, sum = 0;
    u32 first[10];
    int nf = 0;
    for (size_t w = 0; w < W; w++) {
        u64 x = b[w];
        if (!x) continue;
        u64 base = (u64)w * 64, pc = 0, sp = 0;
        for (int k = 0; k < 8; k++) {
            u8 by = (u8)((x >> (8 * k)) & 255);
            pc += PC8[by];
            sp += SP8[by] + (u64)PC8[by] * 8 * k;
        }
        cnt += pc;
        sum += base * pc + sp;
        for (u64 y = x; y && nf < 10; y &= y - 1) first[nf++] = (u32)(base + (u64)ctz64(y));
    }
    out = std::to_string(cnt);
    out += ' ';
    out += std::to_string(sum);
    for (int i = 0; i < nf; i++) { out += ' '; out += std::to_string(first[i]); }
}

static unsigned ncpus() {
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    unsigned lim = hw;
    if (FILE* f = fopen("/sys/fs/cgroup/cpu.max", "r")) {  // cgroup v2
        char q[32] = {0};
        long per = 0;
        if (fscanf(f, "%31s %ld", q, &per) == 2 && strcmp(q, "max") != 0 && per > 0) {
            long quota = atol(q);
            if (quota > 0) lim = std::min(lim, (unsigned)std::max(1L, (quota + per - 1) / per));
        }
        fclose(f);
    } else {  // cgroup v1
        long quota = -1, per = 0;
        if (FILE* f1 = fopen("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", "r")) { if (fscanf(f1, "%ld", &quota) != 1) quota = -1; fclose(f1); }
        if (FILE* f2 = fopen("/sys/fs/cgroup/cpu/cpu.cfs_period_us", "r")) { if (fscanf(f2, "%ld", &per) != 1) per = 0; fclose(f2); }
        if (quota > 0 && per > 0) lim = std::min(lim, (unsigned)std::max(1L, (quota + per - 1) / per));
    }
    return lim;
}

struct UQ {
    bool phrase = false;
    std::vector<std::string_view> w;  // BOOL: all words; PHRASE: terms
    std::vector<int> t;               // term ids (BOOL: operands, PHRASE: terms)
    std::vector<u8> ops;              // BOOL: 0 AND, 1 OR, 2 NOT
    u32 pid = 0;                      // PHRASE (>=2 terms): index into phrase table
};

template <class F>
static void parallel(int nt, F&& f) {
    std::vector<std::thread> th;
    for (int i = 1; i < nt; i++) th.emplace_back(f, i);
    f(0);
    for (auto& t : th) t.join();
}

// Table of distinct multi-term phrases; phrases are matched directly while reading documents.
static inline u64 hstep64(u64 h, int id) { return (h ^ (u64)(id + 1)) * 1099511628211ULL; }

struct PTab {
    std::vector<int32_t> slot;
    u64 mask = 0;
    std::vector<u64> hv;
    std::vector<u32> st, kl;
    std::vector<int> ids;
    std::vector<char> isk;
    std::vector<u64> bg;  // bit filter of adjacent id pairs that occur inside some phrase
    static const int BGBITS = 20;
    u32 maxk = 0, n = 0;
    static inline u32 bgh(int a, int b) {
        return (u32)((((u64)(a + 1) * 0x9E3779B97F4A7C15ULL) + ((u64)(b + 1) * 0xC2B2AE3D27D4EB4FULL)) >> (64 - BGBITS));
    }
    inline bool bgtest(int a, int b) const { u32 h = bgh(a, b); return (bg[h >> 6] >> (h & 63)) & 1; }
    static inline u64 mix(u64 h) { return h ^ (h >> 32); }
    void build(const std::vector<const std::vector<int>*>& pl) {
        n = (u32)pl.size();
        size_t cap = 16;
        while (cap < (size_t)n * 2 + 2) cap <<= 1;
        slot.assign(cap, -1);
        mask = cap - 1;
        hv.resize(n); st.resize(n); kl.resize(n);
        for (u32 i = 0; i < n; i++) {
            const std::vector<int>& p = *pl[i];
            maxk = std::max<u32>(maxk, (u32)p.size());
        }
        isk.assign(maxk + 1, 0);
        bg.assign((size_t)1 << (BGBITS - 6), 0);
        for (u32 i = 0; i < n; i++) {
            const std::vector<int>& p = *pl[i];
            for (size_t j = 0; j + 1 < p.size(); j++) { u32 h = bgh(p[j], p[j + 1]); bg[h >> 6] |= 1ULL << (h & 63); }
        }
        for (u32 i = 0; i < n; i++) {
            const std::vector<int>& p = *pl[i];
            st[i] = (u32)ids.size();
            kl[i] = (u32)p.size();
            isk[p.size()] = 1;
            ids.insert(ids.end(), p.begin(), p.end());
            u64 h = HOFF;
            for (size_t j = p.size(); j-- > 0;) h = hstep64(h, p[j]);
            h = mix(h);
            hv[i] = h;
            size_t ix = h & mask;
            while (slot[ix] >= 0) ix = (ix + 1) & mask;
            slot[ix] = (int32_t)i;
        }
    }
    // win points at the first of the k ids of the candidate window
    inline int find(u64 h, u32 k, const int* win) const {
        size_t ix = h & mask;
        for (;;) {
            int32_t id = slot[ix];
            if (id < 0) return -1;
            if (hv[id] == h && kl[id] == k && memcmp(ids.data() + st[id], win, k * sizeof(int)) == 0) return id;
            ix = (ix + 1) & mask;
        }
    }
};

struct P1 {
    std::vector<V> post;
    std::vector<V> ppost;
    std::vector<u64> lines;
    u32 first_id = 0;
};

int main(int argc, char** argv) {
    std::string data_dir = "/data", out_dir = "/out";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a.rfind("--data=", 0) == 0) data_dir = a.substr(7);
        else if (a.rfind("--out=", 0) == 0) out_dir = a.substr(6);
    }
    for (int c = 0; c < 256; c++) {
        u8 v = 0;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) v = (u8)c;
        else if (c >= 'A' && c <= 'Z') v = (u8)(c + 32);
        LC[c] = v;
    }
    init_tables();
    auto t_start = std::chrono::steady_clock::now();
    auto lap = [&]() {
        auto now = std::chrono::steady_clock::now();
        double s = std::chrono::duration<double>(now - t_start).count();
        t_start = now;
        return s;
    };
    const bool debug = getenv("DEBUG") != nullptr;

    // ---- queries ----
    Mapped qf = map_file(data_dir + "/queries.txt");
    std::vector<std::string_view> lines;
    {
        size_t pos = 0;
        while (pos < qf.n) {
            const char* ls = qf.d + pos;
            const char* le = (const char*)memchr(ls, '\n', qf.n - pos);
            size_t len = le ? (size_t)(le - ls) : qf.n - pos;
            lines.emplace_back(ls, len);
            pos += len + 1;
        }
    }
    std::unordered_map<std::string_view, u32> uniq;
    uniq.reserve(lines.size() * 2);
    std::vector<UQ> uq;
    std::vector<u32> qmap(lines.size());
    std::unordered_set<std::string_view> needed_set;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string_view line = lines[i];
        auto it = uniq.find(line);
        if (it != uniq.end()) { qmap[i] = it->second; continue; }
        u32 id = (u32)uq.size();
        uniq.emplace(line, id);
        qmap[i] = id;
        size_t sp = line.find(' ');
        std::string_view kind = sp == std::string_view::npos ? line : line.substr(0, sp);
        std::string_view rest = sp == std::string_view::npos ? std::string_view() : line.substr(sp + 1);
        UQ q;
        if (kind == "BOOL") {
            q.w = split_ws(rest);
            for (size_t k = 0; k < q.w.size(); k += 2) needed_set.insert(q.w[k]);
        } else if (kind == "PHRASE") {
            if (rest.empty() || rest.front() != '"' || rest.back() != '"')
                die("Invalid phrase query: " + std::string(line));
            q.phrase = true;
            std::string_view inner = rest.size() >= 2 ? rest.substr(1, rest.size() - 2) : std::string_view();
            q.w = split_ws(inner);
            for (auto w : q.w) needed_set.insert(w);
        } else {
            die("Invalid query: " + std::string(line));
        }
        uq.push_back(std::move(q));
    }
    std::vector<std::string_view> terms(needed_set.begin(), needed_set.end());
    Table T;
    T.build(terms);
    for (auto& q : uq) {
        if (!q.phrase) {
            size_t L = q.w.size();
            if (L == 0) continue;
            size_t pairs = (L - 1) / 2;
            for (size_t i = 0; i <= pairs; i++) q.t.push_back(T.find_raw(q.w[2 * i]));
            for (size_t i = 0; i < pairs; i++) {
                std::string_view op = q.w[2 * i + 1];
                if (op == "AND") q.ops.push_back(0);
                else if (op == "OR") q.ops.push_back(1);
                else if (op == "NOT") q.ops.push_back(2);
                else die("Unknown operator: " + std::string(op));
            }
        } else {
            for (auto w : q.w) q.t.push_back(T.find_raw(w));
        }
    }
    std::map<std::vector<int>, u32> pmap;
    std::vector<const std::vector<int>*> plist;
    for (auto& q : uq) {
        if (!q.phrase || q.t.size() < 2) continue;
        auto r = pmap.emplace(q.t, (u32)pmap.size());
        q.pid = r.first->second;
        if (r.second) plist.push_back(&r.first->first);
    }
    PTab PT;
    PT.build(plist);
    double t_parse = lap();

    // ---- pass 1: postings ----
    Mapped df = map_file(data_dir + "/docs.txt");
    int nt = 0;
    if (const char* e = getenv("WORKERS")) nt = atoi(e);
    if (nt <= 0) nt = (int)std::min(8u, ncpus());
    if (df.n < (4u << 20)) nt = 1;
    std::vector<size_t> bnd(nt + 1);
    bnd[0] = 0; bnd[nt] = df.n;
    for (int i = 1; i < nt; i++) {
        size_t p = df.n * (size_t)i / nt;
        if (p > 0 && df.d[p - 1] != '\n') {
            const char* nl = (const char*)memchr(df.d + p, '\n', df.n - p);
            p = nl ? (size_t)(nl - df.d) + 1 : df.n;
        }
        bnd[i] = std::max(p, bnd[i - 1]);
    }
    std::vector<P1> parts(nt);
    parallel(nt, [&](int tid) {
        P1& out = parts[tid];
        out.post.assign(T.n, V());
        out.ppost.assign(PT.n, V());
        std::vector<u32> last(T.n, UINT32_MAX), lastp(PT.n, UINT32_MAX);
        std::vector<int> seq;
        const char* d = df.d;
        size_t pos = bnd[tid], e = bnd[tid + 1];
        while (pos < e) {
            const char* ls = d + pos;
            const char* le = (const char*)memchr(ls, '\n', df.n - pos);
            size_t lend = le ? (size_t)(le - d) : df.n;
            const char* q = ls;
            const char* lim = d + lend;
            u64 id = 0;
            while (q < lim && *q != '\t') { id = id * 10 + (u64)(*q - '0'); ++q; }
            if (q >= lim) die("bad document line");
            if (out.lines.empty()) out.first_id = (u32)id;
            else if (id != (u64)out.first_id + out.lines.size()) die("document ids are not sequential");
            out.lines.push_back(pos);
            u32 did = (u32)id;
            size_t run = 0;
            seq.clear();
            tokenize(q + 1, lim, T, [&](int t) {
                if (t >= 0 && last[t] != did) { last[t] = did; out.post[t].push_back(did); }
                if (PT.maxk >= 2) {
                    seq.push_back(t);
                    if (t < 0) { run = 0; return; }
                    run++;
                    if (run < 2 || !PT.bgtest(seq[seq.size() - 2], t)) return;
                    size_t lk = std::min<size_t>(run, PT.maxk);
                    u64 h = HOFF;
                    const int* en = seq.data() + seq.size();
                    for (size_t j = 1; j <= lk; j++) {
                        h = hstep64(h, en[-(ptrdiff_t)j]);
                        if (j >= 2 && PT.isk[j]) {
                            int pid = PT.find(PTab::mix(h), (u32)j, en - j);
                            if (pid >= 0 && lastp[pid] != did) { lastp[pid] = did; out.ppost[pid].push_back(did); }
                        }
                    }
                }
            });
            pos = lend + 1;
        }
    });
    {
        u64 expect = 0;
        for (auto& p : parts) {
            if (p.lines.empty()) continue;
            if (p.first_id != expect) die("document ids are not sequential");
            expect += p.lines.size();
        }
    }
    auto merge = [&](std::vector<V>& dst, std::vector<V> P1::*member) {
        for (size_t t = 0; t < dst.size(); t++) {
            size_t total = 0;
            for (auto& p : parts) total += (p.*member)[t].size();
            V& g = dst[t];
            g.reserve(total);
            for (auto& p : parts) {
                g.insert(g.end(), (p.*member)[t].begin(), (p.*member)[t].end());
                V().swap((p.*member)[t]);
            }
        }
    };
    std::vector<V> post(T.n), pp(PT.n);
    merge(post, &P1::post);
    merge(pp, &P1::ppost);
    size_t ndocs = 0;
    for (auto& p : parts) ndocs += p.lines.size();
    parts.clear(); parts.shrink_to_fit();
    const size_t W = (ndocs + 63) / 64;
    std::vector<std::vector<u64>> tbits(T.n);
    {
        std::atomic<size_t> nx(0);
        parallel(nt, [&](int) {
            for (;;) {
                size_t t = nx.fetch_add(1);
                if (t >= T.n) break;
                if (post[t].size() * 16 <= ndocs) continue;  // sparse term: list only
                std::vector<u64>& bs = tbits[t];
                bs.assign(W, 0);
                for (u32 x : post[t]) setbit(bs.data(), x);
            }
        });
    }
    double t_p1 = lap();

    // ---- phrases (already matched during pass 1) ----
    std::vector<std::string> res(uq.size());
    for (u32 i = 0; i < uq.size(); i++) {
        const UQ& q = uq[i];
        if (!q.phrase) continue;
        if (q.t.empty()) res[i] = "0 0";
        else if (q.t.size() == 1) fmt(post[q.t[0]], res[i]);
        else fmt(pp[q.pid], res[i]);
    }
    double t_ph = lap();

    // ---- boolean queries ----
    {
        std::vector<u32> bq;
        for (u32 i = 0; i < uq.size(); i++) if (!uq[i].phrase) bq.push_back(i);
        std::atomic<size_t> next(0);
        int bt = (int)std::min<size_t>(nt, std::max<size_t>(1, bq.size()));
        parallel(bt, [&](int) {
            V ls[2];
            std::vector<u64> bs[2];
            auto view = [&](int t) { return SetV{&post[t], tbits[t].empty() ? nullptr : tbits[t].data()}; };
            for (;;) {
                size_t k = next.fetch_add(1);
                if (k >= bq.size()) break;
                const UQ& q = uq[bq[k]];
                if (q.t.empty()) { res[bq[k]] = "0 0"; continue; }
                SetV cur = view(q.t[0]);
                int w = 0;
                for (size_t i = 0; i < q.ops.size(); i++) {
                    cur = apply_op(q.ops[i], cur, view(q.t[i + 1]), ls[w], bs[w], W);
                    w ^= 1;
                }
                if (cur.b) fmt_bits(cur.b, W, res[bq[k]]);
                else fmt(*cur.l, res[bq[k]]);
            }
        });
    }
    double t_bool = lap();

    // ---- output ----
    std::filesystem::create_directories(out_dir);
    std::string outbuf;
    outbuf.reserve(lines.size() * 40);
    for (size_t i = 0; i < lines.size(); i++) {
        outbuf += res[qmap[i]];
        outbuf += '\n';
    }
    FILE* fo = fopen((out_dir + "/results.txt").c_str(), "wb");
    if (!fo) die("cannot write results.txt");
    fwrite(outbuf.data(), 1, outbuf.size(), fo);
    fclose(fo);
    double t_out = lap();
    if (debug)
        fprintf(stderr, "threads=%d parse %.2fs  pass1 %.2fs  phrases %.2fs  bool %.2fs  write %.2fs  (queries=%zu unique=%zu terms=%u docs=%zu)\n",
                nt, t_parse, t_p1, t_ph, t_bool, t_out, lines.size(), uq.size(), T.n, ndocs);
    return 0;
}
