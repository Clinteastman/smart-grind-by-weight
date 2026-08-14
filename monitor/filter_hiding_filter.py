"""Hide the expected touchscreen I2C polling warning from the serial monitor."""

from platformio.public import DeviceMonitorFilterBase


class HidingFilter(DeviceMonitorFilterBase):
    NAME = "hiding_filter"

    def rx(self, text):
        if "i2cWriteReadNonStop" in text:
            return ""
        return text
