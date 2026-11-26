# StockGraber Alerts v1.2.5 — ESP32-S3 N16R8

Sorts the price alert list the same way as the watchlist.

## Fixes

- **`alerts` in order.** The reply listed alerts in the order they were set.
  It now groups them by market under a heading, as `status` does, then A-Z by
  code, then lowest target first for a code with several alerts:

  ```
  -- US --
  GOOG below 260.00
  NVDA below 100.00
  NVDA above 200.00
  -- Crypto --
  BTC-USD below 60,000.00
  ```

  The price alert box on the settings page uses the same order, without the
  headings.

## Download

| File | For |
| --- | --- |
| `stockgraber-v1.2.5-esp32s3-n16r8-app.bin` | **Upgrading** a StockGraber node — app only, write at `0x10000`; keeps settings |
| `stockgraber-v1.2.5-esp32s3-n16r8.bin` | **New board** — ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM), merged image, write at `0x0` |

SHA-256:
- app: `5f60a4b70fb128086dce857570ea11624e8bfd3af3db644945e543f66e5387d8`
- merged: `9613208d277330808af7bfcef8e0d215560bb4cd4267f8a592528ae3800e5529`

Full instructions: `flash.md`.

## Upgrading from v1.0.0 – v1.2.4

Write the **app file only**. Settings, watchlist and price alerts are kept:

```bash
esptool --port <PORT> --baud 460800 write-flash 0x10000 stockgraber-v1.2.5-esp32s3-n16r8-app.bin
```

Do **not** write the merged image to upgrade. It covers the whole 16 MB,
including the blank settings area, so it wipes Wi-Fi, ntfy settings and the
watchlist.

## New board

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 stockgraber-v1.2.5-esp32s3-n16r8.bin
```

Then join the `StockGraber-Setup-XXXX` Wi-Fi to set it up.

## Build

ESP32 Arduino core 3.3.10, ArduinoJson 7.4.3,
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc` —
1,169,255 bytes (37% of the 3 MB app partition), 47,704 bytes static RAM.

## Tested on the board

The same source, built with serial on UART, ran on an N16R8 with three price
alerts. `alerts` came back as:

```
3 of 50:
-- US --
ANT below 70.00
GOOG below 260.00
NVDA below 100.00
```

## Known limitations

- ^HSI and 000001.SS sometimes have no bar at all for a day on Yahoo; their
  change is then measured against the last day that has one.
- A failed ntfy poll just waits for the next one (12 s by default), so a
  command can be delayed on a poor network.
- Alerts can lag: up to 5 minutes from the check interval, plus Yahoo's own
  delay (~15 minutes for Hong Kong).
- Settings page is plain HTTP; ntfy and Yahoo TLS certificates are not verified.
