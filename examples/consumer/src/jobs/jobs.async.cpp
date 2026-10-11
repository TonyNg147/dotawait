#include "jobs.hpp"

namespace jobs {

async int download(std::string url, int chunks) {
    for (int i = 0; i < chunks; ++i) {
        // Stand-in for waiting on the network: give other tasks a turn.
        asyncrt::yield_now().await;
    }
    return chunks * 100;
}

} // namespace jobs
