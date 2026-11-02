#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace std;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint8_t u8;
struct Edge { u32 u, v, w; };
inline u32 parse_u32(const char*& p) {
    u32 v = (u8)*p++ - '0';
    while ((u8)*p >= '0') {
        v = v * 10 + ((u8)*p++ - '0');
    }
    ++p;
    return v;
}
int main() {
    string path = "/work/data/d-full/graph.txt";
    int fd = open(path.c_str(), O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    size_t len = st.st_size;
    const char* data = (const char*)mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    const char* p = data;
    u32 n = parse_u32(p);
    u32 m = parse_u32(p);
    int T = 8;
    const char* body = p;
    size_t blen = (data + len) - body;
    vector<const char*> cut(T + 1);
    cut[0] = body;
    cut[T] = data + len;
    for (int i = 1; i < T; ++i) {
        const char* c = body + (blen / T) * i;
        while (c < data + len && *c != '\n') ++c;
        if (c < data + len) ++c;
        cut[i] = c;
    }
     auto t0 = chrono::steady_clock::now();
    vector<atomic<u32>> deg_out(n), deg_in(n);
    for (u32 i = 0; i < n; ++i) {
        deg_out[i].store(0, memory_order_relaxed);
        deg_in[i].store(0, memory_order_relaxed);
    }
    vector<vector<Edge>> thread_edges(T);
    vector<thread> th;
    for (int t = 0; t < T; ++t) {
        th.emplace_back([&, t]() {
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
    }
    for (auto& t : th) t.join();
    auto t1 = chrono::steady_clock::now();
    double ms = chrono::duration<double, milli>(t1 - t0).count();
    cout << "Atomic parse+deg: " << ms << " ms" << endl;
    return 0;
}