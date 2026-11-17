"""Keep the stack overflow checks the Arduino core had."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StackGuardTest(unittest.TestCase):
    def test_stack_end_is_watched(self):
        # The canary check alone only runs at a context switch, after the
        # overflow may already have corrupted a neighbouring allocation.
        settings = (ROOT / "sdkconfig.defaults").read_text().splitlines()
        self.assertIn("CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y", settings)

    def test_functions_with_local_arrays_check_their_canary(self):
        settings = (ROOT / "sdkconfig.defaults").read_text().splitlines()
        self.assertIn("CONFIG_COMPILER_STACK_CHECK_MODE_NORM=y", settings)
        self.assertNotIn("CONFIG_COMPILER_STACK_CHECK_MODE_NONE=y", settings)


if __name__ == "__main__":
    unittest.main()
