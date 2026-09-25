# StockGraber Alerts v1.0.0 — ESP32-S3 N16R8

First release of the ESP32-S3 node as a shareable binary.

## Highlights

- **No credentials in the firmware.** Wi-Fi, ntfy server/topic/login, command
  passphrase, admin password, report options and the watchlist are entered on a
  built-in web page and stored in flash (NVS). `sg_secrets.h` and the two-pass
  `FORCE_PROVISION` flash are gone.
- **Setup portal** — first boot opens the `StockGraber-Setup-XXXX` Wi-Fi with a
  captive page at `http://192.168.4.1`.
- **Settings page on the LAN** at the node's IP, login `admin`.
- **Wi-Fi fallback** — if the router is unreachable the setup network comes back
  (login required) while the node keeps retrying.
- **ntfy login** — username/password or access token, for protected servers.
- **`factoryreset`** command over ntfy; **BOOT-button** recovery on the board.
- Startup notice now includes the settings URL and firmware version.

## Download

| File | For |
| --- | --- |
| `stockgraber-v1.0.0-esp32s3-n16r8.bin` | ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) — merged image, write at `0x0` |

SHA-256: `e500c7470c604ab2e24d76890207bc33d0f3da79ec03438d77807066a059df07`

```bash
esptool --port <PORT> --baud 460800 write-flash 0x0 stockgraber-v1.0.0-esp32s3-n16r8.bin
```

Full instructions: `flash.md`.

## Upgrading a node set up with `sg_secrets.h`

Flash without erasing. Existing Wi-Fi, topic, passphrase and watchlist keep
working. The settings page will say *Settings locked* until an admin password is
set: press EN (RST), release, press BOOT within 2 s, and save the form with a
new admin password.

## Build

ESP32 Arduino core 3.3.10, ArduinoJson 7.4.3,
`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc` —
1,134,607 bytes (36% of the 3 MB app partition), 47,632 bytes static RAM.

## Known limitations

- Build-verified; the web portal has not yet been exercised on the S3.
- Settings page is plain HTTP; the admin login crosses the LAN unencrypted.
- ntfy and Yahoo TLS certificates are not verified.
- Needs the **R8** (octal PSRAM) variant. On an N16 without PSRAM the node boots
  but Yahoo fetches fail.
