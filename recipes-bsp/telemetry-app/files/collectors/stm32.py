"""
Placeholder collector for the future STM32 power/watchdog companion MCU.
Returns None values today; swap the body for real UART/I2C reads once
the STM32 firmware/protocol is finalized. Kept as its own module so the
integration is a one-file change.
"""


def collect() -> dict:
    return {
        "voltage": None,
        "current": None,
        "pgood": None,
        "watchdog_ok": None,
    }
