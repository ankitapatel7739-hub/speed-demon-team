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
struct Edge { u32 u, v, w; };
struct Arc { u32 to, w; };
inline u32 parse_u32(const char*& p) {
    u32 v = (u8)*p++ - '0';
    while ((u8)*p >= '0') {
        v = v * 10 + ((u8)*p++ - '0');
    }
    ++p;
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
    vector<u32*> deg_out(T), deg_in(T);
    for (int t = 0; t < T; ++t) {
        deg_out[t] = (u32*)calloc(n, sizeof(u32));
        deg_in[t] = (u32*)calloc(n, sizeof(u32));
    }
    auto t_start_parse = chrono::steady_clock::now();
    vector<thread> th;
    for (int t = 0; t < T; ++t) {
        th.emplace_back([&, t]() {
            const char* cur = cut[t];
            const char* end = cut[t + 1];
            auto& edges = thread_edges[t];
            edges.reserve(m / T + 10000);
            u32* do_ptr = deg_out[t];
            u32* di_ptr = deg_in[t];
            while (cur < end) {
                u32 u = parse_u32(cur);
                u32 v = parse_u32(cur);
                u32 w = parse_u32(cur);
                edges.push_back({u, v, w});
                do_ptr[u]++;
                di_ptr[v]++;
            }
        });
    }
     for (auto& t : th) t.join();
    th.clear();
    auto t_parsed = chrono::steady_clock::now();
    // Now prefix sums and offsets
    vector<u32> start_out(n + 1, 0), start_in(n + 1, 0);
    // pos[t][u] will store offset for thread t at node u
    vector<u32*> pos_out(T), pos_in(T);
    for (int t = 0; t < T; ++t) {
        pos_out[t] = (u32*)malloc(n * sizeof(u32));
        pos_in[t] = (u32*)malloc(n * sizeof(u32));
    }
    // Parallel reduce across threads
    for (int t = 0; t < T; ++t) {
        th.emplace_back([&, t]() {
            u32 u0 = (u64)n * t / T;
            u32 u1 = (u64)n * (t + 1) / T;
            for (u32 u = u0; u < u1; ++u) {
                u32 so = 0, si = 0;
                for (int j = 0; j < T; ++j) {
                    pos_out[j][u] = so;
                    so += deg_out[j][u];
                    pos_in[j][u] = si;
                    si += deg_in[j][u];
                }
                start_out[u + 1] = so;
                start_in[u + 1] = si;
            }
        });
    }
     for (auto& t : th) t.join();
    th.clear();
    // Global prefix sums for start_out and start_in
    for (u32 i = 0; i < n; ++i) {
        start_out[i + 1] += start_out[i];
        start_in[i + 1] += start_in[i];
    }
    // Adjust pos_out and pos_in with global start
    for (int t = 0; t < T; ++t) {
        th.emplace_back([&, t]() {
            u32 u0 = (u64)n * t / T;
            u32 u1 = (u64)n * (t + 1) / T;
            for (u32 u = u0; u < u1; ++u) {
                u32 so = start_out[u];
                u32 si = start_in[u];
                for (int j = 0; j < T; ++j) {
                    pos_out[j][u] += so;
                    pos_in[j][u] += si;
                }
            }
        });
    }
     for (auto& t : th) t.join();
    th.clear();
    // Now allocate arcs
    vector<Arc> arcs_out(m), arcs_in(m);
    // Parallel scatter
    for (int t = 0; t < T; ++t) {
        th.emplace_back([&, t]() {
            u32* po = pos_out[t];
            u32* pi = pos_in[t];
            Arc* ao = arcs_out.data();
            Arc* ai = arcs_in.data();
            for (const auto& e : thread_edges[t]) {
                ao[po[e.u]++] = {e.v, e.w};
                ai[pi[e.v]++] = {e.u, e.w};
            }
        });
    }
    for (auto& t : th) t.join();
    auto t_done = chrono::steady_clock::now();
    double parse_ms = chrono::duration<double, milli>(t_parsed - t_start_parse).count();
    double total_ms = chrono::duration<double, milli>(t_done - t0).count();
    double csr_ms = chrono::duration<double, milli>(t_done - t_parsed).count();
    cout << "Parse + degrees: " << parse_ms << " ms" << endl;
    cout << "CSR build:       " << csr_ms << " ms" << endl;
    cout << "Total:           " << total_ms << " ms" << endl;
    return 0;
}