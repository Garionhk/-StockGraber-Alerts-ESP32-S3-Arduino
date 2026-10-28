# StockGraber Alerts — ESP32-S3 (Arduino)

The C++ / Arduino version of the node: per-market end-of-day reports pushed to
ntfy, one-shot price alerts, plus remote commands (`status`, `list`, `<CODE>`,
`add`, `remove`, `alert`, `help`) answered from your phone. Same behaviour as the Raspberry Pi daemon.

> ⚠️ **Not financial advice.** StockGraber is a charting/learning tool. The
> indicators and signals it draws (golden/death crosses, RSI, MACD, etc.) are
> common technical-analysis conventions, not buy/sell recommendations. No
> indicator reliably predicts price.

> ⚠️ **Give this node its own ntfy topic** (e.g. `<your-topic>-esp`). On the
> Pi's topic you'd get duplicate reports and two bots answering every command.

## Prebuilt firmware

**v1.2.0** is in [`dist/`](dist/): `stockgraber-v1.2.0-esp32s3-n16r8.bin`, a
merged image for a 16 MB ESP32-S3 N16R8 — no toolchain needed:

```
esptool --port /dev/cu.usbmodem14401 --baud 460800 write-flash 0x0 dist/stockgraber-v1.2.0-esp32s3-n16r8.bin
```

Then set it up from your phone (§4.1). Checksum, release notes and the full
flashing guide: [`dist/RELEASE-v1.2.0.md`](dist/RELEASE-v1.2.0.md),
[`flash.md`](flash.md).

---

## 1. Hardware

Built for an **ESP32-S3 N16R8** — 16 MB flash, 8 MB **octal** PSRAM.

| Spec | Value |
|---|---|
| SoC | ESP32-S3 (QFN56), revision v0.2 |
| Cores | Dual-core Xtensa LX7 @ 240 MHz + LP core |
| Flash | 16 MB quad SPI (3.3 V, set by eFuse) |
| PSRAM | 8 MB embedded, octal (OPI) |
| Radio | Wi-Fi 2.4 GHz b/g/n, Bluetooth 5 (LE) |
| Crystal | 40 MHz |
| USB | Native USB-Serial/JTAG (`303a:1001`) — no CP210x/CH340 driver needed |

The PSRAM is not optional. `sg_yahoo.cpp` puts the JSON document in SPIRAM so
the internal heap stays free for TLS buffers; with PSRAM disabled or set to
QSPI, fetches fail.

### Confirming your board

```
esptool --port /dev/cu.usbmodem14401 --no-stub flash-id
```

A correct N16R8 reports:

```
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
USB mode:           USB-Serial/JTAG
Detected flash size: 16MB
```

If `Embedded PSRAM 8MB` is missing you have an N16 (no PSRAM) or an R2/R8 QSPI
variant — the board settings below will need to change.

### Flash layout

Partition scheme `16M Flash (3MB APP/9.9MB FATFS)`:

| Partition | Offset | Size | Holds |
|---|---|---|---|
| `nvs` | `0x9000` | 20 KB | settings from the web portal, watchlist |
| `otadata` | `0xe000` | 8 KB | OTA slot selector |
| `app0` | `0x10000` | 3 MB | the sketch (currently ~36% used) |
| `app1` | `0x310000` | 3 MB | second OTA slot (unused) |
| `ffat` | `0x610000` | 9.9 MB | FAT filesystem (unused) |
| `coredump` | `0xFF0000` | 64 KB | crash dumps |

---

## 2. Software prerequisites

| Component | Version | Notes |
|---|---|---|
| ESP32 Arduino core | **3.3.10** | IDF 5.x. Older 2.x cores use a different watchdog API and will not build. |
| ArduinoJson | **7.x** (7.4.3 tested) | **v6 will not compile.** The sketch uses the v7 `Allocator` interface and bare `JsonDocument`. |
| Arduino IDE *or* arduino-cli | any current | Both paths documented below. |

Installing ArduinoJson v7 replaces v6 globally. If other sketches on this
machine parse JSON with the v6 API, they will need updating.

---

## 3. Board settings

*Tools → Board → esp32 → **ESP32S3 Dev Module***, then:

| Setting | Value | Why |
|---|---|---|
| **PSRAM** | **OPI PSRAM** | The N16R8's 8 MB is *octal*. Wrong setting = no PSRAM = fetches fail. |
| **USB CDC On Boot** | **Enabled** | Otherwise `Serial` routes to the UART pins and the Serial Monitor stays blank on the native USB port. |
| Flash Size | 16MB (128Mb) | matches the N16R8 |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) | TLS + ArduinoJson makes a big binary |
| Upload Speed | 921600 | |
| Erase All Flash | **Disabled** | Enabling it wipes NVS — see §7 |

The equivalent FQBN for arduino-cli:

```
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc
```

---

## 4. Where to change settings

**Nothing user-specific is compiled in.** Wi-Fi, ntfy, passphrases, the
report/command options and the watchlist are all entered on a small web page
the node serves itself, and stored in NVS. A `.bin` built from this sketch
carries no credentials and can be shared or flashed onto any board.

### 4.1 First-boot setup

On a board with no stored settings, the node starts its own Wi-Fi network:

1. Join **`StockGraber-Setup-XXXX`** (the suffix is the last four of its MAC)
   from a phone or laptop. The setup page should pop up on its own; if not, open
   **http://192.168.4.1**.
2. Fill in the form and press **Save & restart**. The node restarts, joins your
   network and announces itself on your ntfy topic.

| Section | Fields |
|---|---|
| Wi-Fi | network (picked from a live scan — 2.4 GHz only) and password |
| ntfy | server URL (`https://ntfy.sh`, or `http://192.168.1.5:8080` for a self-hosted one) and topic |
| ntfy login | username + password, or a token — blank for an open server. A token wins over username/password. |
| Passphrases | command passphrase, and an admin password for this page |
| Reports & commands | daily reports on/off, commands on/off, report delay (15–30 min), 52-week proximity %, poll interval |
| Watchlist | Yahoo codes separated by commas or spaces, up to 200 |
| Price alerts | one per line, `AAPL > 200` or `BTC-USD < 60000`, up to 50 (§9.1) |

- **Topic: must be unique per node.** Two nodes on one topic = duplicate
  reports and two bots answering every command. ntfy.sh topics are readable by
  anyone who knows the name, so make it long and unguessable. Letters, digits,
  `-` and `_` only.
- **Command passphrase** prefixes every command (`<passphrase> status`) and is
  required for `add`/`remove`/`factoryreset`. Without it, anyone who finds your
  topic could edit the watchlist.
- **Admin password** protects the settings page (username **`admin`**). It must
  differ from the command passphrase, because that one travels through ntfy.
- **Watchlist** codes typed here are not checked against Yahoo the way `add`
  is, so a typo shows up later as a `no data` line. Aliases work (`gold` →
  `GC=F`).

Password fields are never shown back. Leaving one blank keeps the stored value;
tick *clear stored ntfy login* to remove the ntfy username/password/token.

The setup screen **never times out** — with no credentials there is nothing
else useful the node could be doing.

### 4.2 Changing settings later

| Situation | What to do |
|---|---|
| Node is online | Browse to its IP (it's in the startup notification) and log in as `admin` |
| Wi-Fi is down | It falls back to the `StockGraber-Setup-XXXX` network (portal at `192.168.4.1`, admin login required) while retrying your router every 10 s |
| Moved house, or forgot the admin password | Press **EN**, release it, then **immediately press and hold BOOT** for ~2 s — setup portal, no login required |
| Remotely | `<passphrase> factoryreset` over ntfy |

Every save restarts the node; there is no partial "apply live" state.

**Timing the BOOT button matters.** The ESP32-S3 samples BOOT (GPIO0) *during*
reset — holding it through the EN press puts the chip into its serial
bootloader instead, and it just sits there. Press BOOT only after letting go of
EN; the node watches for it for 2 s (`BOOT_WINDOW_MS`) after `press BOOT now`
appears on serial. That window only runs on a provisioned board, so a normal
boot is ~2 s slower than it used to be.

`factoryreset` wipes the settings and reboots into setup. The watchlist is kept.

Physical access bypasses the admin password by design: anyone holding the board
can reflash it anyway, so a BOOT-button escape hatch costs no real security and
saves you from a lockout.

### 4.3 Boards set up with the old `sg_secrets.h` / `FORCE_PROVISION` flow

They keep working after this firmware is flashed: the Wi-Fi, topic, passphrase
and watchlist already in NVS are read as before. They have **no admin
password**, though, so the LAN settings page answers *Settings locked* until
one is set. Use the BOOT-button step above once, fill in an admin password
(other blank password fields keep their stored values), and save.

### Compile-time constants

Not stored in NVS — a normal reflash is enough.

| Setting | File | Line | Default |
|---|---|---|---|
| `WL_MAX` | `sg_config.h` | 73 | `200` — max watchlist size (see §8) |
| `AL_MAX` | `sg_alerts.h` | 28 | `50` — max price alerts (see §8) |
| `ALERT_CHECK_MS` | `sg_alerts.cpp` | 11 | 5 min between price-alert sweeps |
| `SG_VERSION` | `StockGraberAlerts.ino` | 33 | `"1.2.0"` — shown at boot and in the startup notice; bump per release |
| `WDT_TIMEOUT_S` | `StockGraberAlerts.ino` | 34 | `120` — watchdog reboot timeout |
| `NTP_RESYNC_MS` | `StockGraberAlerts.ino` | 35 | 24 h between clock resyncs |
| `DUE_CHECK_MS` | `StockGraberAlerts.ino` | 36 | 60 s between "is a market due?" checks |
| `WIFI_TIMEOUT_MS` | `StockGraberAlerts.ino` | 37 | 30 s for the first join before falling back to the AP |
| `WIFI_RETRY_MS` | `StockGraberAlerts.ino` | 38 | 10 s between re-joins while in AP fallback |
| `BOOT_WINDOW_MS` | `StockGraberAlerts.ino` | 39 | 2 s after boot to press BOOT for setup |

---

## 5. Compile and flash

### 5.0 Identify the board

Port names follow the USB socket, not the board, so **always check before
uploading** — especially with more than one node on the bench.

```
arduino-cli board list
```

The S3 appears as *ESP32 Family Device* on `/dev/cu.usbmodem*` (macOS),
`/dev/ttyACM*` (Linux) or `COM*` (Windows). To confirm *which* physical board
that is, read its MAC:

```
esptool --port /dev/cu.usbmodem14301 --no-stub flash-id
```

Keep a note of each node's MAC — the last four hex digits are also the suffix
of its `StockGraber-Setup-XXXX` network, which tells nodes apart during setup.

### 5.1 Compile

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc" StockGraberAlerts
```

A good build ends with roughly:

```
Sketch uses 1154607 bytes (36%) of program storage space. Maximum is 3145728 bytes.
Global variables use 47656 bytes (14%) of dynamic memory...
```

If it fails, check §2 — the usual causes are ArduinoJson v6 and ESP32 core 2.x.

### 5.2 Upload

```
arduino-cli upload -p /dev/cu.usbmodem14301 --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc" StockGraberAlerts
```

Substitute your own port. A successful upload ends with:

```
Hash of data verified.
Hard resetting via RTS pin...
```

`No changed sectors found` instead means the board already had that exact
binary — not an error.

### 5.3 First flash of a new board

Compile and upload as above — there is nothing to edit first. On boot the
serial log shows:

```
[sg] web: AP "StockGraber-Setup-A1B2" up, portal at http://192.168.4.1 (auth off)
[sg] === SETUP MODE ===
[sg] setup: join wifi "StockGraber-Setup-A1B2" then open http://192.168.4.1
```

Then follow §4.1. After saving, the node restarts and should reach the healthy
boot in §6.

### 5.4 Updating firmware on a running node

Compile, upload. One pass. Settings and watchlist are preserved — ordinary
reflashing does not touch NVS.

### Arduino IDE equivalent

1. Apply the board settings in §3.
2. *Tools → Port* → select the board.
3. *Sketch → Upload*.
4. For a new board, continue with §4.1.

---

## 6. Verify

Serial monitor at **115200**:

```
arduino-cli monitor -p /dev/cu.usbmodem14401 --config baudrate=115200
```

A healthy boot:

```
[sg] StockGraber Alerts v1.2.0 (ESP32-S3) starting
[sg] PSRAM: 8388608 bytes free 8386096
[sg] press BOOT now to open the setup portal
[sg] wifi: connecting to your-wifi
[sg] wifi: ok 192.168.1.42
[sg] ntp: clock synced
[sg] web: settings page at http://192.168.1.42
[sg] startup notice sent
[sg] running; topic: your-topic-esp; settings: http://192.168.1.42
```

`PSRAM: 8388608` confirms the OPI setting. Anything less (or `0`) means PSRAM
is misconfigured.

The node also **pushes a startup notification to ntfy on every boot**:

```
StockGraber - node started
Reset: power-on
IP: 192.168.1.42
Settings: http://192.168.1.42
Firmware: v1.2.0
Watchlist: 5 of 200 symbols
Heap: 158 KB free
PSRAM: 8192 KB
```

The `Reset:` line distinguishes a power cycle from a watchdog trip, a crash, or
a brownout — so an unexplained reboot is diagnosable without a serial cable.
After a Wi-Fi outage the node sends a similar *back online* notice, since its
IP (and so the settings URL) may have changed.

After startup the log is **silent by design**. `loop()` only prints on an event
(report sent, command answered, Wi-Fi lost). Silence means healthy; a repeating
boot banner means it's stuck in a restart loop.

---

## 7. Storage and persistence

Settings and the watchlist live in NVS (flash) and are committed on every
change, so they survive power loss and ordinary reflashing.

To inspect what's actually stored:

```
esptool --port /dev/cu.usbmodem14401 read-flash 0x9000 0x5000 nvs.bin
strings nvs.bin | grep -A1 watch
```

**What wipes NVS:**

- *Tools → Erase All Flash Before Sketch Upload* → **Enabled**. This is a sticky
  IDE setting — turning it on for another project silently arms it for this one.
- `esptool erase-flash`.
- `<passphrase> factoryreset` over ntfy — settings only; the watchlist is kept.

---

## 8. Limits

| Limit | Value | What happens at the edge |
|---|---|---|
| **Watchlist** | **200 symbols** (`WL_MAX` in `sg_config.h`) | `add` replies *"Already holding the maximum of 200 symbols"* and refuses. |
| **Price alerts** | **50** (`AL_MAX` in `sg_alerts.h`) | `alert` replies *"Already holding the maximum of 50 alerts"*. |
| NVS string | 4000 bytes | 200 typical symbols join to ~1.4 KB. A list of unusually long codes that outgrows it is refused with a *storage write failed* reply, never truncated. |
| ntfy message body | 4 KB (ntfy.sh) | Long replies are **split** into parts of ≤ 3.8 KB: `… (1)`, `… (2)`, `… (3/3)`. ~48 symbol lines fit in a part at worst, ~130 at best. |
| `status` runtime | ~3 s per symbol | 200 symbols ≈ 10–11 minutes. Parts arrive as they fill, so you see progress. The watchdog is fed per symbol. |
| Yahoo rate limit | not published | The real ceiling. Fetches are spaced 300 ms apart and a `429` is retried once after 2 s. |

The caps are *runtime* limits, not storage or memory ones — the 8 MB PSRAM is
barely touched. What binds is Yahoo: `status` makes one HTTPS request per
symbol, and hundreds in a row is what draws HTTP 429. 200 symbols has headroom;
past ~300, expect throttling and a `status` that takes a quarter of an hour.

While a `status` or a report is running the node is busy: it won't answer other
commands or serve the settings page until it finishes. Commands sent in the
meantime are answered afterwards, not lost.

---

## 9. Use it

Subscribe to your topic in the ntfy app. Reports arrive ~20 min after each
market's close. From the app you can send:

| Send | Get |
|---|---|
| `<pass> status` | the whole watchlist |
| `<pass> list` | just the symbols |
| `<pass> AAPL` | one symbol |
| `<pass> add TSLA` | add it (verified against Yahoo first) |
| `<pass> remove IBM` | drop it |
| `<pass> help` | the command list |
| `<pass> alert AAPL above 200` | one-shot price alert (see §9.1) |
| `<pass> alerts` | list price alerts |
| `<pass> unalert AAPL` | cancel AAPL's alerts (`unalert AAPL 200` for just one) |
| `<pass> factoryreset` | wipe settings, reboot into setup (watchlist kept) |

`add`/`remove`/`alert`/`unalert` **require** the passphrase even though queries don't — ntfy.sh
topics are public, so an ungated topic stays read-only. Aliases work too
(`add gold` → `GC=F`, `add btc` → `BTC-USD`).

**Symbol format is Yahoo's**, which isn't always the obvious one:

| Instrument | Code |
|---|---|
| Gold futures | `GC=F` |
| Bitcoin | `BTC-USD` |
| Berkshire class B | `BRK-B` (hyphen, **not** `BRK.B`) |
| Hang Seng index | `^HSI` |

`add` fetches the symbol before storing it, so a bad code is rejected with
*"No data for X — not added"* rather than silently poisoning the watchlist.

### 9.1 Price alerts

```
<pass> alert AAPL above 200
<pass> alert gold below 1,900
<pass> alert BTC-USD > 60000
```

- **One-shot.** The alert pushes once (high priority, 🔔) and deletes itself.
- **Any symbol** — it doesn't have to be on the watchlist. It is checked against
  Yahoo first, and **refused if the price is already past the target**.
- **Checked every 5 minutes** while that symbol's market is open (regular hours
  plus 20 min after the close; crypto around the clock; futures all weekday).
- **Brief touches count.** If a whole session has passed since you set the
  alert, the session's high/low is compared, so a spike between two checks
  still fires. On the day you set it, only the live price counts — the day's
  earlier high doesn't.
- **Delay.** Up to 5 minutes from the check interval, plus Yahoo's own delay:
  near-live for US stocks and crypto, **~15 minutes for Hong Kong** and some
  other exchanges.
- Alerts are stored in NVS, survive reboots and reflashing, and can also be
  edited on the settings page (§4.1).

---

## Files

| File | Role |
|---|---|
| `StockGraberAlerts.ino` | boot flow (setup / AP fallback / online), Wi-Fi, NTP, scheduler, dispatch, watchdog, startup notice |
| `sg_market.*` | market classification, close times, DST via POSIX TZ |
| `sg_calc.*` | SMA 50/200 + cross, 52-week, change %, line formatting |
| `sg_yahoo.*` | Yahoo chart API → OHLC, and latest quote for alerts |
| `sg_ntfy.*` | publish + poll, with optional ntfy login; multi-part messages |
| `sg_commands.*` | command parsing |
| `sg_alerts.*` | price alerts: storage, parsing, periodic checks |
| `sg_config.*` | settings + watchlist in NVS |
| `sg_webconfig.*` | setup portal and settings page |

## Design notes

- **Timezones use the C library.** Unlike the MicroPython node (which hand-rolls
  DST), the ESP32 Arduino core has real POSIX TZ support, so New York is just
  `EST5EDT,M3.2.0,M11.1.0` and DST is handled for you.
- **Yahoo is fetched over HTTP/1.0, then buffered.** Two transport quirks forced
  this. Yahoo answers HTTP/1.1 with `Transfer-Encoding: chunked`, and ESP32's
  `HTTPClient::getStream()` hands back the raw socket with the chunk framing
  still in it, which the JSON parser chokes on — hence `useHTTP10(true)`. Yahoo
  also sends no `Content-Length`, so the body ends at connection close; parsing
  straight off the socket mistakes a gap in the TCP stream for EOF and truncates
  the document. `getString()` reads to genuine completion. At ~30 KB the buffer
  clears the 16 KB PSRAM threshold, so it lands in SPIRAM and the internal heap
  stays free for TLS.
- **An ArduinoJson filter still applies.** Only the four arrays actually needed
  are kept, so the parsed document stays far smaller than the raw response.
- **Polling, not streaming.** The Pi holds a long-lived ntfy connection; a
  multi-hour TLS session on a microcontroller is the least reliable thing you can
  build, so this polls every ~12 s. `since=<id>` gives dedup for free.
- **Alerts use the small quote.** The same chart endpoint at `range=1d` is
  ~1.3 KB against ~30 KB for a report fetch, and its `meta` block has the live
  price, the session's high/low and when the session opened — everything an
  alert needs. One quote is fetched per `loop()` pass, 1.5 s apart, so an alert
  sweep never blocks commands or the settings page.
- **No price cache.** A handful of symbols fetched once per close is cheaper than
  managing flash writes.
- **The watchdog is fed per symbol.** Both the report path and the `status`
  command reset the task WDT inside their fetch loops; without that, a long
  watchlist reboots the board mid-reply.
- **Settings live in NVS, not the binary.** The portal is the WOL-NTFY
  project's design: SoftAP + captive portal before provisioning, SoftAP + STA as
  a fallback while Wi-Fi is down, and a login-protected page on the LAN. DNS is
  only hijacked in AP mode; doing it on the LAN would break name resolution.
- **The `ok` sentinel is written last** when saving, so a power cut mid-save
  reads as unprovisioned rather than half-valid.

## Troubleshooting

| Symptom | Cause |
|---|---|
| Serial Monitor blank, board otherwise fine | **USB CDC On Boot** disabled — `Serial` is going to the UART pins. |
| Boots into `SETUP MODE` every time | No complete settings in NVS (or a save was cut off). Finish the form in §4.1. |
| `wifi unreachable; portal at …` | Router down, wrong password, or a 5 GHz-only network. Fix it from the `StockGraber-Setup-XXXX` network (§4.2); the node also keeps retrying on its own. |
| Settings page says *Settings locked* | Board set up by the old `sg_secrets.h` flow — no admin password yet. See §4.3. |
| BOOT press does nothing / board seems dead after it | BOOT was held through the reset, which starts the serial bootloader. Press EN, release, *then* press BOOT (§4.2). |
| `HTTP 401` / `HTTP 403` from ntfy | The server wants a login, or rejected it. Check username/password/token on the settings page. |
| `PSRAM: 0 bytes` | PSRAM set to Disabled or QSPI; must be **OPI**. |
| Every symbol reports `no data` | Check serial for the `[sg] fetch failed:` line — it names the real error. |
| `HTTP 404` on a symbol | Wrong Yahoo code (e.g. `BRK.B` instead of `BRK-B`). |
| `HTTP 429` | Yahoo rate-limiting. The node retries once; if it persists, shorten the watchlist or send `status` less often. |
| An alert never fired | Market closed (checks run only in market hours), or the price hasn't reached it yet. `alerts` shows what's still pending; serial shows `[sg] alerts: … quote failed` for bad codes set on the settings page. |
| `status` arrives in several messages | Expected above ~48 symbols — ntfy's 4 KB limit (§8). |
| Compile errors mentioning `JsonDocument` or `Allocator` | ArduinoJson v6 installed; upgrade to v7. |
| Compile error on `esp_task_wdt_init` | ESP32 core 2.x installed; needs 3.x. |

## Known limitations

- **The settings page is plain HTTP.** The admin login is HTTP Basic auth, so
  it crosses your LAN (or the setup AP) unencrypted. Fine on a home network;
  don't expose port 80 to the internet.
- **`status` is slow** — every symbol is fetched fresh, ~3 s each, and the
  node is busy for the duration (§8).
- **Alerts are not instant** — up to 5 minutes, plus Yahoo's delay (§9.1).
- **TLS certificates are not verified** (`setInsecure()`); bundle a CA cert if
  that matters to you.
- **Yahoo access is the fragile part.** `yfinance` quietly handles cookies,
  crumbs and rate limits; here it's a browser-ish User-Agent. If Yahoo starts
  demanding a crumb, `yahooFetch()` is the thing to fix.
- **The daily report path has not been observed running.** It shares the fetch
  code that the `status` command exercises, but it only fires after a market
  close and hasn't yet been seen end-to-end.

## Verified status

Compiled, flashed and running on hardware. Confirmed working: build, upload,
Wi-Fi association, NTP sync, 8 MB PSRAM, NVS persistence across reflashing,
ntfy command polling and replies, Yahoo fetches for all watchlist symbols,
passphrase-gated `add`/`remove`, and the startup notification — all with the
old compile-time `sg_secrets.h` setup.

**The web portal is verified by build only on this board.** Setup mode, the
captive-portal pop-up, AP fallback, the BOOT-button window, ntfy login and
`factoryreset` have not yet been run on the S3.

**v1.1.0 (price alerts, 200-symbol watchlist) is verified by build and host
test only.** Alert parsing, storage, the trigger rules (session high/low vs.
live price, failed-publish retry) and the multi-part splitter were run on the
Mac against the real `String` class; the `range=1d` quote fields were checked
against live Yahoo for US, Canada, Hong Kong, futures, index and crypto codes.
Not yet run on the board.

**v1.2.0 (network calls in their own task) was run on the board.** Wi-Fi join,
NTP, the startup notice, ntfy polling and three end-of-day reports, with the
settings page probed every 0.5 s: 297 of 297 answered, the slowest in 0.27 s.
The v1.1.0 code beforehand ran an hour there without a crash and answered
`status` commands; price alerts firing on the board are still untested.

Build size: ~1.15 MB (36% of the 3 MB app partition), 47.7 KB static RAM.
