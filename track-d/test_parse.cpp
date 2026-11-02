#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace std;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint8_t u8;
struct Edge {
    u32 u, v, w;
};
inine u32 parse_u32(const char*& p) {
    u32 v = (u8)*p++ - '0';
    while ((u8)*p >= '0') {
        v = v * 10 + ((u8)*p++ - '0');
    }
    ++p; // skip ' ' or '\n'
    return v;
}
int main(int argc, char** argv) {
    string path = "/work/data/d-full/graph.txt";
    int fd = open(path.c_str(), O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    size_t len = st.st_size;
    const char* data = (const char*)mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    auto t0 = chrono::steady_clock::now();
    // Parse header
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
            }
        });
    }
    for (auto& t : th) t.join();
    auto t1 = chrono::steady_clock::now();
    double ms = chrono::duration<double, milli>(t1 - t0).count();
    size_t total_edges = 0;
    for (int t = 0; t < T; ++t) total_edges += thread_edges[t].size();
    cout << "Parsed n=" << n << ", m=" << total_edges << " in " << ms << " ms ("
         << (len / (1024.0 * 1024.0)) / (ms / 1000.0) << " MB/s)" << endl;
    return 0;
}