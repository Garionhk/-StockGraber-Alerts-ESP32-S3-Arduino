# StockGraber Alerts v1.2.3 — ESP32-S3 N16R8

Shows today's price instead of yesterday's, and adds the change in value to
every symbol line.

## Fixes

- **Current price, not yesterday's close.** Yahoo often leaves the current
  session's daily bar empty, even after the market closes. The node dropped
  that bar and reported the day before as the latest: NVDA showed 155.38 while
  it traded at 149.30. The newest bar is now filled in from Yahoo's live quote,
  so `status`, single-symbol replies and reports show the current price and
  today's change. Hong Kong's pre-open auction prices, which arrive a few
  minutes before the session's bar, count toward that session.

## New

- **Change in value.** Each symbol line shows the change since the previous
  close next to the percentage:

  ```
  NVDA       149.30 (-6.08)  ▼ -3.91%
  CAD=X      1.42 (-0.0054)  ▼ -0.38%   ⚑ near 52w high
  ```

  Changes under 0.01 get four decimals, so currencies don't show `(+0.00)`.
  Lines are longer, so a long `status` splits into parts sooner: about 39
  symbols per part at worst, about 75 typically.

## Download

| File | For |
| --- | --- |
| `stockgraber-v1.2.3-esp32s3-n16r8-app.bin` | **Upgrading** a StockGraber node — app only, write at `0x10000`; keeps settings |
| `stockgraber-v1.2.3-esp32s3-n16r8.bin` | **New board** — ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM), merged image, write at `0x0` |

SHA-256:
- app: `3581c337f20f7865c7a4f59e35f98b9a13abec38f6506580bf4c4b88215ba85a`
- merged: `7d68f03a6bd56b1b6a19c0aaf2df46d3514d0a4b8de07970255cb65fa870b32d`

Full instructions: `flash.md`.

## Upgrading from v1.0.0 – v1.2.2

Write the **app file only**. Settings, watchlist and price alerts are kept:

```bash
esptool --port <PORT> --baud 460800 write-flash 0x10000 stockgraber-v1.2.3-esp32s3-n16r8-app.bin
```

Do **not** write the merged image to upgrade. It covers the whole 16 MB,
including the blank settings area, so it wipes Wi-Fi, ntfy settings and the
watchlist.

## New board

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 stockgraber-v1.2.3-esp32s3-n16r8.bin
```

Then join the `StockGraber-Setup-XXXX` Wi-Fi to set it up.

## Build

ESP32 Arduino core 3.3.10, ArduinoJson 7.4.3,
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc` —
1,161,015 bytes (36% of the 3 MB app partition), 47,704 bytes static RAM.

## Tested on the board

The same source, built with serial on UART, ran a `status` of 28 symbols on an
N16R8. All 28 came back on the first pass. Checked against Yahoo a few minutes
later, 25 matched; the other 3 (0005.HK, ^HSI, CADCNY=X) were still trading
and had moved, with the same previous close. NVDA showed 149.30 (-6.08).
No resets in 6 minutes; free heap 247 KB.

## Known limitations

- ^HSI and 000001.SS sometimes have no bar at all for a day on Yahoo; their
  change is then measured against the last day that has one.
- A failed ntfy poll just waits for the next one (12 s by default), so a
  command can be delayed on a poor network.
- Alerts can lag: up to 5 minutes from the check interval, plus Yahoo's own
  delay (~15 minutes for Hong Kong).
- Settings page is plain HTTP; ntfy and Yahoo TLS certificates are not verified.
