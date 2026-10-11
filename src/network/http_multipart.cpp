#include "http_multipart.h"

namespace {

constexpr char kCrLf[] = "\r\n";
constexpr char kHeaderEnd[] = "\r\n\r\n";

/** Case-insensitive search, since header names arrive in any casing. */
size_t find_insensitive(const std::string& haystack, const std::string& needle,
                        size_t from = 0) {
    if (needle.empty() || haystack.size() < needle.size()) return std::string::npos;
    for (size_t i = from; i + needle.size() <= haystack.size(); ++i) {
        size_t j = 0;
        while (j < needle.size()) {
            const char a = haystack[i + j];
            const char b = needle[j];
            const char lower_a = (a >= 'A' && a <= 'Z') ? static_cast<char>(a + 32) : a;
            const char lower_b = (b >= 'A' && b <= 'Z') ? static_cast<char>(b + 32) : b;
            if (lower_a != lower_b) break;
            ++j;
        }
        if (j == needle.size()) return i;
    }
    return std::string::npos;
}

/** Value of a quoted `key="value"` attribute inside a header block. */
std::string quoted_attribute(const std::string& headers, const std::string& key) {
    const size_t key_position = find_insensitive(headers, key + "=\"");
    if (key_position == std::string::npos) return {};
    const size_t start = key_position + key.size() + 2;
    const size_t end = headers.find('"', start);
    if (end == std::string::npos) return {};
    return headers.substr(start, end - start);
}

}  // namespace

bool MultipartReader::begin(const std::string& content_type) {
    if (find_insensitive(content_type, "multipart/form-data") == std::string::npos) {
        return false;
    }

    const size_t marker = find_insensitive(content_type, "boundary=");
    if (marker == std::string::npos) return false;

    std::string value = content_type.substr(marker + 9);
    // The boundary may be quoted and may be followed by further parameters.
    if (!value.empty() && value.front() == '"') {
        const size_t end = value.find('"', 1);
        value = end == std::string::npos ? value.substr(1) : value.substr(1, end - 1);
    } else {
        const size_t end = value.find_first_of("; \t\r\n");
        if (end != std::string::npos) value = value.substr(0, end);
    }
    if (value.empty()) return false;

    boundary_ = "--" + value;
    delimiter_ = std::string(kCrLf) + boundary_;
    state_ = State::SEEK_FIRST_BOUNDARY;
    pending_.clear();
    filename_.clear();
    forwarding_ = false;
    saw_file_ = false;
    bytes_forwarded_ = 0;
    return true;
}

bool MultipartReader::forward(size_t length, const DataCallback& on_data) {
    if (!forwarding_) return true;
    if (!on_data(reinterpret_cast<const uint8_t*>(pending_.data()), length)) return false;
    saw_file_ = true;
    bytes_forwarded_ += length;
    return true;
}

size_t MultipartReader::retain_length() const {
    // A boundary occurrence in the payload is preceded by CRLF and may be
    // followed by "--" for the final one.
    return boundary_.size() + 4;
}

bool MultipartReader::feed(const uint8_t* data, size_t length, const DataCallback& on_data) {
    if (state_ == State::UNINITIALISED) return false;
    if (state_ == State::COMPLETE) return true;

    if (data && length > 0) {
        pending_.append(reinterpret_cast<const char*>(data), length);
    }

    while (true) {
        if (state_ == State::SEEK_FIRST_BOUNDARY) {
            const size_t position = pending_.find(boundary_);
            if (position == std::string::npos) {
                // Preamble before the first boundary is discarded.
                if (pending_.size() > retain_length()) {
                    pending_.erase(0, pending_.size() - retain_length());
                }
                return true;
            }
            pending_.erase(0, position + boundary_.size());
            state_ = State::PART_HEADERS;
            continue;
        }

        if (state_ == State::PART_HEADERS) {
            // A closing boundary arrives as "--" straight after the delimiter.
            if (pending_.size() >= 2 && pending_.compare(0, 2, "--") == 0) {
                state_ = State::COMPLETE;
                pending_.clear();
                return true;
            }
            const size_t header_end = pending_.find(kHeaderEnd);
            if (header_end == std::string::npos) return true;  // wait for more

            const std::string headers = pending_.substr(0, header_end);
            const std::string name = quoted_attribute(headers, "filename");
            // Only the first file is the upload; text fields and any later
            // file are read past without reaching on_data.
            forwarding_ = !name.empty() && filename_.empty();
            if (forwarding_) filename_ = name;
            pending_.erase(0, header_end + 4);
            state_ = State::PART_DATA;
            continue;
        }

        // State::PART_DATA
        const size_t position = pending_.find(delimiter_);
        if (position == std::string::npos) {
            // Forward everything that cannot be the start of the delimiter.
            if (pending_.size() > retain_length()) {
                const size_t emit = pending_.size() - retain_length();
                if (!forward(emit, on_data)) return false;
                pending_.erase(0, emit);
            }
            return true;
        }

        if (position > 0 && !forward(position, on_data)) return false;
        pending_.erase(0, position + delimiter_.size());
        state_ = State::PART_HEADERS;
    }
}
