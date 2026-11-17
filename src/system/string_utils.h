#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <string>

//==============================================================================
// STRING HELPERS
//==============================================================================
// The predicates std::string gained in C++20, plus the search, slicing and
// numeric conversions the firmware needs. Kept here so the device build and the
// C++17 desktop simulator share one implementation.

namespace strings {

inline bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

/** Suffix test that ignores ASCII letter case, as file-extension checks need. */
inline bool ends_with_ignore_case(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           std::equal(suffix.begin(), suffix.end(), value.end() - suffix.size(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

/** Offset of the first occurrence, or -1 when absent. */
template <typename Needle>
inline int index_of(const std::string& value, const Needle& needle, size_t from = 0) {
    const size_t position = value.find(needle, from);
    return position == std::string::npos ? -1 : static_cast<int>(position);
}

/** Substring between two offsets, mirroring the half-open [begin, end) range. */
inline std::string slice(const std::string& value, int begin, int end) {
    if (begin < 0 || end <= begin || static_cast<size_t>(begin) >= value.size()) return {};
    const size_t stop = std::min(static_cast<size_t>(end), value.size());
    return value.substr(static_cast<size_t>(begin), stop - static_cast<size_t>(begin));
}

/** Leading decimal digits as an unsigned value; 0 when there are none. */
inline uint32_t to_uint32(const std::string& value) {
    return static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
}

/** Leading decimal digits as a signed value; 0 when there are none. */
inline int32_t to_int32(const std::string& value) {
    return static_cast<int32_t>(std::strtol(value.c_str(), nullptr, 10));
}

/** Leading decimal number as a float; 0 when there is none. */
inline float to_float(const std::string& value) {
    return std::strtof(value.c_str(), nullptr);
}

/** Drop leading and trailing ASCII whitespace. */
inline std::string trim(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

}  // namespace strings
