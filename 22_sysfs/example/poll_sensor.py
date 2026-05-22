#!/usr/bin/env python3
"""
poll_sensor.py - demonstrates sysfs_notify from user space.

Blocks on poll() waiting for the kernel driver to call sysfs_notify()
on the 'value' attribute. When notified, reads and prints the new value.

Usage:
    python3 poll_sensor.py
"""

import select
import time

SYSFS_VALUE = "/sys/class/mysensor/mysensor0/value"

def main():
    print(f"Opening {SYSFS_VALUE}")
    print("Waiting for sysfs_notify() from the driver (Ctrl+C to stop)...\n")

    with open(SYSFS_VALUE, "r") as f:
        # Initial read
        f.seek(0)
        print(f"Initial value: {f.read().strip()}")

        poll = select.poll()
        poll.register(f, select.POLLPRI | select.POLLERR)

        while True:
            # Block until sysfs_notify() wakes us up
            events = poll.poll(10000)   # 10s timeout

            if not events:
                print("  (no notification in 10s : still waiting)")
                continue

            # Re-read the updated value
            f.seek(0)
            value = f.read().strip()
            print(f"[{time.strftime('%H:%M:%S')}] sysfs_notify received value: {value}")

if __name__ == "__main__":
    main()