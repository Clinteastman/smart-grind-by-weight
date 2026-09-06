"""Exercise the production TLS body wait without network timing dependencies."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class OtaStreamTest(unittest.TestCase):
    def test_production_image_header_check(self):
        source = (ROOT / "src/network/device_web_server.cpp").read_text()
        check = source.split("    WiFiClient* stream = http.getStreamPtr();", 1)[1]
        check = "WiFiClient* stream = http.getStreamPtr();" + check.split(
            "    if (!web_firmware_update.begin", 1)[0]
        harness = r'''
#include "network/ota_stream.h"
#include <cassert>
#include <cstddef>
#define LOG_BLE(...) ((void)0)
uint32_t elapsed=0;
uint32_t millis() { return elapsed; }
void delay(unsigned ms) { elapsed+=ms; }
struct WiFiClient {
    unsigned arrives=40, disconnects=30000;
    uint8_t magic=0xe9;
    int available() { return elapsed>=arrives ? 1 : 0; }
    bool connected() { return elapsed<disconnects; }
    size_t readBytes(uint8_t* out, size_t) {
        if (!available()) return 0; *out=magic; return 1;
    }
} stream;
struct { bool ended=false; WiFiClient* getStreamPtr() { return &stream; }
    void end() { ended=true; } } http;
bool failed=false, accepted=false;
void finish_ota(bool success) { assert(!success); failed=true; }
void verify() {
''' + check + r'''
    accepted=true;
}
int main() {
    verify(); assert(accepted && !failed && elapsed==40 && !http.ended);
    elapsed=0; accepted=false; stream.magic=0x3c;
    verify(); assert(!accepted && failed && http.ended); // HTML rejected
    elapsed=0; failed=false; http.ended=false; stream.arrives=30000;
    verify(); assert(!accepted && failed && elapsed==15000 && http.ended);
    elapsed=0; failed=false; http.ended=false; stream.disconnects=20;
    verify(); assert(!accepted && failed && elapsed==20 && http.ended);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "header.cpp", Path(tmp) / "header"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-I", str(ROOT / "src"),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_first_byte_wait(self):
        harness = r'''
#include "network/ota_stream.h"
#include <cassert>
#include <limits>
struct Stream {
    uint32_t elapsed = 0, arrives = 0, disconnects = 30000;
    int available() { return elapsed >= arrives ? 1 : 0; }
    bool connected() { return elapsed < disconnects; }
};
void check(uint32_t start, uint32_t arrives, uint32_t disconnects,
           bool expected, uint32_t expected_elapsed) {
    Stream stream{0, arrives, disconnects};
    auto clock = [&] { return start + stream.elapsed; };
    auto pause = [&] { stream.elapsed += 2; };
    assert(ota_wait_for_data(stream, clock, pause, 15000) == expected);
    assert(stream.elapsed == expected_elapsed);
}
int main() {
    check(0, 0, 30000, true, 0);             // already buffered
    check(0, 40, 30000, true, 40);           // headers precede TLS body
    check(0, 14998, 30000, true, 14998);     // slow but within timeout
    check(0, 30000, 30000, false, 15000);    // bounded timeout
    check(0, 30000, 20, false, 20);          // disconnect before body
    check(0, 0, 0, true, 0);                 // drain buffered closed peer
    check(UINT32_MAX - 10, 40, 30000, true, 40); // millis rollover
    check(UINT32_MAX - 10, 30000, 30000, false, 15000);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "stream.cpp", Path(tmp) / "stream"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
