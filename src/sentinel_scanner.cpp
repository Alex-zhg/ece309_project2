#include "core/sentinel_scanner.h"
#include <algorithm>

SentinelScanner::SentinelScanner(std::string sentinel) : sentinel_(std::move(sentinel)) {}

SentinelScanner::Out SentinelScanner::feed(std::string_view chunk) {
    if (sentinel_.empty()) return {std::string(chunk), false};

    std::string search_text = pending_ + std::string(chunk);
    std::size_t pos = search_text.find(sentinel_);

    if (pos != std::string::npos) {
        std::string safe = search_text.substr(0, pos);
        pending_.clear();
        return {safe, true};
    }

    std::size_t keep_len = std::min(search_text.size(), sentinel_.size() - 1);
    std::size_t safe_len = search_text.size() - keep_len;
    
    std::string safe = search_text.substr(0, safe_len);
    pending_ = search_text.substr(safe_len);

    return {safe, false};
}

SentinelScanner::Out SentinelScanner::flush() {
    std::string safe = std::move(pending_);
    pending_.clear();
    return {safe, false};
}