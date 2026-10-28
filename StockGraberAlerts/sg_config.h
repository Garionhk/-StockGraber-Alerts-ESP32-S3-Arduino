// Settings + watchlist storage, backed by NVS (Preferences).
//
// The Pi keeps these in SQLite; on the ESP32 they live in flash key/value
// storage, which survives reflashing the sketch (but not "Erase All Flash").
//
// Nothing user-specific is compiled in any more: Wi-Fi, ntfy and passphrases
// are entered on the web portal (sg_webconfig.*) and stored here, so a build
// can be shared without leaking a password.
#pragma once

#include <Arduino.h>
#include <vector>

struct Settings {
  String wifiSsid;
  String wifiPassword;
  String ntfyServer;      // https://ntfy.sh
  String ntfyTopic;       // give this node its OWN topic, not the Pi's

  // ntfy login -- token wins over user/password; all blank means an open server
  String ntfyUser;
  String ntfyPass;
  String ntfyToken;

  String cmdSecret;       // command passphrase; REQUIRED for add/remove

  // Login for the settings page. Deliberately separate from cmdSecret: that one
  // travels through the ntfy topic, and it should not also unlock the config.
  String adminPass;

  bool   reportEnabled;
  bool   cmdEnabled;
  int    reportDelayMin;  // minutes after close, clamped 15..30
  float  proximityPct;    // 52-week high/low threshold
  int    pollSeconds;     // how often to check for commands

  bool   provisioned = false;   // Wi-Fi + topic stored and the save completed
};

extern Settings g_settings;

// g_settings is replaced by the settings page (loop task) while the network
// task works from it, so the network task takes a copy rather than reading it
// in place, and the page swaps it under the same lock.
Settings cfgSnapshot();
void     cfgSetCurrent(const Settings& s);

// Username paired with Settings::adminPass on the settings page.
static const char* const ADMIN_USER = "admin";

void     cfgLoad(Settings& s);
bool     cfgSave(const Settings& s);

// Wipes the settings only. The watchlist and the per-market last-sent dates
// share the namespace but are left alone.
void     cfgClearSettings();

// Per-market once-a-day guard, mirroring the Pi's last_sent_<MARKET>.
String   cfgLastSent(const char* market);
void     cfgSetLastSent(const char* market, const String& isoDate);

// Raw string values in the same NVS namespace, for modules that keep their own
// record (price alerts). cfgPutString reports a refused write as false.
String   cfgGetString(const char* key);
bool     cfgPutString(const char* key, const String& value);

// Watchlist (stored as one comma-separated string).
//
// The cap is a runtime limit, not a storage one: 200 typical symbols join to
// ~1.4 KB against NVS's 4000-byte string (a list of unusually long codes that
// outgrows it is refused by wlWrite, not truncated). The 8 MB PSRAM makes
// memory a non-issue too.
//
// What binds is that a `status` fetches every symbol over HTTPS in one command
// -- ~3 s each, so 200 symbols take about ten minutes -- and that the sheer
// number of requests is what draws Yahoo's HTTP 429. Replies longer than
// ntfy's 4 KB limit are split across several messages (NtfyChunker). See
// README §8 before raising this.
static const size_t WL_MAX = 200;

std::vector<String> wlAll();
bool     wlContains(const String& symbol);
bool     wlAdd(const String& symbol);
bool     wlRemove(const String& symbol);

// Replaces the whole list; used by the settings page. The caller has already
// normalised, de-duplicated and capped it.
bool     wlReplace(const std::vector<String>& list);
