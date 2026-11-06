# StockGraber Alerts v1.2.1 — ESP32-S3 N16R8

Fixes symbols randomly showing "— no data" in `status` and reports.

## Fixes

- **Fewer "— no data" lines.** Yahoo intermittently drops new connections when
  a `status` or report opens dozens in a row. The connect timed out
  (`HTTP -1`) and the symbol came back empty, at random: 7 of 27 symbols in
  one `status` on the board. Before, only HTTP 429 got a retry. Now a fetch
  gets up to 3 attempts on connection errors and on 429, waiting 2 s, then
  4 s. On the board, 15 dropped connections in one `status` all recovered on
  the first retry, so every symbol came back.
- **Watchdog fed during retry waits**, so a slow `status` can't trip it.

## Download

| File | For |
| --- | --- |
| `stockgraber-v1.2.1-esp32s3-n16r8.bin` | ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) — merged image, write at `0x0` |

SHA-256: `817caec85018e697fd8f6aa6351d98d884640932ae4270cbe96484f3389a87e0`

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 stockgraber-v1.2.1-esp32s3-n16r8.bin
```

Full instructions: `flash.md`.

## Upgrading from v1.0.0 – v1.2.0

Flash **without erasing**. Settings, watchlist and price alerts are kept.

## Build

ESP32 Arduino core 3.3.10, ArduinoJson 7.4.3,
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc` —
1,156,131 bytes (36% of the 3 MB app partition), 47,672 bytes static RAM.

## Tested on the board

The same fix, built with serial on UART, ran on an N16R8: Wi-Fi join, NTP,
startup notice, one `status` of 27 symbols and an `add` command. No crash or
reset over 30 minutes of serial capture.

## Known limitations

- A retried fetch adds 2-6 s, so a `status` can take longer when Yahoo is
  dropping connections. Commands sent meanwhile wait until it finishes.
- A symbol whose connections fail 3 times in a row still shows "— no data".
- Alerts can lag: up to 5 minutes from the check interval, plus Yahoo's own
  delay (~15 minutes for Hong Kong).
- Settings page is plain HTTP; ntfy and Yahoo TLS certificates are not verified.
