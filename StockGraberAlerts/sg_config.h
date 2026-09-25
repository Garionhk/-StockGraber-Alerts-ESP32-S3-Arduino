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

// Watchlist (stored as one comma-separated string).
//
// The cap is a runtime limit, not a storage one: NVS would hold ~700 symbols in
// its 4000-byte string, and the reply buffer costs ~4 KB of heap at most, which
// is nothing. What binds is that a `status` fetches every symbol over HTTPS in
// one command, so list length sets both how long that takes (~3 s each) and how
// big the ntfy reply gets.
//
// 50 keeps a full status inside a few minutes, but the reply is nearer ntfy's
// 4 KB limit than it looks: at SYMBOL_LINE_MAX it is 3,999 bytes, and ntfy
// answers 413 rather than truncating. That needs nearly every symbol flagged at
// once to happen. See README §8 before raising this.
static const size_t WL_MAX = 50;

std::vector<String> wlAll();
bool     wlContains(const String& symbol);
bool     wlAdd(const String& symbol);
bool     wlRemove(const String& symbol);

// Replaces the whole list; used by the settings page. The caller has already
// normalised, de-duplicated and capped it.
bool     wlReplace(const std::vector<String>& list);
