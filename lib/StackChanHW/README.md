# StackChanHW

C++ library (Arduino, ESP32-S3) for the Stack-chan hardware: the CoreS3 power rails, the K151 base (two SCS0009 servos, LEDs, INA226 battery sensor) and the GC0308 camera. Pins, buses and limits are explained in [docs/ROBOT.md](../../docs/ROBOT.md).

## Main API

| File | What it gives you |
|---|---|
| `board.h` | `boardInit()`, `servoInit()`, `servoStartTask()` (25 Hz loop with a speed limit), `servoSetTarget(pan, tilt)` in radians, `servoHome()`, `servoSetTorque()`, `servoGetCurrent()`, `setBaseLeds(r, g, b)`, `readBatteryCurrent()`, and `i2cLock()` / `I2CGuard` for the shared internal I2C bus. |
| `camera.h` | `cameraInit()` and `cameraCaptureJpeg(quality, &jpg, &len)`: QVGA capture in RGB565, encoded in software (the GC0308 has no hardware JPEG). The caller must `free()` the buffer. |
| `stackchan_limits.h` | Head limits (`PAN_MIN/MAX`, `TILT_MIN/MAX`), `LED_MAX_LEVEL` and the servo calibration. Respect them: below `TILT_MIN` the chin hits the base, and LEDs at full power can make the PMIC cut the power. |

## Who uses it

Every robot firmware in `demos/` (01, 02, 03 receiver, 04 to 09), and `lib/StackChanLink`. Add it in `platformio.ini` with `lib_extra_dirs` pointing to `lib/`.
