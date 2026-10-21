"""The Keyestudio KS0428 tank over Bluetooth LE, from the laptop.

The tank's HM-10 module is a "serial port over Bluetooth": what you write to
its FFE1 characteristic comes out of the module's TX and reaches the Arduino
through Serial. This class finds it, connects, reconnects if the link drops and
sends it the speed of each track 12 times per second:

    t = Tank()
    t.move(200, 200)      # forward
    t.move(-150, 150)     # spin left on the spot
    t.move(0, 0)          # stop

If the laptop stops sending (program closed, Bluetooth down), the tank's
firmware stops by itself after 400 ms.
"""
import asyncio
import threading
import time

from bleak import BleakClient, BleakScanner

SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb"     # "serial" of the HM-10 and its clones
CHARACTERISTIC = "0000ffe1-0000-1000-8000-00805f9b34fb"
NAMES = ("HMSoft", "BT05", "BT-05", "HM-10", "HM10", "MLT-BT05", "JDY", "Keyes")   # usual module names
PERIOD = 0.08             # 12.5 commands per second: plenty for the 400 ms watchdog
SCAN_S = 6.0


class Tank:
    def __init__(self, name=None, log=print):
        """name: the module's Bluetooth name or MAC, if there are several nearby."""
        self.name = name
        self.log = log
        self.left = self.right = 0
        self.connected = False
        self.device = None   # "name (MAC)" of the connected tank
        threading.Thread(target=lambda: asyncio.run(self._main()), daemon=True, name="tank").start()

    def move(self, left, right):
        """Speed of each track, from -255 (back) to 255 (forward)."""
        self.left = int(max(-255, min(255, left)))
        self.right = int(max(-255, min(255, right)))

    def stop(self):
        self.move(0, 0)

    # ------------------------------------------------------------ Bluetooth
    def _is_the_tank(self, dev, adv):
        name = adv.local_name or dev.name or ""
        if self.name:
            return self.name.lower() in (name.lower(), dev.address.lower())
        return SERVICE in [u.lower() for u in adv.service_uuids] or any(name.startswith(n) for n in NAMES)

    async def _scan(self):
        found = await BleakScanner.discover(timeout=SCAN_S, return_adv=True)
        for dev, adv in found.values():
            if self._is_the_tank(dev, adv):
                return dev, adv.local_name or dev.name or "?"
        return None, None

    async def _main(self):
        warned = 0.0
        while True:
            try:
                dev, name = await self._scan()
                if dev is None:
                    if time.monotonic() - warned > 20:
                        warned = time.monotonic()
                        self.log("Tank: can't find it over Bluetooth. Is it switched on? Is the HM-10 module plugged in?")
                    continue
                async with BleakClient(dev) as client:
                    self.connected, self.device = True, f"{name} ({dev.address})"
                    self.log(f"Tank connected: {self.device}")
                    while client.is_connected:
                        command = f"M{self.left},{self.right}\n".encode()
                        await client.write_gatt_char(CHARACTERISTIC, command, response=False)
                        await asyncio.sleep(PERIOD)
            except Exception as e:   # noqa: BLE001 - any Bluetooth failure: retry
                if self.connected:
                    self.log(f"Tank: connection lost ({e.__class__.__name__}). Reconnecting...")
                await asyncio.sleep(2)
            finally:
                if self.connected:
                    self.log("Tank disconnected")
                self.connected = False
