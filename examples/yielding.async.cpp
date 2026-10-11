#include <iostream>
#include <string>
#include <vector>

using asyncrt::yield_now;

std::vector<std::string> g_log;

async void worker(std::string name) {
    g_log.push_back(name + "0");
    yield_now().await;
    g_log.push_back(name + "1");
    yield_now().await;
    g_log.push_back(name + "2");
}

int main() {
    // spawn() queues each task on the executor instead of running it to
    // completion, so run() interleaves them through the ready queue.
    asyncrt::spawn(worker("a"));
    asyncrt::spawn(worker("b"));
    asyncrt::run();

    for (const auto& s : g_log) std::cout << s << " ";
    std::cout << std::endl;

    std::vector<std::string> expected = {"a0", "b0", "a1", "b1", "a2", "b2"};
    return g_log == expected ? 0 : 1;
}
