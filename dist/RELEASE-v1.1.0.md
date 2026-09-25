# StockGraber Alerts v1.1.0 — ESP32-S3 N16R8

Price alerts, and a watchlist four times the size.

## Highlights

- **One-shot price alerts.** `<pass> alert AAPL above 200` pushes once (high
  priority) when the price gets there, then deletes itself. `alerts` lists them,
  `unalert AAPL [price]` cancels. Up to 50, also editable on the settings page.
  - Checked every 5 minutes while the symbol's market is open (crypto 24/7).
  - A brief spike between checks still fires: once a full session has passed
    since the alert was set, the session's high/low is compared, not just the
    last price.
  - Refused at set time if the price is already past the target.
- **Watchlist raised from 50 to 200 symbols.**
- **Long replies are split** across several ntfy messages (`… (1)`, `… (2)`,
  `… (3/3)`) instead of failing at ntfy's 4 KB limit. `status` and the daily
  reports send parts as they fill; only a report part holding a golden/death
  cross goes out at the higher priority.
- **Gentler on Yahoo.** Fetches are spaced 300 ms apart, and HTTP 429 is
  retried once after 2 s.

## Download

| File | For |
| --- | --- |
| `stockgraber-v1.1.0-esp32s3-n16r8.bin` | ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) — merged image, write at `0x0` |

SHA-256: `3098e97d6c4a07050f2b693a4c975604c7fcfb7ec6387abf750f435763494779`

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 stockgraber-v1.1.0-esp32s3-n16r8.bin
```

Full instructions: `flash.md`.

## Upgrading from v1.0.0

Flash **without erasing**. Settings and watchlist are kept; there are simply no
alerts until you set some.

## Build

ESP32 Arduino core 3.3.10, ArduinoJson 7.4.3,
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc` —
1,154,607 bytes (36% of the 3 MB app partition), 47,656 bytes static RAM.

## Known limitations

- Verified by build and host test; not yet run on the board.
- A 200-symbol `status` takes ~10 minutes, and the node answers nothing else
  while it runs (commands queue and are answered afterwards).
- Alerts can lag: up to 5 minutes from the check interval, plus Yahoo's own
  delay (~15 minutes for Hong Kong).
- Settings page is plain HTTP; ntfy and Yahoo TLS certificates are not verified.
