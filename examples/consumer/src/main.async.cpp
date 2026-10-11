#include "jobs/jobs.hpp"
#include "report.hpp"

#include <iostream>
#include <string>
#include <vector>

std::vector<std::string> g_finished;

// Awaits other async functions like ordinary calls.
async int download_both(std::string a, std::string b) {
    int first = jobs::download(a, 1).await;
    auto second = jobs::download(b, 2);
    return first + second.await;
}

async void download_and_log(std::string url, int chunks) {
    int bytes = jobs::download(url, chunks).await;
    g_finished.push_back(describe(url, bytes));
}

int main() {
    // 1. Drive a single task to its result.
    int total = asyncrt::block_on(download_both("a.txt", "b.txt"));
    std::cout << "sequential total: " << total << "\n";

    // 2. Run several tasks concurrently; shorter downloads finish first.
    asyncrt::spawn(download_and_log("big.bin", 3));
    asyncrt::spawn(download_and_log("medium.bin", 2));
    asyncrt::spawn(download_and_log("tiny.bin", 1));
    asyncrt::run();

    std::cout << "finished in order:\n";
    for (const auto& line : g_finished) std::cout << "  " << line << "\n";

    std::vector<std::string> expected = {
        "tiny.bin (100 bytes)", "medium.bin (200 bytes)", "big.bin (300 bytes)"};
    return total == 300 && g_finished == expected ? 0 : 1;
}
