# StockGraber Alerts v1.2.4 — ESP32-S3 N16R8

Sorts the watchlist by market and code, and gives currencies their own group.

## New

- **Sorted by market, then A-Z.** `status` and `list` group symbols by market
  in this order: US, Canada, Hong Kong, China, Currencies, Commodities, Crypto,
  Other. Within each market, codes are A-Z, ignoring case and an index's
  leading `^` (so `^DJI` sits among the D's). `status` puts a heading before
  each group:

  ```
  -- Hong Kong --
  0005.HK    129.00 (+0.40)  ▲ +0.31%
  ^HSI       22,449.15 (+483.14)  ▲ +2.20%
  ```

  `list` gives one line per market:

  ```
  Hong Kong: 0005.HK, ^HSI
  Currencies: CAD=X, CADCNY=X, CNY=X, JPY=X
  ```

  Daily reports, one per market as before, are now A-Z too. The stored
  watchlist keeps the order you entered it in.

- **Currencies market.** Codes ending in `=X` (CAD=X, JPY=X, EURUSD=X, ...)
  were counted as US. They are now their own market:
  - a separate **"Currencies close"** report about 20 minutes after the US
    close. The first one may arrive right after upgrading, if that day's close
    has already passed.
  - price alerts on currencies are checked **all weekday**, like futures,
    instead of only during US market hours.

## Download

| File | For |
| --- | --- |
| `stockgraber-v1.2.4-esp32s3-n16r8-app.bin` | **Upgrading** a StockGraber node — app only, write at `0x10000`; keeps settings |
| `stockgraber-v1.2.4-esp32s3-n16r8.bin` | **New board** — ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM), merged image, write at `0x0` |

SHA-256:
- app: `f1a702a8396ded6dd1a644b9a048a922ceb922a9d00924130a540b8e01141374`
- merged: `fa6d06d424eca01ebd68424c5df6b42ebafc77f681855cc0f2c0971a99bd89ef`

Full instructions: `flash.md`.

## Upgrading from v1.0.0 – v1.2.3

Write the **app file only**. Settings, watchlist and price alerts are kept:

```bash
esptool --port <PORT> --baud 460800 write-flash 0x10000 stockgraber-v1.2.4-esp32s3-n16r8-app.bin
```

Do **not** write the merged image to upgrade. It covers the whole 16 MB,
including the blank settings area, so it wipes Wi-Fi, ntfy settings and the
watchlist.

## New board

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 stockgraber-v1.2.4-esp32s3-n16r8.bin
```

Then join the `StockGraber-Setup-XXXX` Wi-Fi to set it up.

## Build

ESP32 Arduino core 3.3.10, ArduinoJson 7.4.3,
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc` —
1,165,259 bytes (37% of the 3 MB app partition), 47,704 bytes static RAM.

## Tested on the board

The same source, built with serial on UART, ran on an N16R8 with a 28-symbol
watchlist across six markets. `list` and `status` came back grouped and sorted
as above, all 28 symbols on the first pass, and the first Currencies report
went out on its own after the US close.

## Known limitations

- ^HSI and 000001.SS sometimes have no bar at all for a day on Yahoo; their
  change is then measured against the last day that has one.
- A failed ntfy poll just waits for the next one (12 s by default), so a
  command can be delayed on a poor network.
- Alerts can lag: up to 5 minutes from the check interval, plus Yahoo's own
  delay (~15 minutes for Hong Kong).
- Settings page is plain HTTP; ntfy and Yahoo TLS certificates are not verified.
