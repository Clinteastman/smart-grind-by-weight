"""Run the production filesystem health check against a fake LittleFS mount."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/tasks/file_io_task.cpp").read_text()

HARNESS = r'''
#include <cassert>
#include <cstdio>
#define LOG_BLE(...) std::printf(__VA_ARGS__)
#define pdMS_TO_TICKS(ms) (ms)
inline void vTaskDelay(int) {}

// A file handle that accepts writes, so a write probe would compile and count.
struct FsFile {
    explicit operator bool() const { return true; }
    void println(const char*) {}
    void close() {}
};

// Records every call the health check makes on the filesystem.
struct FakeFilesystem {
    bool mounted = true;
    int mount_failures = 0;   // begin() calls that fail before one succeeds
    int begins = 0, formatting_begins = 0, ends = 0, opens = 0, removes = 0;
    bool is_mounted() const { return mounted; }
    bool begin(bool format_if_mount_failed) {
        ++begins;
        if (format_if_mount_failed) ++formatting_begins;
        if (mount_failures > 0) { --mount_failures; return false; }
        mounted = true;
        return true;
    }
    void end() { ++ends; mounted = false; }
    FsFile open(const char*, const char*) { ++opens; return FsFile(); }
    bool remove(const char*) { ++removes; return true; }
} filesystem;

class FileIOTask {
public:
    void check_filesystem_health();
    bool validate_filesystem_access();
    void handle_filesystem_error();
    bool attempt_filesystem_recovery();
};
''' + "\n".join(function(SOURCE, signature) for signature in (
    "void FileIOTask::check_filesystem_health()",
    "bool FileIOTask::validate_filesystem_access()",
    "void FileIOTask::handle_filesystem_error()",
    "bool FileIOTask::attempt_filesystem_recovery()",
)) + r'''

int main() {
    FileIOTask task;

    // A mounted filesystem is checked without writing to flash or remounting.
    for (int check = 0; check < 3; ++check) task.check_filesystem_health();
    assert(filesystem.mounted);
    assert(filesystem.opens == 0 && filesystem.removes == 0 && filesystem.begins == 0);

    // Lost mount: every check retries a non-formatting mount until it works,
    // and the filesystem is never unmounted underneath other tasks.
    filesystem.mounted = false;
    filesystem.mount_failures = 2;
    task.check_filesystem_health();
    assert(!filesystem.mounted && filesystem.begins == 1);
    task.check_filesystem_health();
    assert(!filesystem.mounted && filesystem.begins == 2);
    task.check_filesystem_health();
    assert(filesystem.mounted && filesystem.begins == 3);
    assert(filesystem.formatting_begins == 0 && filesystem.ends == 0);
    assert(filesystem.opens == 0 && filesystem.removes == 0);
}
'''


class FilesystemHealthTest(unittest.TestCase):
    def test_check_is_read_only_and_recovery_never_formats(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "health.cpp", Path(folder) / "health"
            cpp.write_text(HARNESS)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    unittest.main()
