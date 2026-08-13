# Rollback

Before beta testing, keep a copy of your previous known-good firmware image.

## App-only rollback

If your previous image was an app image, restore it at `0x10000`:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 write_flash 0x10000 previous-firmware.bin
```

This normally preserves calibration and NVS state.

## Full rollback

If you have a full-device backup, restore it using the offsets and size documented when the backup was made.

Do not use a backup from a different board, flash size, or partition table.

## When rollback is recommended

Rollback if:

- BLE no longer advertises.
- The display does not boot.
- Tare stops working.
- The scale cannot be calibrated.
- The device repeatedly resets.
- Compatibility apps stop reading weight.

If only the web UI looks wrong, update LittleFS first before rolling back the app firmware.
