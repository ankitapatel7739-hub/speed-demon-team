#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

using namespace std;

class FastInput {
private:
    vector<char> data;
    size_t pos = 0;

public:
    explicit FastInput(const string& path) {
        ifstream file(path, ios::binary);

        if (!file) {
            cerr << "Cannot open: " << path << endl;
            exit(1);
        }

        file.seekg(0, ios::end);
        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, ios::beg);

        data.resize(size);
        file.read(data.data(), size);
    }

    inline bool next_u32(uint32_t& value) {
        const size_t len = data.size();

        while (pos < len &&
               (data[pos] < '0' || data[pos] > '9')) {
            ++pos;
        }

        if (pos >= len) {
            return false;
        }

        value = 0;

        while (pos < len) {
            char c = data[pos];

            if (c < '0' || c > '9') {
                break;
            }

            value = value * 10 + static_cast<uint32_t>(c - '0');
            ++pos;
        }

        return true;
    }
};

struct Graph {
    size_t n;

    vector<size_t> out_start;
    vector<uint32_t> out_to;
    vector<uint32_t> out_w;

    vector<size_t> in_start;
    vector<uint32_t> in_to;
    vector<uint32_t> in_w;
};

Graph read_graph(const string& path) {
    FastInput input(path);

    uint32_t n32, m32;
    input.next_u32(n32);
    input.next_u32(m32);

    size_t n = n32;
    size_t m = m32;

    vector<uint32_t> us;
    vector<uint32_t> vs;
    vector<uint32_t> ws;

    us.reserve(m);
    vs.reserve(m);
    ws.reserve(m);

    vector<size_t> out_count(n, 0);
    vector<size_t> in_count(n, 0);

    for (size_t i = 0; i < m; ++i) {
        uint32_t u, v, w;

        input.next_u32(u);
        input.next_u32(v);
        input.next_u32(w);

        us.push_back(u);
        vs.push_back(v);
        ws.push_back(w);

        ++out_count[u];
        ++in_count[v];
    }

    vector<size_t> out_start(n + 1, 0);
    vector<size_t> in_start(n + 1, 0);

    for (size_t i = 0; i < n; ++i) {
        out_start[i + 1] = out_start[i] + out_count[i];
        in_start[i + 1] = in_start[i] + in_count[i];
    }

    vector<uint32_t> out_to(m);
    vector<uint32_t> out_w(m);

    vector<uint32_t> in_to(m);
    vector<uint32_t> in_w(m);

    vector<size_t> out_pos(out_start.begin(), out_start.begin() + n);
    vector<size_t> in_pos(in_start.begin(), in_start.begin() + n);

    for (size_t i = 0; i < m; ++i) {
        uint32_t u = us[i];
        uint32_t v = vs[i];
        uint32_t w = ws[i];

        size_t p = out_pos[u]++;
        out_to[p] = v;
        out_w[p] = w;

        p = in_pos[v]++;
        in_to[p] = u;
        in_w[p] = w;
    }

    return {
        n,
        move(out_start),
        move(out_to),
        move(out_w),
        move(in_start),
        move(in_to),
        move(in_w)
    };
}

class Solver {
private:
    Graph graph;

    vector<uint64_t> dist_f;
    vector<uint64_t> dist_b;

    vector<uint32_t> mark_f;
    vector<uint32_t> mark_b;

    uint32_t query_id = 0;

    using HeapItem = pair<uint64_t, uint32_t>;

    priority_queue<
        HeapItem,
        vector<HeapItem>,
        greater<HeapItem>
    > heap_f;

    priority_queue<
        HeapItem,
        vector<HeapItem>,
        greater<HeapItem>
    > heap_b;

public:
    explicit Solver(Graph g)
        : graph(move(g)),
          dist_f(graph.n, 0),
          dist_b(graph.n, 0),
          mark_f(graph.n, 0),
          mark_b(graph.n, 0) {}

    inline int64_t shortest_path(uint32_t source, uint32_t target) {
        if (source == target) {
            return 0;
        }

        if (graph.out_start[source] ==
            graph.out_start[source + 1]) {
            return -1;
        }

        if (graph.in_start[target] ==
            graph.in_start[target + 1]) {
            return -1;
        }

        ++query_id;

        if (query_id == 0) {
            fill(mark_f.begin(), mark_f.end(), 0);
            fill(mark_b.begin(), mark_b.end(), 0);
            query_id = 1;
        }

        uint32_t qid = query_id;

        while (!heap_f.empty()) heap_f.pop();
        while (!heap_b.empty()) heap_b.pop();

        dist_f[source] = 0;
        mark_f[source] = qid;
        heap_f.push({0, source});

        dist_b[target] = 0;
        mark_b[target] = qid;
        heap_b.push({0, target});

        uint64_t best = UINT64_MAX;

        while (true) {
            while (!heap_f.empty()) {
                auto [d, u] = heap_f.top();

                if (mark_f[u] == qid && dist_f[u] == d) {
                    break;
                }

                heap_f.pop();
            }

            while (!heap_b.empty()) {
                auto [d, u] = heap_b.top();

                if (mark_b[u] == qid && dist_b[u] == d) {
                    break;
                }

                heap_b.pop();
            }

            if (heap_f.empty() || heap_b.empty()) {
                break;
            }

            uint64_t min_f = heap_f.top().first;
            uint64_t min_b = heap_b.top().first;

            if (min_f >= best - min_b) {
                break;
            }

            if (min_f <= min_b) {
                auto [d, u] = heap_f.top();
                heap_f.pop();

                if (mark_b[u] == qid) {
                    best = min(best, d + dist_b[u]);
                }

                size_t start = graph.out_start[u];
                size_t end = graph.out_start[u + 1];

                for (size_t i = start; i < end; ++i) {
                    uint32_t v = graph.out_to[i];
                    uint64_t nd =
                        d + static_cast<uint64_t>(graph.out_w[i]);

                    if (mark_f[v] != qid || nd < dist_f[v]) {
                        dist_f[v] = nd;
                        mark_f[v] = qid;

                        heap_f.push({nd, v});

                        if (mark_b[v] == qid) {
                            best = min(best, nd + dist_b[v]);
                        }
                    }
                }
            }
            else {
                auto [d, u] = heap_b.top();
                heap_b.pop();

                if (mark_f[u] == qid) {
                    best = min(best, d + dist_f[u]);
                }

                size_t start = graph.in_start[u];
                size_t end = graph.in_start[u + 1];

                for (size_t i = start; i < end; ++i) {
                    uint32_t v = graph.in_to[i];
                    uint64_t nd =
                        d + static_cast<uint64_t>(graph.in_w[i]);

                    if (mark_b[v] != qid || nd < dist_b[v]) {
                        dist_b[v] = nd;
                        mark_b[v] = qid;

                        heap_b.push({nd, v});

                        if (mark_f[v] == qid) {
                            best = min(best, nd + dist_f[v]);
                        }
                    }
                }
            }
        }

        return best == UINT64_MAX
            ? -1
            : static_cast<int64_t>(best);
    }
};

int main(int argc, char* argv[]) {
    string data_dir = "/data";
    string out_dir = "/out";

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];

        if (arg == "--data" && i + 1 < argc) {
            data_dir = argv[++i];
        }
        else if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        }
    }

    Graph graph = read_graph(data_dir + "/graph.txt");
    Solver solver(move(graph));

    FastInput queries(data_dir + "/queries.txt");

    filesystem::create_directories(out_dir);

    ofstream output(
        out_dir + "/distances.txt",
        ios::binary
    );

    uint32_t source, target;

    while (queries.next_u32(source) &&
           queries.next_u32(target)) {

        output << solver.shortest_path(source, target) << '\n';
    }

    return 0;
}