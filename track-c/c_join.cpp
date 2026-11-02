// Speed Demon 2026, Track C (Join): C++ implementation (v3, hash-only distinct count).
//
// Changes vs v1:
//  * no MAP_POPULATE: each thread faults/reads ahead its own slice in parallel
//  * distinct names are de-duplicated on insert with an open-addressing hash set
//    (64-bit hash + byte comparison, so still exact) instead of pushing every
//    record and sorting O(n log n)
//  * merge = insert the smaller sets into the largest one, no re-sort
//
// Build: g++ -O3 -std=c++17 -pthread -o c_join c_join.cpp
// Run:   ./c_join --data /data --out /out

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char BOMB[3] = {(char)0xEF, (char)0xBB, (char)0xBF};
static const char HEADER[] = "id,name,note,region_id,amount";

[[noreturn]] static void die(const char* msg) {
    fprintf(stderr, "c_join: %s\n", msg);
    exit(1);
}

struct NameRec {
    uint64_t h;
    uint32_t off;
    uint32_t len;
};

static inline uint64_t hash_bytes(const char* p, size_t n) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ (uint64_t)n;
    while (n >= 8) {
        uint64_t v;
        memcpy(&v, p, 8);
        h = (h ^ v) * 0xFF51AFD7ED558CCDULL;
        h ^= h >> 32;
        p += 8;
        n -= 8;
    }
    uint64_t v = 0;
    memcpy(&v, p, n);
    h = (h ^ v) * 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDULL; h ^= h >> 33;
    return h;
}

// Set of 64-bit name hashes (open addressing, 0 = empty). No file access on insert.
struct HSet {
    std::vector<uint64_t> t;
    size_t n = 0;
    void init(size_t cap) { t.assign(cap, 0); }
    void grow() {
        std::vector<uint64_t> old;
        old.swap(t);
        init(old.size() * 2);
        size_t mask = t.size() - 1;
        for (uint64_t h : old) {
            if (!h) continue;
            size_t i = (size_t)(h ^ (h >> 32)) & mask;
            while (t[i]) i = (i + 1) & mask;
            t[i] = h;
        }
    }
    inline void insert(uint64_t h) {
        if (!h) h = 1;
        if (t.empty()) init(64);
        size_t mask = t.size() - 1;
        size_t i = (size_t)(h ^ (h >> 32)) & mask;
        for (;;) {
            uint64_t s = t[i];
            if (!s) {
                t[i] = h;
                if (++n * 2 > t.size()) grow();
                return;
            }
            if (s == h) return;
            i = (i + 1) & mask;
        }
    }
};

struct Acc {
    uint64_t count = 0, id_sum = 0, name_bytes = 0, note_bytes = 0;
    int64_t sum = 0, mn = INT64_MAX, mx = INT64_MIN;
    HSet names;
};

// ---------------------------------------------------------------- regions

struct Lookup {
    std::vector<uint16_t> tab;                      // dense table when ids are small
    std::unordered_map<uint64_t, uint16_t> big;     // otherwise
    uint16_t unmatched = 0;

    inline uint16_t get(uint64_t id) const {
        if (id < tab.size()) return tab[id];
        if (!big.empty()) {
            auto it = big.find(id);
            if (it != big.end()) return it->second;
        }
        return unmatched;
    }
};

static std::string read_all(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) die(("cannot open " + path).c_str());
    std::string s;
    char buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static void load_regions(const std::string& path, std::vector<std::string>& codes,
                         Lookup& lk) {
    std::string data = read_all(path);
    std::unordered_map<std::string, uint16_t> index;
    std::vector<std::pair<uint64_t, uint16_t>> pairs;
    uint64_t max_id = 0;
    size_t pos = 0;
    bool first = true;

    while (pos < data.size()) {
        size_t e = data.find('\n', pos);
        if (e == std::string::npos) e = data.size();
        size_t le = e;
        if (le > pos && data[le - 1] == '\r') le--;
        std::string line = data.substr(pos, le - pos);
        pos = e + 1;

        if (first) {
            if (line != "region_id,country") die("unexpected regions.csv header");
            first = false;
            continue;
        }
        if (line.empty()) continue;

        size_t c = line.find(',');
        if (c == std::string::npos || c == 0 || c > 18) die("bad regions.csv line");
        uint64_t id = 0;
        for (size_t i = 0; i < c; i++) {
            unsigned d = (unsigned char)line[i] - '0';
            if (d > 9) die("bad region id");
            id = id * 10 + d;
        }
        std::string country = line.substr(c + 1);
        auto it = index.find(country);
        uint16_t ci;
        if (it == index.end()) {
            if (codes.size() >= 65000) die("too many countries");
            ci = (uint16_t)codes.size();
            codes.push_back(country);
            index.emplace(country, ci);
        } else {
            ci = it->second;
        }
        pairs.emplace_back(id, ci);
        if (id > max_id) max_id = id;
    }
    if (first) die("empty regions.csv");

    lk.unmatched = (uint16_t)codes.size();
    if (max_id <= 64u * 1024 * 1024) {
        lk.tab.assign(max_id + 1, lk.unmatched);
        for (auto& p : pairs) lk.tab[p.first] = p.second;
    } else {
        for (auto& p : pairs) lk.big[p.first] = p.second;
    }
}

// ---------------------------------------------------------------- parsing

// First record start at or after pos: just past an LF (and optional BOM) that is
// followed by a digit. Inside a quoted note an LF is followed by a lowercase word.
static size_t record_start(const char* b, size_t n, size_t pos) {
    while (pos < n) {
        const char* p = (const char*)memchr(b + pos, '\n', n - pos);
        if (!p) return n;
        size_t j = (size_t)(p - b) + 1;
        if (j + 3 <= n && memcmp(b + j, BOMB, 3) == 0) j += 3;
        if (j < n && (unsigned)((unsigned char)b[j] - '0') <= 9) return j;
        pos = (size_t)(p - b) + 1;
    }
    return n;
}

struct Text {
    size_t start, len;  // inner bytes (between the quotes, "" still doubled)
    size_t value_len;   // UTF-8 length after unescaping
};

// One text field starting at pos; leaves pos on the byte after the field.
static inline bool parse_text(const char* b, size_t fend, size_t& pos, Text& t) {
    if (pos >= fend) return false;
    if (b[pos] == '"') {
        size_t cur = pos + 1, pairs = 0;
        for (;;) {
            if (cur > fend) return false;
            const char* p = (const char*)memchr(b + cur, '"', fend - cur);
            if (!p) return false;
            size_t q = (size_t)(p - b);
            if (q + 1 < fend && b[q + 1] == '"') {
                pairs++;
                cur = q + 2;
            } else {
                t.start = pos + 1;
                t.len = q - (pos + 1);
                t.value_len = t.len - pairs;
                pos = q + 1;
                return true;
            }
        }
    }
    // unquoted: normally empty; anything else must be free of quote/CR/LF
    size_t s = pos;
    while (pos < fend && b[pos] != ',') {
        char c = b[pos];
        if (c == '"' || c == '\r' || c == '\n') return false;
        pos++;
    }
    t.start = s;
    t.len = pos - s;
    t.value_len = t.len;
    return true;
}

static void run_chunk(const char* b, size_t pos, size_t end, size_t fend,
                      const Lookup& lk, std::vector<Acc>& accs) {
    // read ahead our own slice (page-aligned) so faults overlap across threads
    {
        size_t a = pos & ~(size_t)4095;
        if (end > a) madvise((void*)(b + a), end - a, MADV_WILLNEED);
    }
    while (pos < end) {
        // id
        size_t s = pos;
        uint64_t id = 0;
        unsigned d;
        while (pos < fend && (d = (unsigned char)b[pos] - '0') <= 9) {
            id = id * 10 + d;
            pos++;
        }
        if (pos == s || pos - s > 19 || pos >= fend || b[pos] != ',') die("bad record id");
        pos++;

        Text name, note;
        if (!parse_text(b, fend, pos, name) || pos >= fend || b[pos] != ',')
            die("bad name field");
        pos++;
        if (!parse_text(b, fend, pos, note) || pos >= fend || b[pos] != ',')
            die("bad note field");
        pos++;

        // region id
        s = pos;
        uint64_t reg = 0;
        bool reg_big = false;
        while (pos < fend && (d = (unsigned char)b[pos] - '0') <= 9) {
            if (reg > 1000000000000000000ULL) reg_big = true;  // would overflow
            else reg = reg * 10 + d;
            pos++;
        }
        if (pos == s || pos >= fend || b[pos] != ',') die("bad region id");
        if (reg_big) reg = UINT64_MAX;
        pos++;

        // amount: -?digits.dd
        bool neg = false;
        if (pos < fend && b[pos] == '-') {
            neg = true;
            pos++;
        }
        s = pos;
        int64_t whole = 0;
        while (pos < fend && (d = (unsigned char)b[pos] - '0') <= 9) {
            whole = whole * 10 + d;
            pos++;
        }
        if (pos == s || pos - s > 17 || pos + 2 >= fend || b[pos] != '.') die("bad amount");
        unsigned d1 = (unsigned char)b[pos + 1] - '0', d2 = (unsigned char)b[pos + 2] - '0';
        if (d1 > 9 || d2 > 9) die("bad amount");
        pos += 3;
        int64_t cents = whole * 100 + (int64_t)(d1 * 10 + d2);
        if (neg) cents = -cents;

        // terminator, then an optional BOM that starts the next record
        if (pos < fend) {
            if (b[pos] == '\r') {
                if (pos + 1 >= fend || b[pos + 1] != '\n') die("bad terminator");
                pos += 2;
            } else if (b[pos] == '\n') {
                pos++;
            } else {
                die("bad terminator");
            }
            if (pos + 3 <= fend && memcmp(b + pos, BOMB, 3) == 0) pos += 3;
        }

        Acc& a = accs[lk.get(reg)];
        a.count++;
        a.id_sum += id;
        a.sum += cents;
        if (cents < a.mn) a.mn = cents;
        if (cents > a.mx) a.mx = cents;
        a.name_bytes += name.value_len;
        a.note_bytes += note.value_len;
        a.names.insert(hash_bytes(b + name.start, name.len));
    }
}

// ---------------------------------------------------------------- main

static unsigned thread_count() {
    if (const char* e = getenv("C_JOIN_THREADS")) {
        int v = atoi(e);
        if (v > 0) return (unsigned)v;
    }
    unsigned n = std::thread::hardware_concurrency();
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof set, &set) == 0) {
        int c = CPU_COUNT(&set);
        if (c > 0) n = (unsigned)c;
    }
    if (FILE* f = fopen("/sys/fs/cgroup/cpu.max", "r")) {  // container CPU quota
        char q[64];
        long period;
        if (fscanf(f, "%63s %ld", q, &period) == 2 && strcmp(q, "max") != 0 && period > 0) {
            long quota = atol(q);
            unsigned lim = (unsigned)((quota + period - 1) / period);
            if (lim >= 1 && lim < n) n = lim;
        }
        fclose(f);
    }
    if (n < 1) n = 1;
    if (n > 16) n = 16;
    return n;
}

static std::string format_cents(int64_t c) {
    bool neg = c < 0;
    uint64_t a = neg ? (uint64_t)(-c) : (uint64_t)c;
    char buf[48];
    snprintf(buf, sizeof buf, "%s%llu.%02llu", neg ? "-" : "", (unsigned long long)(a / 100),
             (unsigned long long)(a % 100));
    return buf;
}

int main(int argc, char** argv) {
    std::string data_dir = "/data", out_dir = "/out";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data_dir = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_dir = argv[++i];
        else die("usage: c_join [--data DIR] [--out DIR]");
    }

    std::vector<std::string> codes;
    Lookup lk;
    load_regions(data_dir + "/regions.csv", codes, lk);
    const size_t K = codes.size() + 1;  // last slot = _UNMATCHED

    // map records.csv
    std::string rpath = data_dir + "/records.csv";
    int fd = open(rpath.c_str(), O_RDONLY);
    if (fd < 0) die("cannot open records.csv");
    struct stat st;
    if (fstat(fd, &st) != 0) die("cannot stat records.csv");
    size_t fsize = (size_t)st.st_size;
    if (fsize >= 0xFFFFFFF0ULL) die("records.csv too large");

    std::vector<Acc> total(K);
    const char* base = nullptr;

    if (fsize > 0) {
        void* m = mmap(nullptr, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m == MAP_FAILED) die("mmap failed");
        base = (const char*)m;

        // header
        size_t pos = 0;
        if (fsize >= 3 && memcmp(base, BOMB, 3) == 0) pos = 3;
        size_t hl = strlen(HEADER);
        if (pos + hl > fsize || memcmp(base + pos, HEADER, hl) != 0)
            die("unexpected records.csv header");
        pos += hl;
        if (pos + 1 < fsize && base[pos] == '\r' && base[pos + 1] == '\n') pos += 2;
        else if (pos < fsize && base[pos] == '\n') pos++;
        else if (pos != fsize) die("bad header terminator");
        if (pos + 3 <= fsize && memcmp(base + pos, BOMB, 3) == 0) pos += 3;

        // cut into per-thread ranges at record starts
        unsigned T = thread_count();
        if (fsize - pos < (1u << 20)) T = 1;
        std::vector<size_t> cuts(1, pos);
        for (unsigned k = 1; k < T; k++) {
            size_t off = pos + (fsize - pos) / T * k;
            size_t c = record_start(base, fsize, off);
            cuts.push_back(std::max(cuts.back(), c));
        }
        cuts.push_back(fsize);

        size_t parts = cuts.size() - 1;
        std::vector<std::vector<Acc>> per(parts, std::vector<Acc>(K));
        std::vector<std::thread> th;

        for (size_t i = 0; i < parts; i++) {
            th.emplace_back([&, i]() { run_chunk(base, cuts[i], cuts[i + 1], fsize, lk, per[i]); });
        }
        for (auto& t : th) t.join();

        // merge numbers; merge distinct names per country, in parallel
        std::vector<uint64_t> distinct(K, 0);
        for (size_t k = 0; k < K; k++) {
            Acc& t = total[k];
            for (size_t i = 0; i < parts; i++) {
                Acc& a = per[i][k];
                t.count += a.count;
                t.id_sum += a.id_sum;
                t.sum += a.sum;
                t.mn = std::min(t.mn, a.mn);
                t.mx = std::max(t.mx, a.mx);
                t.name_bytes += a.name_bytes;
                t.note_bytes += a.note_bytes;
            }
        }
        std::atomic<size_t> next{0};
        unsigned mt = std::min<unsigned>(std::max(1u, T), 16);
        std::vector<std::thread> mth;
        for (unsigned w = 0; w < mt; w++) {
            mth.emplace_back([&]() {
                for (;;) {
                    size_t k = next.fetch_add(1);
                    if (k >= K) break;
                    size_t big = 0;
                    for (size_t i = 1; i < parts; i++)
                        if (per[i][k].names.n > per[big][k].names.n) big = i;
                    HSet& dst = per[big][k].names;
                    for (size_t i = 0; i < parts; i++) {
                        if (i == big) continue;
                        for (uint64_t h : per[i][k].names.t)
                            if (h) dst.insert(h);
                        std::vector<uint64_t>().swap(per[i][k].names.t);
                    }
                    distinct[k] = dst.n;
                }
            });
        }
        for (auto& t : mth) t.join();
        // write output, sorted bytewise by key
        std::vector<std::pair<std::string, size_t>> keys;
        for (size_t k = 0; k < K; k++) {
            if (total[k].count == 0) continue;
            keys.emplace_back(k + 1 == K ? std::string("_UNMATCHED") : codes[k], k);
        }
        std::sort(keys.begin(), keys.end());

        std::filesystem::create_directories(out_dir);
        std::string out;
        for (auto& kv : keys) {
            const Acc& a = total[kv.second];
            out += kv.first;
            out += ' ' + std::to_string(a.count);
            out += ' ' + std::to_string(a.id_sum);
            out += ' ' + format_cents(a.sum);
            out += ' ' + format_cents(a.mn);
            out += ' ' + format_cents(a.mx);
            out += ' ' + std::to_string(distinct[kv.second]);
            out += ' ' + std::to_string(a.name_bytes);
            out += ' ' + std::to_string(a.note_bytes);
            out += '\n';
        }
        FILE* f = fopen((out_dir + "/aggregates.txt").c_str(), "wb");
        if (!f) die("cannot write aggregates.txt");
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
        _exit(0);  // skip unmapping / freeing big structures
    }

    // empty records.csv: empty output
    std::filesystem::create_directories(out_dir);
    FILE* f = fopen((out_dir + "/aggregates.txt").c_str(), "wb");
    if (!f) die("cannot write aggregates.txt");
    fclose(f);
    return 0;
}