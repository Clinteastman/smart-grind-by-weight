"""Exercise the shared reservation under competing starts and stale cleanup."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class OperationInterlockTest(unittest.TestCase):
    def test_competing_operations(self):
        harness = r'''
#include "system/operation_interlock.h"
#include <cassert>
#include <thread>
#include <vector>

int main() {
    OperationInterlock gate;
    assert(!gate.release(0));
    auto old = gate.try_acquire();
    assert(old && gate.owns(old));
    assert(!gate.try_acquire());
    assert(gate.release(old));
    auto next = gate.try_acquire();
    assert(next && next != old);
    assert(!gate.release(old) && gate.owns(next));
    assert(!gate.release(0) && gate.owns(next));
    assert(gate.release(next));

    std::atomic<unsigned> inside{0}, successes{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            for (unsigned j = 0; j < 20000; ++j) {
                auto token = gate.try_acquire();
                if (!token) continue;
                assert(inside.fetch_add(1) == 0);
                assert(gate.owns(token));
                std::this_thread::yield();
                assert(inside.fetch_sub(1) == 1);
                assert(gate.release(token));
                ++successes;
            }
        });
    }
    for (auto& thread : threads) thread.join();
    assert(successes > 0 && inside == 0);
    assert(gate.try_acquire());
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "interlock.cpp", Path(tmp) / "interlock"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread",
                            "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
