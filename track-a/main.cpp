
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace std;

constexpr size_t BUFFER_SIZE = 1 << 20;
constexpr size_t MAX_TOKEN = 1024;

struct FastInput {
    FILE* file;
    char buffer[BUFFER_SIZE];
    size_t pos = 0;
    size_t len = 0;

    explicit FastInput(const char* path) {
        file = fopen(path, "rb");
    }

    ~FastInput() {
        if (file) fclose(file);
    }

    bool valid() const {
        return file != nullptr;
    }

    int getChar() {
        if (pos == len) {
            len = fread(buffer, 1, BUFFER_SIZE, file);
            pos = 0;

            if (len == 0)
                return EOF;
        }

        return static_cast<unsigned char>(buffer[pos++]);
    }

    bool nextToken(char* dest, size_t capacity) {
        int c;

        do {
            c = getChar();
            if (c == EOF) return false;
        } while (c <= ' ');

        size_t n = 0;

        while (c > ' ') {
            if (n + 1 >= capacity)
                return false;

            dest[n++] = static_cast<char>(c);
            c = getChar();
        }

        dest[n] = '\0';
        return true;
    }
};

struct Group {
    uint32_t hourId;
    uint32_t endpointId;
    uint64_t errors = 0;
    vector<uint32_t> latencies;

    Group(uint32_t h, uint32_t e)
        : hourId(h), endpointId(e) {
        latencies.reserve(8);
    }
};

static inline uint32_t parseLatency(const char* s) {
    uint32_t value = 0;

    while (*s) {
        value = value * 10u +
                static_cast<uint32_t>(*s - '0');
        ++s;
    }

    return value;
}

static inline uint64_t makeGroupKey(
    uint32_t hourId,
    uint32_t endpointId
) {
    return (static_cast<uint64_t>(hourId) << 32)
           | endpointId;
}

static inline size_t percentileIndex(
    size_t n,
    size_t percentile
) {
    return (percentile * n + 99) / 100 - 1;
}

int main(int argc, char* argv[]) {

    string dataDir = "/data";
    string outDir = "/out";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            dataDir = argv[++i];
        }
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            outDir = argv[++i];
        }
    }

    string inputPath = dataDir + "/requests.log";
    string outputPath = outDir + "/summary.txt";

    FastInput input(inputPath.c_str());

    if (!input.valid()) {
        fprintf(stderr, "Cannot open %s\n", inputPath.c_str());
        return 1;
    }

    // Store each unique endpoint and hour only once.
    vector<string> endpoints;
    vector<string> hours;

    endpoints.reserve(1842);
    hours.reserve(128);

    unordered_map<string_view, uint32_t> endpointIds;
    unordered_map<string_view, uint32_t> hourIds;

    endpointIds.reserve(2048);
    hourIds.reserve(128);

    endpointIds.max_load_factor(0.7f);
    hourIds.max_load_factor(0.7f);

    // Map compact integer keys to group indices.
    unordered_map<uint64_t, uint32_t> groupIds;

    groupIds.reserve(150000);
    groupIds.max_load_factor(0.7f);

    vector<Group> groups;
    groups.reserve(150000);

    char timestamp[MAX_TOKEN];
    char method[MAX_TOKEN];
    char endpoint[MAX_TOKEN];
    char status[MAX_TOKEN];
    char latency[MAX_TOKEN];

    // Read and process every request.
    while (input.nextToken(timestamp, sizeof(timestamp))) {

        if (!input.nextToken(method, sizeof(method)) ||
            !input.nextToken(endpoint, sizeof(endpoint)) ||
            !input.nextToken(status, sizeof(status)) ||
            !input.nextToken(latency, sizeof(latency))) {

            fprintf(stderr, "Malformed input record\n");
            return 1;
        }

        if (strlen(timestamp) < 13 ||
            status[0] == '\0' ||
            latency[0] == '\0') {

            fprintf(stderr, "Invalid input record\n");
            return 1;
        }

        // Intern the hour.
        string_view hourView(timestamp, 13);

        auto hourIt = hourIds.find(hourView);

        uint32_t hourId;

        if (hourIt == hourIds.end()) {
            hourId = static_cast<uint32_t>(hours.size());

            hours.emplace_back(hourView);

            hourIds.emplace(
                string_view(hours.back()),
                hourId
            );
        }
        else {
            hourId = hourIt->second;
        }

        // Intern the endpoint.
        string_view endpointView(endpoint);

        auto endpointIt = endpointIds.find(endpointView);

        uint32_t endpointId;

        if (endpointIt == endpointIds.end()) {
            endpointId = static_cast<uint32_t>(
                endpoints.size()
            );

            endpoints.emplace_back(endpointView);

            endpointIds.emplace(
                string_view(endpoints.back()),
                endpointId
            );
        }
        else {
            endpointId = endpointIt->second;
        }

        // Combine hour and endpoint IDs.
        uint64_t key = makeGroupKey(
            hourId,
            endpointId
        );

        auto groupIt = groupIds.find(key);

        uint32_t groupIndex;

        if (groupIt == groupIds.end()) {

            groupIndex = static_cast<uint32_t>(
                groups.size()
            );

            groups.emplace_back(hourId, endpointId);

            groupIds.emplace(key, groupIndex);
        }
        else {
            groupIndex = groupIt->second;
        }

        Group& group = groups[groupIndex];

        // HTTP 500-599 responses are errors.
        if (status[0] == '5')
            ++group.errors;

        group.latencies.push_back(
            parseLatency(latency)
        );
    }

    // Sort groups in exact required output order.
    vector<uint32_t> order(groups.size());

    for (size_t i = 0; i < groups.size(); ++i)
        order[i] = static_cast<uint32_t>(i);

    sort(
        order.begin(),
        order.end(),
        [&](uint32_t a, uint32_t b) {

            const Group& ga = groups[a];
            const Group& gb = groups[b];

            const string& ha = hours[ga.hourId];
            const string& hb = hours[gb.hourId];

            if (ha != hb)
                return ha < hb;

            return endpoints[ga.endpointId] <
                   endpoints[gb.endpointId];
        }
    );

    // Prepare output directory.
    try {
        filesystem::create_directories(outDir);
    }
    catch (const filesystem::filesystem_error& e) {
        fprintf(stderr, "Cannot create output directory: %s\n",
                e.what());
        return 1;
    }

    FILE* output = fopen(outputPath.c_str(), "wb");

    if (!output) {
        fprintf(stderr, "Cannot create %s\n",
                outputPath.c_str());
        return 1;
    }

    // Large buffered output.
    string result;
    result.reserve(groups.size() * 90);

    char numberBuffer[32];

    auto appendNumber = [&](uint64_t value) {
        int n = snprintf(
            numberBuffer,
            sizeof(numberBuffer),
            "%llu",
            static_cast<unsigned long long>(value)
        );

        result.append(numberBuffer, static_cast<size_t>(n));
    };

    // Generate summaries.
    for (uint32_t index : order) {

        Group& group = groups[index];

        vector<uint32_t>& values = group.latencies;

        size_t n = values.size();

        if (n == 0)
            continue;

        size_t i50 = percentileIndex(n, 50);
        size_t i95 = percentileIndex(n, 95);
        size_t i99 = percentileIndex(n, 99);

        uint32_t p50, p95, p99;

        if (n <= 32) {

            sort(values.begin(), values.end());

            p50 = values[i50];
            p95 = values[i95];
            p99 = values[i99];

        } else {

            nth_element(
                values.begin(),
                values.begin() + i50,
                values.end()
            );

            p50 = values[i50];

            nth_element(
                values.begin(),
                values.begin() + i95,
                values.end()
            );

            p95 = values[i95];

            nth_element(
                values.begin(),
                values.begin() + i99,
                values.end()
            );

            p99 = values[i99];
        }

        uint64_t ppm =
            group.errors * 1000000ULL / n;

        result += hours[group.hourId];
        result += ":00:00Z ";

        result += endpoints[group.endpointId];
        result += ' ';

        appendNumber(n);
        result += ' ';

        appendNumber(p50);
        result += ' ';

        appendNumber(p95);
        result += ' ';

        appendNumber(p99);
        result += ' ';

        appendNumber(group.errors);
        result += ' ';

        appendNumber(ppm);
        result += '\n';
    }

    // Write output.
    if (!result.empty()) {
        size_t written = fwrite(
            result.data(),
            1,
            result.size(),
            output
        );

        if (written != result.size()) {
            fprintf(stderr, "Output write failed\n");
            fclose(output);
            return 1;
        }
    }

    fclose(output);

    return 0;
}
