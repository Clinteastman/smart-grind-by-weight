"""Keep Bluetooth teardown from freeing objects that are still in use."""
from pathlib import Path
import re
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/bluetooth/manager.cpp").read_text()


class BluetoothLifecycleTest(unittest.TestCase):
    def test_server_callbacks_stay_owned_by_the_manager(self):
        # NimBLEDevice::deinit(true) deletes the server, and the server deletes
        # callbacks it owns. The manager is a static object, so deleting it
        # corrupts the heap the first time Bluetooth is turned off.
        self.assertIn("NimBLEDevice::deinit(true)", SOURCE)
        server_callbacks = re.findall(r"ble_server->setCallbacks\(([^)]*)\)", SOURCE)
        self.assertEqual(server_callbacks, ["this, false"])

    def test_teardown_is_serialised_with_the_bluetooth_task(self):
        # disable() frees every characteristic handle() uses, and the UI and
        # service loop call it from other tasks.
        for signature in ("void BluetoothManager::enable(",
                          "void BluetoothManager::disable()",
                          "void BluetoothManager::handle()"):
            body = function(SOURCE, signature)
            first_statement = body.split("{", 1)[1].strip().splitlines()[0]
            self.assertIn("lock_guard<std::recursive_mutex> lock(lifecycle_mutex)",
                          first_statement, signature)

    def test_logging_stops_before_the_debug_characteristic_is_freed(self):
        disable = function(SOURCE, "void BluetoothManager::disable()")
        self.assertLess(disable.index("debug_stream_active = false"),
                        disable.index("NimBLEDevice::deinit(true)"))


if __name__ == "__main__":
    unittest.main()
