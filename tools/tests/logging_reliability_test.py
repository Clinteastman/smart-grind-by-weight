"""Exercise production history code with allocation and filesystem faults."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def without_includes(text):
    return "\n".join(line for line in text.splitlines()
                     if not line.startswith(("#include", "#pragma once")))


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class LoggingReliabilityTest(unittest.TestCase):
    def test_allocation_storage_and_export(self):
        source = (ROOT / "src/logging/grind_logging.cpp").read_text()
        methods = "\n".join(function(source, signature) for signature in (
            "GrindTerminationReason classify_termination_reason(",
            "bool GrindLogger::init(", "void GrindLogger::cleanup(",
            "void GrindLogger::start_grind_session(", "void GrindLogger::end_grind_session(",
            "void GrindLogger::clear_buffers(",
            "bool GrindLogger::write_individual_session_file(",
            "bool GrindLogger::validate_session_file(",
            "bool GrindLogger::remove_session_file(",
            "void GrindLogger::cleanup_old_session_files(",
            "void GrindLogger::mark_session_storage_dirty(",
        ))
        header = without_includes((ROOT / "src/logging/grind_logging.h").read_text())
        data_header = without_includes((ROOT / "src/bluetooth/data_stream.h").read_text())
        data_source = without_includes((ROOT / "src/bluetooth/data_stream.cpp").read_text())
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "src/controllers/grind_session.h"
#include "src/logging/session_file.h"
#define SYS_TASK_GRIND_CONTROL_INTERVAL_MS 20
#define SYS_LOG_EVERY_N_GRIND_LOOPS 1
#define GRIND_TIMEOUT_SEC 60
#define GRIND_MAX_PULSE_ATTEMPTS 10
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ENABLE_GRIND_DEBUG 0
std::string messages;
void log_message(const char* format, ...) {
    char text[512]; va_list args; va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args); va_end(args);
    messages += text;
}
#define LOG_BLE(...) log_message(__VA_ARGS__)
int allocation_number = 0, fail_allocation = 0;
std::set<void*> allocations;
void* heap_caps_malloc(size_t size, int) {
    if (++allocation_number == fail_allocation) return nullptr;
    void* p = std::malloc(size); assert(p); allocations.insert(p); return p;
}
void heap_caps_free(void* p) {
    assert(allocations.erase(p) == 1); std::free(p);
}
struct Preferences {
    unsigned writes = 0;
    uint32_t getUInt(const char*, int fallback) { return fallback; }
    size_t putUInt(const char*, uint32_t) { ++writes; return 4; }
    bool begin(const char*, bool) { return true; }
    bool getBool(const char*, bool fallback) { return fallback; }
    void end() {}
};
unsigned long millis() { return 1000; }
struct Statistics {
    void update_grind_session(float, float, uint8_t, bool, uint32_t) {}
} statistics_manager;
struct Node {
    std::string name;
    bool directory = false;
    std::vector<uint8_t> bytes;
    std::vector<std::shared_ptr<Node>> entries;
};
size_t write_limit = SIZE_MAX;
class File {
    std::shared_ptr<Node> node;
    size_t pos = 0, entry = 0;
public:
    File() = default;
    explicit File(std::shared_ptr<Node> n) : node(std::move(n)) {}
    explicit operator bool() const { return bool(node); }
    bool isDirectory() const { return node && node->directory; }
    const char* name() const { return node->name.c_str(); }
    File openNextFile() {
        if (!node || entry >= node->entries.size()) return File();
        return File(node->entries[entry++]);
    }
    size_t size() const { return node ? node->bytes.size() : 0; }
    size_t read(uint8_t* out, size_t count) {
        if (!node) return 0;
        count = std::min(count, node->bytes.size() - pos);
        if (count) std::memcpy(out, node->bytes.data() + pos, count);
        pos += count; return count;
    }
    size_t write(uint8_t* data, size_t count) {
        count = std::min(count, write_limit);
        if (write_limit != SIZE_MAX) write_limit -= count;
        node->bytes.insert(node->bytes.end(), data, data + count); return count;
    }
    void close() { node.reset(); }
};
struct FakeFS {
    std::map<std::string, std::shared_ptr<Node>> files;
    std::vector<std::vector<std::shared_ptr<Node>>> directory_reads;
    size_t directory_read = 0;
    bool fail_open = false, fail_remove = false;
    std::vector<std::string> removed;
    bool exists(const char* path) { return std::string(path) == "/sessions" || files.count(path); }
    File open(const char* path, const char* mode = "r") {
        if (fail_open) return File();
        if (std::string(path) == "/sessions") {
            auto dir = std::make_shared<Node>(); dir->name = path; dir->directory = true;
            if (!directory_reads.empty()) {
                dir->entries = directory_reads[std::min(directory_read++, directory_reads.size()-1)];
            } else {
                for (auto& pair : files) dir->entries.push_back(pair.second);
            }
            return File(dir);
        }
        if (std::strcmp(mode, "w") == 0) {
            auto node = std::make_shared<Node>(); node->name = path; files[path] = node;
            return File(node);
        }
        auto it = files.find(path); return it == files.end() ? File() : File(it->second);
    }
    bool remove(const char* path) {
        if (fail_remove) return false;
        removed.push_back(path); return files.erase(path) != 0;
    }
} LittleFS;
#define private public
''' + header + "\n" + data_header + r'''
#undef private
GrindLogger grind_logger;
uint32_t reported_sessions = 0;
uint32_t GrindLogger::count_sessions_in_flash() const { return reported_sessions; }
uint32_t GrindLogger::get_total_flash_sessions() const { return reported_sessions; }
void GrindLogger::initialize_session_config() {}
bool save_success = false;
bool GrindLogger::flush_session_to_flash() { return save_success; }
''' + methods + "\n" + data_source + r'''
void write_session(uint32_t id) {
    GrindSession session; session.session_id = id; session.session_timestamp = 1234;
    GrindEvent events[2]; GrindMeasurement measurements[3];
    grind_logger.event_count = 2; grind_logger.measurement_count = 3;
    assert(grind_logger.write_individual_session_file(id, session, events, measurements));
}
std::shared_ptr<Node> file(uint32_t id) {
    return LittleFS.files.at("/sessions/session_" + std::to_string(id) + ".bin");
}
int main() {
    Preferences preferences;
    for (int fault = 1; fault <= 3; ++fault) {
        allocation_number = 0; fail_allocation = fault;
        assert(!grind_logger.init(&preferences));
        assert(!grind_logger.current_session && !grind_logger.event_buffer && !grind_logger.measurement_buffer);
        assert(allocations.empty());
        grind_logger.start_grind_session(GrindSessionDescriptor{}, 0);
        assert(!grind_logger.logging_active);
        grind_logger.cleanup(); grind_logger.cleanup();
    }
    fail_allocation = 0;
    assert(!grind_logger.init(nullptr));
    assert(grind_logger.init(&preferences));
    assert(grind_logger.init(&preferences)); // safely replace an existing allocation
    assert(allocations.size() == 3);
    for (bool saved : {false, true}) {
        messages.clear(); save_success = saved;
        grind_logger.start_grind_session(GrindSessionDescriptor{}, 0);
        grind_logger.end_grind_session("COMPLETE", 18, 0);
        assert((messages.find("(saved)") != std::string::npos) == saved);
        assert((messages.find("not saved - storage failure") != std::string::npos) == !saved);
    }

    write_session(1);
    assert(grind_logger.validate_stored_session(1));
    const auto original = file(1)->bytes;
    TimeSeriesSessionHeader header;
    std::memcpy(&header, original.data(), sizeof(header));
    assert(header.checksum == 0 && header.schema_version == 2);
    assert(original.size() == 24 + 80 + 2*44 + 3*24);
    // Reject header-only, truncated payload, trailing data and mismatched metadata.
    for (size_t length : {size_t(0), size_t(23), size_t(24), original.size()-1}) {
        file(1)->bytes.assign(original.begin(), original.begin()+length);
        assert(!grind_logger.validate_stored_session(1));
    }
    file(1)->bytes = original; file(1)->bytes.push_back(0);
    assert(!grind_logger.validate_stored_session(1));
    for (size_t offset : {size_t(0), size_t(8), size_t(16), size_t(18), size_t(20), size_t(24), size_t(28)}) {
        file(1)->bytes = original; file(1)->bytes[offset] ^= 1;
        assert(!grind_logger.validate_stored_session(1));
    }
    file(1)->bytes = original;
    for (size_t limit : {size_t(0), size_t(24), size_t(104), size_t(192), original.size()-1}) {
        write_limit = limit;
        GrindSession session; session.session_id = 2;
        GrindEvent events[2]; GrindMeasurement measurements[3];
        assert(!grind_logger.write_individual_session_file(2, session, events, measurements));
        assert(!LittleFS.files.count("/sessions/session_2.bin"));
    }
    write_limit = SIZE_MAX;

    uint32_t id = 0;
    for (const char* name : {"session_1.bin", "/sessions/session_4294967295.bin"})
        assert(parse_session_filename(name, id));
    for (const char* name : {"session_0.bin", "session_01.bin", "session_-1.bin", "session_abc.bin",
                            "session_4294967296.bin", "session_1.bin.bak", "other_1.bin", "session_1", ""})
        assert(!parse_session_filename(name, id));

    // Malformed and invalid-only directory lists must terminate, not underflow.
    DataStreamManager stream;
    uint32_t ids[12]{};
    reported_sessions = 10;
    auto malformed = std::make_shared<Node>(); malformed->name = "session_abc.bin";
    LittleFS.directory_reads = {{malformed}};
    assert(stream.get_session_list(ids, 12) == 0);
    LittleFS.directory_reads.clear();
    file(1)->bytes.resize(24);
    assert(stream.get_session_list(ids, 12) == 0);
    assert(!stream.initialize_file_stream(1));
    file(1)->bytes = original;
    assert(stream.get_session_list(ids, 0) == 0);
    assert(stream.get_session_list(nullptr, 12) == 0);
    assert(stream.get_session_list(ids, 12) == 1 && ids[0] == 1);
    assert(stream.initialize_file_stream(1));
    std::vector<uint8_t> exported;
    uint8_t chunk[17]; size_t actual = 999;
    while (stream.read_file_chunk(chunk, sizeof(chunk), &actual))
        exported.insert(exported.end(), chunk, chunk+actual);
    assert(exported == original && actual == 0);

    // Retention ignores unrelated entries and sorts only initialized IDs.
    for (uint32_t i = 2; i <= 12; ++i) write_session(i);
    std::vector<std::shared_ptr<Node>> entries;
    for (auto& pair : LittleFS.files) entries.push_back(pair.second);
    entries.push_back(malformed);
    auto directory = std::make_shared<Node>(); directory->name = "session_99.bin"; directory->directory = true;
    entries.push_back(directory);
    LittleFS.directory_reads = {entries, {file(12)}}; LittleFS.directory_read = 0;
    LittleFS.removed.clear();
    grind_logger.cleanup_old_session_files();
    assert(LittleFS.removed.empty()); // directory shrank between count and collect
    LittleFS.directory_reads = {entries}; LittleFS.directory_read = 0;
    auto version = grind_logger.session_storage_version;
    LittleFS.fail_remove = true;
    grind_logger.cleanup_old_session_files();
    assert(LittleFS.removed.empty() && grind_logger.session_storage_version == version);
    LittleFS.fail_remove = false;
    grind_logger.cleanup_old_session_files();
    assert(LittleFS.removed.size() == 2);
    assert(LittleFS.removed[0] == "/sessions/session_1.bin");
    assert(LittleFS.removed[1] == "/sessions/session_2.bin");
    assert(grind_logger.session_storage_version == version + 1);
    grind_logger.cleanup(); assert(allocations.empty());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "logging.cpp"
            binary = Path(directory) / "logging"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Wno-format",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
