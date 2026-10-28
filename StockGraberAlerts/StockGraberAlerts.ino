/*
  StockGraber Alerts -- ESP32-S3 node (Arduino)
  =============================================
  The C++ twin of the Raspberry Pi daemon: sends each market's end-of-day
  report to ntfy once per day after its close, and answers remote commands
  (status / list / <CODE> / add / remove / help) sent from the ntfy app.

  Board  : ESP32S3 Dev Module   (see README: PSRAM must be set to "OPI PSRAM")
  Library: ArduinoJson v7  (Library Manager)

  FIRST RUN -- nothing to edit before compiling. Flash, then join the
  "StockGraber-Setup-XXXX" Wi-Fi from a phone and fill in the page that pops up
  (or browse to http://192.168.4.1). Wi-Fi, ntfy and passphrases are stored in
  flash (NVS) and survive reflashing; change them later at the node's own IP.

  NB: give this node its OWN ntfy topic. Sharing the Pi's topic means duplicate
  reports and two bots answering every command.
*/

#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "sg_alerts.h"
#include "sg_calc.h"
#include "sg_commands.h"
#include "sg_config.h"
#include "sg_market.h"
#include "sg_ntfy.h"
#include "sg_webconfig.h"
#include "sg_yahoo.h"

static const char*    SG_VERSION      = "1.2.0";

static const uint32_t WDT_TIMEOUT_S   = 120;      // reboot if a loop wedges
static const uint32_t NTP_RESYNC_MS   = 24UL * 3600UL * 1000UL;
static const uint32_t DUE_CHECK_MS    = 60UL * 1000UL;
static const uint32_t FETCH_GAP_MS    = 300;      // between report fetches (Yahoo 429)
static const uint32_t WIFI_TIMEOUT_MS = 30000;    // first join at boot
static const uint32_t WIFI_RETRY_MS   = 10000;    // re-join while in AP fallback
static const uint32_t BOOT_WINDOW_MS  = 2000;     // time to press BOOT for setup
static const uint32_t NET_STACK_BYTES = 12288;    // TLS + JSON; loop() gets 8 KB

// What the node is doing. Setup blocks inside setup() until a save restarts
// the board; the other two alternate as Wi-Fi comes and goes.
enum class Mode { SetupPortal, ApFallback, Online };

// Startup notice owed by the network task: set by the loop when it goes
// online, cleared once the notice is out.
enum class Announce { None, Boot, Reconnect };

Settings          g_settings;
static volatile Mode     g_mode = Mode::SetupPortal;
static volatile Announce g_announce = Announce::None;
static String     g_since = "10s";     // start from now, not ntfy's cache
static uint32_t   g_lastPoll     = 0;
static uint32_t   g_lastDueCheck = 0;
static uint32_t   g_lastNtpSync  = 0;
static uint32_t   g_lastWifiTry  = 0;

static void log(const String& msg) { Serial.println("[sg] " + msg); }

// ---- setup helpers -------------------------------------------------------

static bool wifiConnect(const Settings& s, uint32_t timeoutMs = WIFI_TIMEOUT_MS) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!s.wifiSsid.length()) { log("no wifi ssid configured"); return false; }

  log("wifi: connecting to " + s.wifiSsid);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(s.wifiSsid.c_str(), s.wifiPassword.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    log("wifi: ok " + WiFi.localIP().toString());
    return true;
  }
  log("wifi: FAILED");
  return false;
}

static bool ntpSync(uint32_t timeoutMs = 20000) {
  // UTC here; each market's local time comes from its own POSIX TZ string.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t start = millis();
  while (time(nullptr) < 1700000000 && millis() - start < timeoutMs) delay(250);
  if (time(nullptr) < 1700000000) { log("ntp: FAILED"); return false; }
  log("ntp: clock synced");
  return true;
}

static const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external reset";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "crash (panic)";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_BROWNOUT:  return "brownout (power dip)";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    default:                return "unknown";
  }
}

// Announce startup to ntfy. A reboot is otherwise invisible unless you happen
// to have the serial monitor open, and the reset reason distinguishes a plain
// power cycle from a watchdog trip or a brownout. Also sent when Wi-Fi comes
// back after a fallback, since the IP -- and so the settings URL -- may differ.
static void announceStart(const Settings& s, bool atBoot) {
  if (!s.ntfyTopic.length()) return;
  String body = (atBoot ? "Reset: " + String(resetReasonName())
                        : String("Wi-Fi reconnected")) +
                "\nIP: " + WiFi.localIP().toString() +
                "\nSettings: " + webconfig::settingsUrl() +
                "\nFirmware: v" + SG_VERSION +
                "\nWatchlist: " + String((int)wlAll().size()) + " of " +
                String((int)WL_MAX) + " symbols" +
                "\nHeap: " + String(ESP.getFreeHeap() / 1024) + " KB free";
  if (ESP.getPsramSize())
    body += "\nPSRAM: " + String(ESP.getPsramSize() / 1024) + " KB";
  String err;
  bool ok = ntfyPublish(s,
                        atBoot ? "StockGraber - node started"
                               : "StockGraber - back online",
                        body, 3, NTFY_BOT_TAG, err);
  log(String("startup notice ") + (ok ? "sent" : "FAILED: " + err));
}

// ---- report path ---------------------------------------------------------

static Analysis analyseSymbol(const String& symbol, double prox) {
  std::vector<double> closes, highs, lows;
  String lastDate, err;
  if (!yahooFetch(symbol, closes, highs, lows, lastDate, err)) {
    log("fetch failed: " + symbol + " (" + err + ")");
    Analysis a;
    a.symbol = symbol;
    a.symbol.toUpperCase();
    a.ok = false;
    a.error = err;
    return a;
  }
  return calcAnalyze(symbol, closes, highs, lows, lastDate, prox);
}

static void sendMarketReport(const Settings& s, const char* market,
                             const std::vector<String>& symbols) {
  // The date is only known once a fetch succeeds, but full parts go out as they
  // fill, so title the report from the market's local date up front.
  String day = marketLocalDate(market, time(nullptr));
  String title = String("StockGraber - ") + marketLabel(market) + " close " + day;
  String tags = String("chart_with_upwards_trend,") + NTFY_BOT_TAG;
  String crossTags = String("warning,") + NTFY_BOT_TAG;

  // A large watchlist can outgrow ntfy's 4 KB body; the chunker splits it, and
  // only the part holding a golden/death cross is sent at the higher priority.
  NtfyChunker out(s, title, 3, tags, 4, crossTags);
  for (size_t i = 0; i < symbols.size(); i++) {
    if (i) delay(FETCH_GAP_MS);
    Analysis a = analyseSymbol(symbols[i], s.proximityPct);
    if (a.ok && (a.golden || a.death)) out.markUrgent();
    out.addLine(calcSymbolLine(a));
    esp_task_wdt_reset();             // fetches are slow; keep the dog happy
  }

  String err;
  bool ok = out.finish(err);
  log(String("report ") + market + (ok ? " sent" : " FAILED: " + err) +
      (out.parts() > 1 ? " (" + String(out.parts()) + " parts)" : String("")));
}

static void checkDue(const Settings& s) {
  if (!s.reportEnabled || !s.ntfyTopic.length()) return;
  std::vector<String> all = wlAll();
  if (all.empty()) return;

  int delay_ = s.reportDelayMin;
  if (delay_ < 15) delay_ = 15;
  if (delay_ > 30) delay_ = 30;

  time_t now = time(nullptr);
  for (int i = 0; i < MARKET_COUNT; i++) {
    const char* market = MARKET_ORDER[i];

    std::vector<String> mine;
    for (const String& sym : all)
      if (strcmp(marketOf(sym), market) == 0) mine.push_back(sym);
    if (mine.empty()) continue;

    if (!marketIsDue(market, now, delay_)) continue;
    String today = marketLocalDate(market, now);
    if (cfgLastSent(market) == today) continue;

    // Mark before sending so a failure can't refire every minute.
    cfgSetLastSent(market, today);
    sendMarketReport(s, market, mine);
  }
}

// ---- command path --------------------------------------------------------

static void checkCommands(const Settings& s) {
  if (!s.cmdEnabled || !s.ntfyTopic.length()) return;

  std::vector<String> messages;
  if (!ntfyPoll(s, g_since, messages)) return;

  for (const String& text : messages) {
    String title, body;
    if (!cmdHandle(text, s, title, body)) continue;   // ignored
    String err;
    ntfyPublish(s, title, body, 3, NTFY_BOT_TAG, err);

    // A handled command starts with the passphrase; keep it out of the log.
    String shown = text;
    shown.trim();
    if (s.cmdSecret.length()) {
      int sp = shown.indexOf(' ');
      shown = sp < 0 ? String("") : shown.substring(sp + 1);
      shown.trim();
    }
    log("replied to: " + shown.substring(0, 40));
    esp_task_wdt_reset();

    if (cmdRestartPending()) {
      log("factory reset; restarting into setup");
      delay(1000);
      ESP.restart();
    }
  }
}

// ---- modes ---------------------------------------------------------------

// Pressing BOOT (BOOT_PIN, GPIO0 -- defined by the core) just after a reset forces the setup portal on an already-
// provisioned node -- the way back from a forgotten admin password or a moved
// network. It has to be pressed AFTER releasing EN: GPIO0 held low through
// the reset itself puts the ESP32 into its serial bootloader instead.
static bool bootButtonPressed() {
  pinMode(BOOT_PIN, INPUT_PULLUP);
  log("press BOOT now to open the setup portal");
  uint32_t start = millis();
  while (millis() - start < BOOT_WINDOW_MS) {
    if (digitalRead(BOOT_PIN) == LOW) return true;
    delay(20);
  }
  return false;
}

// Portal with no Wi-Fi behind it. Deliberately has no timeout: without
// credentials there is no Wi-Fi, no ntfy and nothing to do, so a page telling
// you how to fix that is the most useful thing the node can be showing.
//
// Deliberately open, too. It is only reached when the node is unprovisioned
// (nothing to protect yet) or someone pressed BOOT, which already implies
// physical access -- and anyone holding the board can reflash it regardless.
static void runSetupPortal() {
  g_mode = Mode::SetupPortal;
  webconfig::beginAP(/*requireAuth=*/false);

  log("=== SETUP MODE ===");
  uint32_t lastNag = 0;
  while (!webconfig::saved()) {
    webconfig::tick();
    if (millis() - lastNag > 5000) {
      lastNag = millis();
      log("setup: join wifi \"" + webconfig::apSsid() + "\" then open " +
          webconfig::settingsUrl());
    }
    delay(5);
  }
  // A save restarts the board, so this is never reached.
}

// NTP and the startup notice are network calls, so they are left to the
// network task; the settings page is up as soon as this returns.
static void goOnline(bool atBoot) {
  webconfig::beginSTA();
  g_announce = atBoot ? Announce::Boot : Announce::Reconnect;
  g_mode = Mode::Online;
  log("running; topic: " + g_settings.ntfyTopic +
      "; settings: " + webconfig::settingsUrl());
}

// Everything that talks to ntfy, Yahoo or NTP runs here. Those calls block for
// seconds at a time (minutes for a big report), and on the loop task that
// froze the settings page with them. Works from a settings snapshot per pass.
static void netTask(void*) {
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    if (g_mode == Mode::Online && WiFi.status() == WL_CONNECTED) {
      Settings s = cfgSnapshot();

      if (g_announce != Announce::None) {
        bool atBoot = g_announce == Announce::Boot;
        g_announce = Announce::None;
        if (ntpSync()) g_lastNtpSync = millis();
        announceStart(s, atBoot);
      }
      if (millis() - g_lastPoll >= (uint32_t)s.pollSeconds * 1000UL) {
        g_lastPoll = millis();
        checkCommands(s);
      }
      if (millis() - g_lastDueCheck >= DUE_CHECK_MS) {
        g_lastDueCheck = millis();
        checkDue(s);
      }
      alertsTick(s);                     // at most one quote per pass
      if (millis() - g_lastNtpSync >= NTP_RESYNC_MS) {
        if (ntpSync()) g_lastNtpSync = millis();
      }
    }
    delay(10);
  }
}

// Wi-Fi is configured but unreachable. Bring the portal up so the settings can
// be fixed, while the station side keeps retrying underneath -- a router coming
// back should not need a reboot.
static void startApFallback() {
  g_mode = Mode::ApFallback;
  g_lastWifiTry = millis();
  webconfig::beginAP(/*requireAuth=*/true);
  log("wifi unreachable; portal at " + webconfig::settingsUrl() +
      " while retrying in background");
}

// ---- arduino entry points ------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  log(String("StockGraber Alerts v") + SG_VERSION + " (" + ESP.getChipModel() +
      ") starting");
  log("PSRAM: " + String(ESP.getPsramSize()) + " bytes free " +
      String(ESP.getFreePsram()));
  if (ESP.getPsramSize() == 0)
    log("WARNING: no PSRAM detected -- set Tools > PSRAM to 'OPI PSRAM'");

  cfgLoad(g_settings);

  // Only worth waiting for on a provisioned board; otherwise setup is next anyway.
  bool forced = g_settings.provisioned && bootButtonPressed();
  if (forced) log("BOOT pressed -- forcing setup portal");
  if (!g_settings.provisioned || forced) runSetupPortal();   // never returns

  // Watchdog: a wedged HTTPS call reboots the board rather than hanging.
  // Core 3.x (IDF 5) starts the TWDT itself, so reconfigure rather than init.
  esp_task_wdt_config_t wdtCfg = {
      .timeout_ms     = WDT_TIMEOUT_S * 1000,
      .idle_core_mask = 0,
      .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&wdtCfg);
  esp_task_wdt_add(NULL);

  // Same core and priority as loop(); both block in socket waits, not spins.
  xTaskCreatePinnedToCore(netTask, "sg_net", NET_STACK_BYTES, nullptr, 1,
                          nullptr, ARDUINO_RUNNING_CORE);

  if (wifiConnect(g_settings)) goOnline(/*atBoot=*/true);
  else                         startApFallback();
}

void loop() {
  webconfig::tick();                       // the portal answers in every mode
  esp_task_wdt_reset();

  switch (g_mode) {
    case Mode::SetupPortal:
      break;                               // runSetupPortal() never returns

    case Mode::ApFallback:
      // WIFI_AP_STA: the station side is still trying underneath the AP.
      if (WiFi.status() == WL_CONNECTED) {
        log("wifi: back up " + WiFi.localIP().toString() + "; leaving AP mode");
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        goOnline(/*atBoot=*/false);
      } else if (millis() - g_lastWifiTry >= WIFI_RETRY_MS) {
        g_lastWifiTry = millis();
        WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPassword.c_str());
      }
      break;

    case Mode::Online:
      // Polls, reports and alerts run in netTask; this only watches Wi-Fi.
      if (WiFi.status() != WL_CONNECTED) {
        log("wifi lost");
        startApFallback();
      }
      break;
  }

  delay(10);                               // yield; the web server needs ticks
}
