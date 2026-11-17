"""Keep the task watchdog resetting the chip, as it did under the Arduino core."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TaskWatchdogTest(unittest.TestCase):
    def test_a_hung_control_task_resets_the_chip(self):
        # The grind-control task owns the motor and its timeout; if it hangs
        # with the motor on, only a watchdog reset turns the motor off.
        settings = (ROOT / "sdkconfig.defaults").read_text().splitlines()
        self.assertIn("CONFIG_ESP_TASK_WDT_INIT=y", settings)
        self.assertIn("CONFIG_ESP_TASK_WDT_PANIC=y", settings)


if __name__ == "__main__":
    unittest.main()
