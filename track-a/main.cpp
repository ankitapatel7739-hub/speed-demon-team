#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

using namespace std;

struct Stats {
    vector<int> latencies;
    int errors = 0;
};

int main(int argc, char* argv[]) {
    string data_dir = "/data";
    string out_dir = "/out";

    // Accept the same arguments as the Python implementation
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];

        if (arg == "--data" && i + 1 < argc) {
            data_dir = argv[++i];
        } 
        else if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        }
    }

    string input_path = data_dir + "/requests.log";

    ifstream input(input_path);

    if (!input) {
        cerr << "Cannot open " << input_path << endl;
        return 1;
    }

    // Key: hour + separator + endpoint
    unordered_map<string, Stats> groups;

    string timestamp;
    string method;
    string endpoint;
    int status;
    int latency;

    while (input >> timestamp >> method >> endpoint >> status >> latency) {
        // YYYY-MM-DDTHH
        string hour = timestamp.substr(0, 13);

        string key = hour;
        key.push_back('\x1F');
        key += endpoint;

        Stats& stats = groups[key];

        stats.latencies.push_back(latency);

        if (status >= 500 && status < 600) {
            stats.errors++;
        }
    }

    // Sort keys so output matches required deterministic order
    vector<string> keys;
    keys.reserve(groups.size());

    for (const auto& item : groups) {
        keys.push_back(item.first);
    }

    sort(keys.begin(), keys.end());

    filesystem::create_directories(out_dir);

    string output_path = out_dir + "/summary.txt";

    ofstream output(output_path);

    if (!output) {
        cerr << "Cannot create " << output_path << endl;
        return 1;
    }

    for (const string& key : keys) {
        Stats& stats = groups[key];

        sort(stats.latencies.begin(), stats.latencies.end());

        int n = static_cast<int>(stats.latencies.size());

        int p50_index = (50 * n + 99) / 100 - 1;
        int p95_index = (95 * n + 99) / 100 - 1;
        int p99_index = (99 * n + 99) / 100 - 1;

        int p50 = stats.latencies[p50_index];
        int p95 = stats.latencies[p95_index];
        int p99 = stats.latencies[p99_index];

        long long ppm =
            static_cast<long long>(stats.errors) * 1000000 / n;

        size_t separator = key.find('\x1F');

        string hour = key.substr(0, separator);
        string endpoint = key.substr(separator + 1);

        output
            << hour << ":00:00Z "
            << endpoint << " "
            << n << " "
            << p50 << " "
            << p95 << " "
            << p99 << " "
            << stats.errors << " "
            << ppm << "\n";
    }

    return 0;
}