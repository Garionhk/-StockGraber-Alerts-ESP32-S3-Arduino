# StockGraber Alerts v1.2.2 — ESP32-S3 N16R8

Fixes repeated "Reset: task watchdog" reboots, and refetches symbols that
still come back empty.

## Fixes

- **No more watchdog reboots from a stalled connection.** The ESP32 core lets
  a TLS handshake run for 120 s, exactly as long as the task watchdog. When
  Yahoo or ntfy accepted a connection and then stalled, the node rebooted
  ("Reset: task watchdog"), every 5-15 minutes during US market hours on one
  board. Every HTTPS call is now capped: 10 s to connect, 15 s for the
  handshake, 15 s per read. The watchdog is also fed before each Yahoo retry.
- **Second pass for missing symbols.** `status` and the end-of-day reports
  fetch the whole watchlist, wait 15 s, then fetch each failed symbol once
  more. Only symbols that fail both passes show "— no data". Parts now go out
  together at the end, in watchlist order.
- **Faster recheck for price alerts.** A quote that fails is tried again after
  one minute instead of waiting for the next 5-minute check. A recheck that
  fails again waits for the next regular check.
- **Wi-Fi power saving off.** The radio no longer dozes between beacons, which
  showed up as dropped connections and failed ntfy polls.
- **Connect limit 10 s** (was 5 s), so a connection that needs one resent
  request still gets through. Yahoo was seen taking 7 s to accept.

## Diagnostics

- The startup and back-online notices show **Wi-Fi signal** in dBm. Below
  about -70 dBm, expect dropped connections; move the board closer to the
  router.
- Over serial, a `health:` line every 5 minutes shows signal, ntfy polls
  succeeded/failed and free heap.

## Download

| File | For |
| --- | --- |
| `stockgraber-v1.2.2-esp32s3-n16r8-app.bin` | **Upgrading** a StockGraber node — app only, write at `0x10000`; keeps settings |
| `stockgraber-v1.2.2-esp32s3-n16r8.bin` | **New board** — ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM), merged image, write at `0x0` |

SHA-256:
- app: `074df2b065535d6f29fbb4a314078677723620cadd5ab8a43b009b2c71e26ac5`
- merged: `bc33212e2680ab949279a07c2abcc8df21a0ee6da95630919bb47cb88898fa2b`

Full instructions: `flash.md`.

## Upgrading from v1.0.0 – v1.2.1

Write the **app file only**. Settings, watchlist and price alerts are kept:

```bash
esptool --port <PORT> --baud 460800 write-flash 0x10000 stockgraber-v1.2.2-esp32s3-n16r8-app.bin
```

Do **not** write the merged image to upgrade, even without erasing. It covers
the whole 16 MB, including the blank settings area, so it wipes Wi-Fi, ntfy
settings and the watchlist. Earlier release notes said otherwise; that was
wrong.

## New board

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 stockgraber-v1.2.2-esp32s3-n16r8.bin
```

Then join the `StockGraber-Setup-XXXX` Wi-Fi to set it up.

## Build

ESP32 Arduino core 3.3.10, ArduinoJson 7.4.3,
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc` —
1,159,215 bytes (36% of the 3 MB app partition), 47,704 bytes static RAM.

## Tested on the board

The same source, built with serial on UART, ran on an N16R8 for an hour on a
network where 1 in 25 ntfy polls also timed out from a desktop. Signal was
-65 to -68 dBm. No resets; 215 ntfy polls succeeded and 56 failed, including a
5-minute stretch where half failed, with every failure giving up within the
new limits. Free heap held at 242-248 KB. A `status` of the full watchlist
recovered all 10 dropped Yahoo connections on the first retry.

Not yet confirmed under a heavy price-alert load during US market hours, where
v1.2.1 was rebooting.

## Known limitations

- A failed ntfy poll just waits for the next one (12 s by default), so a
  command can be delayed on a poor network.
- `status` and reports take longer when Yahoo is dropping connections, and
  commands sent meanwhile wait until they finish.
- Alerts can lag: up to 5 minutes from the check interval, plus Yahoo's own
  delay (~15 minutes for Hong Kong).
- Settings page is plain HTTP; ntfy and Yahoo TLS certificates are not verified.
