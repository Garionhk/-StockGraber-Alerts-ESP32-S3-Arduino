# StockGraber Alerts v1.2.0 — ESP32-S3 N16R8

The settings page and commands stay responsive while the node is busy.

## Highlights

- **No more frozen settings page.** ntfy polls, market reports, price-alert
  checks, NTP and the startup notice now run in their own task, so the page
  keeps answering during slow network calls. Before, a slow ntfy poll froze it
  for 10-30 s, and a large end-of-day report for minutes. On the board it
  answered 297 of 297 requests, the slowest in 0.27 s, while three market
  reports went out.
- **Settings page is up sooner after Wi-Fi joins.** NTP and the startup notice
  no longer hold it back.
- **Passphrase kept out of the serial log.** The `replied to:` line now shows
  the command only.

## Download

| File | For |
| --- | --- |
| `stockgraber-v1.2.0-esp32s3-n16r8.bin` | ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) — merged image, write at `0x0` |

SHA-256: `d289f1d965f0ee0ca0e7f3e7cc6c3db8ba72ba571e99d69caae784c5cdabbf70`

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 stockgraber-v1.2.0-esp32s3-n16r8.bin
```

Full instructions: `flash.md`.

## Upgrading from v1.0.0 or v1.1.0

Flash **without erasing**. Settings, watchlist and price alerts are kept.

## Build

ESP32 Arduino core 3.3.10, ArduinoJson 7.4.3,
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc` —
1,155,811 bytes (36% of the 3 MB app partition), 47,672 bytes static RAM.

## Tested on the board

The same source, built with serial on UART, ran on an N16R8: Wi-Fi join, NTP,
startup notice, ntfy polling and three end-of-day reports, with the settings
page probed every 0.5 s throughout. Free heap held at ~249 KB (lowest 173 KB);
the network task used about 5 KB of its 12 KB stack. The v1.1.0 code ran there
for an hour beforehand without a crash, answering `status` commands.

## Known limitations

- A `status` or report still takes ~3 s per symbol, and commands sent meanwhile
  wait until it finishes. The settings page is no longer affected.
- Alerts can lag: up to 5 minutes from the check interval, plus Yahoo's own
  delay (~15 minutes for Hong Kong).
- Settings page is plain HTTP; ntfy and Yahoo TLS certificates are not verified.
