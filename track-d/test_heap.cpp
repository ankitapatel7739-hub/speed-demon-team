#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <chrono>
using namespace std;
typedef uint32_t u32;
typedef uint64_t u64;
struct Item {
    u64 d;
    u32 v;
};
// 4-ary min-heap
struct QuadHeap {
    vector<Item> a;
    inline void clear() {
        a.clear();
    }
    inline bool empty() const {
        return a.empty();
    }
    inline size_t size() const {
        return a.size();
    }
    inline const Item& top() const {
        return a[0];
    }
    inline void push(u64 d, u32 v) {
        size_t i = a.size();
        a.push_back({d, v});
        while (i > 0) {
            size_t p = (i - 1) >> 2;
            if (a[p].d <= d) break;
            a[i] = a[p];
            i = p;
        }
        a[i] = {d, v};
    }
    inline void pop() {
        Item val = a.back();
        a.pop_back();
        if (a.empty()) return;
        size_t i = 0, n = a.size();
        while (true) {
            size_t c = (i << 2) + 1;
            if (c >= n) break;
            size_t best = c;
            u64 best_d = a[c].d;
            if (c + 1 < n && a[c + 1].d < best_d) { best = c + 1; best_d = a[c + 1].d; }
            if (c + 2 < n && a[c + 2].d < best_d) { best = c + 2; best_d = a[c + 2].d; }
            if (c + 3 < n && a[c + 3].d < best_d) { best = c + 3; best_d = a[c + 3].d; }
            if (val.d <= best_d) break;
            a[i] = a[best];
            i = best;
        }
        a[i] = val;
    }
};
int main() {
    cout << "QuadHeap defined" << endl;
    return 0;
}