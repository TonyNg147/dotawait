#pragma once

#include <asyncrt/task.hpp>

#include <string>

namespace jobs {

// Pretends to download `url` in `chunks` pieces, letting other tasks run
// between them. Returns the number of bytes "downloaded".
asyncrt::Task<int> download(std::string url, int chunks);

} // namespace jobs
