#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>

//==============================================================================
// STREAMING MULTIPART/FORM-DATA READER
//==============================================================================
// The device page uploads firmware images and screensaver frames as
// multipart/form-data, which esp_http_server does not parse. Both payloads are
// far larger than free internal RAM, so this reader is fed the request body in
// chunks and forwards only the file bytes, buffering just enough to recognise a
// boundary that straddles two chunks.
//
// It deliberately has no ESP-IDF dependency so the desktop test build can
// exercise it.

class MultipartReader {
public:
    /**
     * Receives the payload of the first part that carries a file name, in
     * order; other form fields are skipped. Return false to abort the upload.
     */
    using DataCallback = std::function<bool(const uint8_t* data, size_t length)>;

    /**
     * Extract the boundary from a Content-Type header value.
     *
     * @return false when the header is not multipart/form-data or has no boundary.
     */
    bool begin(const std::string& content_type);

    /**
     * Consume a chunk of request body.
     *
     * @return false when the stream is malformed or `on_data` aborted.
     */
    bool feed(const uint8_t* data, size_t length, const DataCallback& on_data);

    /** True once the closing boundary has been seen. */
    bool complete() const { return state_ == State::COMPLETE; }

    /** True once any file payload byte has been forwarded. */
    bool saw_file() const { return saw_file_; }

    /** The `filename` from the part's Content-Disposition header, if any. */
    const std::string& filename() const { return filename_; }

    /** Total payload bytes forwarded so far. */
    size_t bytes_forwarded() const { return bytes_forwarded_; }

private:
    enum class State : uint8_t {
        UNINITIALISED,
        SEEK_FIRST_BOUNDARY,
        PART_HEADERS,
        PART_DATA,
        COMPLETE,
    };

    /** Longest suffix that could still turn into a boundary in the next chunk. */
    size_t retain_length() const;

    /** Hand the first `length` pending bytes to on_data if this part is the file. */
    bool forward(size_t length, const DataCallback& on_data);

    State state_ = State::UNINITIALISED;
    std::string boundary_;   // "--<boundary>"
    std::string delimiter_;  // CRLF + boundary_, as it appears after a part
    std::string pending_;    // bytes not yet classified
    std::string filename_;
    bool forwarding_ = false;  // the current part's payload goes to on_data
    bool saw_file_ = false;
    size_t bytes_forwarded_ = 0;
};
