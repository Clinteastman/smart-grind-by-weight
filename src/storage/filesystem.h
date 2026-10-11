#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

//==============================================================================
// LITTLEFS STORAGE
//==============================================================================
// Grind sessions, the autotune log and the custom screensaver frame live on the
// LittleFS partition labelled "spiffs" in partitions.csv.
//
// Paths used throughout the firmware are partition-absolute ("/sessions/...").
// This wrapper prefixes them with the VFS mount point, so callers never have to
// know where the partition is mounted.

/**
 * An open file or directory handle. Move-only, and closes itself on destruction
 * so an early return cannot leak a descriptor during a grind.
 */
class FsFile {
public:
    FsFile() = default;
    ~FsFile();

    FsFile(FsFile&& other) noexcept;
    FsFile& operator=(FsFile&& other) noexcept;
    FsFile(const FsFile&) = delete;
    FsFile& operator=(const FsFile&) = delete;

    /** True when the handle refers to an open file or directory. */
    explicit operator bool() const { return file_ != nullptr || dir_ != nullptr; }

    bool isDirectory() const { return dir_ != nullptr; }

    /** Base name of the entry, without its directory. */
    const char* name() const { return name_.c_str(); }

    /** Partition-absolute path of the entry. */
    const char* path() const { return path_.c_str(); }

    size_t read(uint8_t* buffer, size_t length);
    size_t readBytes(char* buffer, size_t length);
    size_t write(const uint8_t* buffer, size_t length);

    int println(const char* text = "");
    int printf(const char* format, ...) __attribute__((format(printf, 2, 3)));

    bool seek(size_t position);
    size_t position() const;
    size_t size() const;
    size_t available() const;
    void flush();
    void close();

    /**
     * Advance a directory handle to the next regular file or subdirectory and
     * return it opened for reading. Returns a closed handle at the end.
     */
    FsFile openNextFile();

private:
    friend class Filesystem;

    /** Open a single entry; `mode` follows fopen(). */
    static FsFile open_entry(const std::string& logical_path, const char* mode);

    /** Write `text` without a line ending; println()'s building block. */
    int print(const char* text);

    FILE* file_ = nullptr;
    bool writable_ = false;  // only a write handle needs flushing before stat
    void* dir_ = nullptr;  // DIR*, kept opaque so callers need no dirent.h
    std::string path_;     // partition-absolute
    std::string name_;
};

class Filesystem {
public:
    /**
     * Mount the LittleFS partition.
     *
     * @param format_if_mount_failed Format and retry when the partition cannot
     *        be mounted, matching the previous boot behaviour.
     */
    bool begin(bool format_if_mount_failed = false);
    void end();

    /**
     * True while the partition is mounted. A constant-time lookup that touches
     * no flash, unlike a usage query, which walks the whole filesystem.
     */
    bool is_mounted() const;

    /** Open a file, or a directory when `mode` describes a read. */
    FsFile open(const char* path, const char* mode = "r");

    bool exists(const char* path);
    bool remove(const char* path);
    bool rename(const char* from, const char* to);
    bool mkdir(const char* path);

private:
    /** Absolute VFS path for `path`. */
    std::string to_vfs_path(const char* path) const;

    bool mounted_ = false;
};

extern Filesystem filesystem;
