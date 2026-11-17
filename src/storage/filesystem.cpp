#include "filesystem.h"
#include <cstdint>
#include <cstdio>

#include <dirent.h>
#include <esp_littlefs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "../config/constants.h"

Filesystem filesystem;

namespace {

// Partition label from partitions.csv. It stays "spiffs" so an existing grinder
// keeps its grind history and screensaver after updating to this firmware.
constexpr char kPartitionLabel[] = "spiffs";
constexpr char kMountPath[] = "/littlefs";

std::string join_mount(const std::string& mount_path, const std::string& logical_path) {
    if (logical_path.empty() || logical_path == "/") return mount_path;
    if (logical_path.front() != '/') return mount_path + "/" + logical_path;
    return mount_path + logical_path;
}

std::string base_name(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/** True when `mode` only reads, which is the case where a directory is valid. */
bool is_read_mode(const char* mode) {
    return mode && mode[0] == 'r' && !strchr(mode, '+');
}

}  // namespace

//==============================================================================
// FsFile
//==============================================================================

FsFile::~FsFile() {
    close();
}

FsFile::FsFile(FsFile&& other) noexcept
    : file_(other.file_),
      writable_(other.writable_),
      dir_(other.dir_),
      path_(std::move(other.path_)),
      name_(std::move(other.name_)) {
    other.file_ = nullptr;
    other.dir_ = nullptr;
}

FsFile& FsFile::operator=(FsFile&& other) noexcept {
    if (this != &other) {
        close();
        file_ = other.file_;
        writable_ = other.writable_;
        dir_ = other.dir_;
        path_ = std::move(other.path_);
        name_ = std::move(other.name_);
        other.file_ = nullptr;
        other.dir_ = nullptr;
    }
    return *this;
}

void FsFile::close() {
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
    if (dir_) {
        closedir(static_cast<DIR*>(dir_));
        dir_ = nullptr;
    }
}

FsFile FsFile::open_entry(const std::string& logical_path, const char* mode) {
    FsFile handle;
    const std::string vfs_path = join_mount(kMountPath, logical_path);

    struct stat info = {};
    const bool exists = stat(vfs_path.c_str(), &info) == 0;

    if (exists && S_ISDIR(info.st_mode)) {
        if (!is_read_mode(mode)) return handle;
        DIR* dir = opendir(vfs_path.c_str());
        if (!dir) return handle;
        handle.dir_ = dir;
    } else {
        FILE* file = fopen(vfs_path.c_str(), mode);
        if (!file) return handle;
        handle.file_ = file;
        handle.writable_ = !is_read_mode(mode);
    }

    handle.path_ = logical_path;
    handle.name_ = base_name(logical_path);
    return handle;
}

size_t FsFile::read(uint8_t* buffer, size_t length) {
    if (!file_ || !buffer || length == 0) return 0;
    return fread(buffer, 1, length, file_);
}

size_t FsFile::readBytes(char* buffer, size_t length) {
    return read(reinterpret_cast<uint8_t*>(buffer), length);
}

size_t FsFile::write(const uint8_t* buffer, size_t length) {
    if (!file_ || !buffer || length == 0) return 0;
    return fwrite(buffer, 1, length, file_);
}

int FsFile::print(const char* text) {
    if (!file_ || !text) return 0;
    const int written = fputs(text, file_);
    return written < 0 ? 0 : static_cast<int>(strlen(text));
}

int FsFile::println(const char* text) {
    // CRLF matches the line endings the exported autotune log has always used.
    const int written = print(text);
    return written + print("\r\n");
}

int FsFile::printf(const char* format, ...) {
    if (!file_ || !format) return 0;
    va_list args;
    va_start(args, format);
    const int written = vfprintf(file_, format, args);
    va_end(args);
    return written < 0 ? 0 : written;
}

bool FsFile::seek(size_t position) {
    if (!file_) return false;
    return fseek(file_, static_cast<long>(position), SEEK_SET) == 0;
}

size_t FsFile::position() const {
    if (!file_) return 0;
    const long offset = ftell(file_);
    return offset < 0 ? 0 : static_cast<size_t>(offset);
}

size_t FsFile::size() const {
    if (!file_) return 0;
    // Only a write handle needs flushing; on a read stream fflush discards the
    // buffered read-ahead and forces a re-read from flash.
    if (writable_) fflush(file_);
    struct stat info = {};
    if (fstat(fileno(file_), &info) != 0) return 0;
    return static_cast<size_t>(info.st_size);
}

size_t FsFile::available() const {
    const size_t total = size();
    const size_t offset = position();
    return offset >= total ? 0 : total - offset;
}

void FsFile::flush() {
    if (file_) fflush(file_);
}

FsFile FsFile::openNextFile() {
    FsFile handle;
    if (!dir_) return handle;

    while (const struct dirent* entry = readdir(static_cast<DIR*>(dir_))) {
        if (entry->d_type != DT_REG && entry->d_type != DT_DIR) continue;

        std::string child = path_;
        if (child.empty() || child.back() != '/') child += '/';
        child += entry->d_name;
        return open_entry(child, "r");
    }
    return handle;
}

//==============================================================================
// Filesystem
//==============================================================================

bool Filesystem::begin(bool format_if_mount_failed) {
    if (mounted_) return true;

    esp_vfs_littlefs_conf_t config = {};
    config.base_path = kMountPath;
    config.partition_label = kPartitionLabel;
    config.format_if_mount_failed = format_if_mount_failed;
    config.dont_mount = false;

    const esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err != ESP_OK) {
        LOG_BLE("[FS] LittleFS mount failed: %s\n", esp_err_to_name(err));
        return false;
    }

    mounted_ = true;
    return true;
}

void Filesystem::end() {
    if (!mounted_) return;
    esp_vfs_littlefs_unregister(kPartitionLabel);
    mounted_ = false;
}

bool Filesystem::is_mounted() const {
    return mounted_ && esp_littlefs_mounted(kPartitionLabel);
}

std::string Filesystem::to_vfs_path(const char* path) const {
    return join_mount(kMountPath, path ? path : "");
}

FsFile Filesystem::open(const char* path, const char* mode) {
    if (!mounted_ || !path) return FsFile();
    return FsFile::open_entry(path, mode);
}

bool Filesystem::exists(const char* path) {
    if (!mounted_ || !path) return false;
    struct stat info = {};
    return stat(to_vfs_path(path).c_str(), &info) == 0;
}

bool Filesystem::remove(const char* path) {
    if (!mounted_ || !path) return false;
    return unlink(to_vfs_path(path).c_str()) == 0;
}

bool Filesystem::rename(const char* from, const char* to) {
    if (!mounted_ || !from || !to) return false;
    return ::rename(to_vfs_path(from).c_str(), to_vfs_path(to).c_str()) == 0;
}

bool Filesystem::mkdir(const char* path) {
    if (!mounted_ || !path) return false;
    return ::mkdir(to_vfs_path(path).c_str(), 0777) == 0;
}
