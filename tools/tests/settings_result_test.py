"""Production settings result methods and APPLY_SETTINGS dispatch branch."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from settings_persistence_test import method

ROOT = Path(__file__).resolve().parents[2]


class SettingsResultTest(unittest.TestCase):
    def test_application_completion(self):
        source = (ROOT / "src/network/device_api.cpp").read_text()
        header = (ROOT / "src/network/device_api.h").read_text()
        fields = header[header.index("    struct SettingsResult"):header.index("    std::atomic<uint32_t> client_ids_")]
        branch = method(source, "case CommandAction::APPLY_SETTINGS:")
        methods = "\n".join(method(source, signature) for signature in [
            "uint32_t DeviceApi::reserve_settings_result(",
            "void DeviceApi::set_settings_result(",
            "const char* DeviceApi::settings_result(",
            "void DeviceApi::complete_settings_application(",
        ])
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include "src/system/operation_interlock.h"
constexpr int portMAX_DELAY = 0;
void xSemaphoreTake(std::mutex* mutex, int) { mutex->lock(); }
void xSemaphoreGive(std::mutex* mutex) { mutex->unlock(); }
enum class CommandAction { APPLY_SETTINGS };
struct Command { uint32_t request_id; int settings = 0; };
struct DeviceApi {
    std::mutex mutex;
    std::mutex* settings_mutex_ = &mutex;
''' + fields + r'''
    int applied = 0, refreshed = 0;
    bool persist_success = true;
    bool apply_settings(int) { ++applied; return persist_success; }
    void refresh_settings_cache() { ++refreshed; }
    uint32_t reserve_settings_result();
    void set_settings_result(uint32_t, const char*);
    const char* settings_result(uint32_t);
    void complete_settings_application();
    bool process_one(const Command& command) {
        switch (CommandAction::APPLY_SETTINGS) {
''' + branch + r'''
        }
        return false;
    }
};
''' + methods + r'''
int main() {
    DeviceApi api;
    auto id = api.reserve_settings_result();
    assert(id && !api.reserve_settings_result());
    assert(strcmp(api.settings_result(id), "pending") == 0);
    auto competing = operation_interlock().try_acquire(); assert(competing);
    assert(!api.process_one({id}));
    assert(api.applied == 0 && api.refreshed == 0);
    assert(strcmp(api.settings_result(id), "busy") == 0);
    assert(operation_interlock().release(competing));
    id = api.reserve_settings_result();
    assert(api.process_one({id}));
    assert(api.applied == 1 && api.refreshed == 1);
    assert(strcmp(api.settings_result(id), "pending") == 0);
    assert(!operation_interlock().try_acquire());
    // Runtime/UI refresh is the caller's responsibility. Until it finishes,
    // the result must stay pending and the shared operation gate stay held.
    api.complete_settings_application();
    assert(strcmp(api.settings_result(id), "saved") == 0);
    competing = operation_interlock().try_acquire(); assert(competing);
    api.complete_settings_application(); // Repeated completion cannot free another owner.
    assert(operation_interlock().owns(competing));
    operation_interlock().release(competing);
    api.persist_success = false;
    auto failed = api.reserve_settings_result();
    assert(api.process_one({failed}));
    assert(api.refreshed == 2); // Partial changes are also reloaded.
    assert(strcmp(api.settings_result(failed), "pending") == 0);
    api.complete_settings_application();
    assert(strcmp(api.settings_result(failed), "failed") == 0);
    assert(strcmp(api.settings_result(id), "saved") == 0);
    assert(strcmp(api.settings_result(0), "unknown") == 0);
    for (int i = 0; i < 4; ++i) {
        auto next = api.reserve_settings_result(); api.set_settings_result(next, "failed");
    }
    assert(strcmp(api.settings_result(id), "unknown") == 0);
    api.next_settings_id_ = UINT32_MAX;
    auto wrapped = api.reserve_settings_result(); assert(wrapped == 1);
    api.set_settings_result(wrapped, "failed");
    // Two TCP callbacks cannot reserve two pending saves at once.
    uint32_t first = 0, second = 0;
    std::thread a([&] { first = api.reserve_settings_result(); });
    std::thread b([&] { second = api.reserve_settings_result(); });
    a.join(); b.join(); assert((first != 0) != (second != 0));
}
'''
        ui = (ROOT / "src/ui/ui_manager.cpp").read_text()
        update = method(ui, "void UIManager::update(")
        self.assertLess(update.index("apply_runtime_settings()"), update.index("complete_settings_application()"))
        self.assertLess(update.index("update_screensaver_toggles()"), update.index("complete_settings_application()"))
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "result.cpp", Path(tmp) / "result"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread",
                            "-fsanitize=address,undefined", "-I", str(ROOT), str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
