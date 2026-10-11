#include "report.hpp"

std::string describe(const std::string& url, int bytes) {
    return url + " (" + std::to_string(bytes) + " bytes)";
}
