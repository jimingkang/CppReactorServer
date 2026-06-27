#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

struct Config {
    std::string host = "127.0.0.1";
    int port = 7779;
    std::uint64_t totalRequests = 1'000'000;
    int concurrency = static_cast<int>(std::max(1u, std::thread::hardware_concurrency() * 4));
    std::string command = "PING";
    std::string expectedPrefix = "PONG";
};

struct WorkerStats {
    std::uint64_t success = 0;
    std::uint64_t failed = 0;
    std::vector<double> latencyUs;
};

int connectToServer(const Config& config) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(config.port));
    if (inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool sendAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = send(fd, data.data() + offset, data.size() - offset, kSendFlags);
        if (n > 0) {
            offset += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool readLine(int fd, std::string& pending, std::string& line) {
    while (true) {
        const std::size_t pos = pending.find('\n');
        if (pos != std::string::npos) {
            line = pending.substr(0, pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            pending.erase(0, pos + 1);
            return true;
        }

        char buffer[4096];
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            pending.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

void workerMain(
    const Config& config,
    std::uint64_t requestCount,
    std::atomic_uint64_t& completed,
    WorkerStats& stats) {
    if (requestCount == 0) {
        return;
    }

    const int fd = connectToServer(config);
    if (fd < 0) {
        stats.failed += requestCount;
        completed += requestCount;
        return;
    }

    std::string pending;
    std::string line;
    if (!readLine(fd, pending, line)) {
        stats.failed += requestCount;
        completed += requestCount;
        close(fd);
        return;
    }

    const std::string request = config.command + "\n";
    stats.latencyUs.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(requestCount, 200000)));

    for (std::uint64_t i = 0; i < requestCount; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        if (!sendAll(fd, request) || !readLine(fd, pending, line)) {
            ++stats.failed;
            ++completed;
            continue;
        }

        const auto end = std::chrono::steady_clock::now();
        const bool ok = line.rfind(config.expectedPrefix, 0) == 0;
        if (ok) {
            ++stats.success;
            if (stats.latencyUs.size() < 200000) {
                const auto latency = std::chrono::duration<double, std::micro>(end - begin).count();
                stats.latencyUs.push_back(latency);
            }
        } else {
            ++stats.failed;
        }
        ++completed;
    }

    sendAll(fd, "QUIT\n");
    close(fd);
}

Config parseArgs(int argc, char** argv) {
    Config config;
    if (argc > 1) {
        config.host = argv[1];
    }
    if (argc > 2) {
        config.port = std::atoi(argv[2]);
    }
    if (argc > 3) {
        config.totalRequests = std::strtoull(argv[3], nullptr, 10);
    }
    if (argc > 4) {
        config.concurrency = std::max(1, std::atoi(argv[4]));
    }
    if (argc > 5) {
        config.command = argv[5];
    }
    if (argc > 6) {
        config.expectedPrefix = argv[6];
    }
    return config;
}

double percentile(std::vector<double>& values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>((values.size() - 1) * p);
    return values[index];
}

} // namespace

int main(int argc, char** argv) {
    const Config config = parseArgs(argc, argv);
    if (config.totalRequests == 0 || config.concurrency <= 0) {
        std::cerr << "usage: stress_client [host] [port] [total_requests] [concurrency] [command] [expected_prefix]\n";
        return 1;
    }

    std::cout << "stress_client host=" << config.host << " port=" << config.port
              << " total=" << config.totalRequests << " concurrency=" << config.concurrency
              << " command=" << config.command << " expected=" << config.expectedPrefix << '\n';

    std::atomic_uint64_t completed{0};
    std::vector<WorkerStats> stats(static_cast<std::size_t>(config.concurrency));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(config.concurrency));

    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t base = config.totalRequests / static_cast<std::uint64_t>(config.concurrency);
    const std::uint64_t extra = config.totalRequests % static_cast<std::uint64_t>(config.concurrency);

    std::atomic_bool reporting{true};
    std::thread reporter([&] {
        while (reporting.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - start).count();
            const std::uint64_t done = completed.load();
            std::cout << "progress " << done << '/' << config.totalRequests
                      << " qps=" << static_cast<std::uint64_t>(done / std::max(seconds, 0.001)) << '\n';
        }
    });

    for (int i = 0; i < config.concurrency; ++i) {
        const std::uint64_t requestCount = base + (static_cast<std::uint64_t>(i) < extra ? 1 : 0);
        threads.emplace_back(workerMain, std::cref(config), requestCount, std::ref(completed), std::ref(stats[i]));
    }

    for (auto& thread : threads) {
        thread.join();
    }
    reporting.store(false);
    reporter.join();

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();

    std::uint64_t success = 0;
    std::uint64_t failed = 0;
    std::vector<double> latencies;
    for (auto& stat : stats) {
        success += stat.success;
        failed += stat.failed;
        latencies.insert(latencies.end(), stat.latencyUs.begin(), stat.latencyUs.end());
    }

    const double averageLatency = latencies.empty()
        ? 0.0
        : std::accumulate(latencies.begin(), latencies.end(), 0.0) / static_cast<double>(latencies.size());
    const double p50 = percentile(latencies, 0.50);
    const double p95 = percentile(latencies, 0.95);
    const double p99 = percentile(latencies, 0.99);

    std::cout << "done success=" << success << " failed=" << failed
              << " seconds=" << seconds
              << " qps=" << static_cast<std::uint64_t>(static_cast<double>(success) / std::max(seconds, 0.001))
              << " avg_us=" << averageLatency
              << " p50_us=" << p50
              << " p95_us=" << p95
              << " p99_us=" << p99 << '\n';

    return failed == 0 ? 0 : 2;
}
