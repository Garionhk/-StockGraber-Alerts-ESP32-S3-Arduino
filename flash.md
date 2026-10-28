# Building and distributing firmware

How to produce a `.bin` other people can flash to their own ESP32-S3 N16R8, and
how they flash it.

Nothing user-specific is compiled in — Wi-Fi credentials, the ntfy server,
topic, login, both passphrases and the watchlist live in NVS on the device,
entered through the setup portal on first boot. That is what makes the binary
shareable.

---

## 1. Prerequisites (you, the builder)

[arduino-cli](https://arduino.github.io/arduino-cli/) plus the ESP32 core
(3.x — 3.3.10 tested) and one library:

```bash
arduino-cli core install esp32:esp32
```

```bash
arduino-cli lib install ArduinoJson
```

ArduinoJson must be **7.x**. `WebServer`, `DNSServer`, `WiFiClientSecure`,
`HTTPClient` and `Preferences` all ship with the core.

## 2. Build

Bump `SG_VERSION` in `StockGraberAlerts/StockGraberAlerts.ino` first, then:

```bash
arduino-cli compile --clean --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc" --export-binaries StockGraberAlerts
```

The artifacts land in `StockGraberAlerts/build/esp32.esp32.esp32s3/`:

| File | Size | Purpose |
| --- | --- | --- |
| `StockGraberAlerts.ino.bin` | ~1.1 MB | the application |
| `StockGraberAlerts.ino.bootloader.bin` | 20 KB | second-stage bootloader |
| `StockGraberAlerts.ino.partitions.bin` | 3 KB | partition table |
| `boot_app0.bin` | 8 KB | OTA selector |
| `StockGraberAlerts.ino.merged.bin` | 16 MB | all of the above, pre-combined |

The application is 36% of the 3 MB app partition. `--export-binaries` is what
writes these into the sketch folder; without it arduino-cli builds into a
temporary directory and throws the result away.

The partition scheme (`app3M_fat9M_16MB`, see README §3) is baked into
`partitions.bin`, so it travels with the firmware as long as whoever flashes it
writes the partition table too — both methods below do. PSRAM mode (OPI) and
USB CDC On Boot are compiled into the application.

The FQBN targets a **16 MB** board with **octal PSRAM** (N16R8). `merged.bin` is
a whole-flash image and assumes 16 MB; rebuild with matching `FlashSize` and
`PSRAM` settings for anything else.

### Package the release

```bash
mkdir -p dist
```

```bash
cp StockGraberAlerts/build/esp32.esp32.esp32s3/StockGraberAlerts.ino.merged.bin dist/stockgraber-v1.2.0-esp32s3-n16r8.bin
```

```bash
cd dist && shasum -a 256 stockgraber-v1.2.0-esp32s3-n16r8.bin > stockgraber-v1.2.0-esp32s3-n16r8.bin.sha256
```

`.gitignore` excludes `*.bin`, so the image is not committed — attach it to a
release instead.

## 3. Confirm the binary carries no secrets

Do this before publishing:

```bash
strings dist/stockgraber-v1.2.0-esp32s3-n16r8.bin | grep -iE "your-ssid|your-wifi-password|your-topic|your-passphrase"
```

Substitute your own network name, password, topic and passphrase. Expect **no
output**. Grep for the actual values, not the word "password" — the settings
form contains `<input type=password …>` markup.

Sanity-check that `strings` actually read the file. This should print `1`:

```bash
strings dist/stockgraber-v1.2.0-esp32s3-n16r8.bin | grep -c "StockGraber Alerts v"
```

---

## 4. Instructions for the person flashing

They need [esptool](https://github.com/espressif/esptool) — no Arduino
toolchain, no source:

```bash
pip install esptool
```

### Find the port

The S3 uses its native USB port (USB-Serial/JTAG) — no CP210x/CH340 driver
needed. On boards with two USB-C sockets, use the one labelled **USB**, not
**UART/COM**.

| OS | Typically |
| --- | --- |
| macOS | `/dev/cu.usbmodem*` |
| Linux | `/dev/ttyACM0` |
| Windows | `COM3`, `COM4`, … |

If nothing appears, try another cable — a charge-only USB cable is the usual
culprit.

### Verify the download

```bash
shasum -a 256 -c stockgraber-v1.2.0-esp32s3-n16r8.bin.sha256
```

### Erase, then write

Erasing first avoids inheriting stale settings from whatever was on the board
before. **Skip the erase when upgrading a StockGraber node** — it would wipe the
settings and watchlist.

```bash
esptool --port /dev/cu.usbmodem14401 erase-flash
```

```bash
esptool --port /dev/cu.usbmodem14401 --baud 460800 write-flash 0x0 stockgraber-v1.2.0-esp32s3-n16r8.bin
```

The merged image already contains the bootloader (`0x0` on the S3), partition
table (`0x8000`), OTA selector (`0xe000`) and application (`0x10000`). Writing
the bare application to `0x0` instead produces a board that does not boot.

Older esptool (v4 and earlier) uses underscores: `erase_flash`, `write_flash`.
If the connect fails, hold **BOOT**, tap **RST** (EN), release **BOOT** and
retry — then press **RST** once after flashing to start the firmware.

### First boot

1. Join the Wi-Fi network **`StockGraber-Setup-XXXX`** (suffix differs per
   board).
2. The setup page should open automatically. If not, browse to
   **http://192.168.4.1**.
3. Fill in the form → **Save & restart** (details in README §4.1). Wi-Fi is
   **2.4 GHz only**; give each node its **own** ntfy topic.

The node joins the network and publishes a startup notice to the topic:

```
StockGraber - node started
Reset: power-on
IP: 192.168.1.42
Settings: http://192.168.1.42
Firmware: v1.2.0
Watchlist: 4 of 200 symbols
Heap: 158 KB free
PSRAM: 8192 KB
```

The settings page stays available at that address, protected by the admin
password (username **`admin`**). Send `<passphrase> help` to the topic for the
command list. Serial runs at **115200 baud** if anything goes wrong.

---

## 5. Notes

- **Reflashing does not erase settings.** NVS (`0x9000`, 20 KB) lives outside
  the application partition, so an upgrade keeps the configuration and the
  watchlist.
- **Returning a node to setup mode** needs no USB: press **RST** (EN), release,
  then press **BOOT** within 2 s; or send `<passphrase> factoryreset` over ntfy
  (keeps the watchlist).
- **To wipe settings and watchlist over USB** without reflashing:

  ```bash
  esptool --port /dev/cu.usbmodem14401 erase-region 0x9000 0x5000
  ```
