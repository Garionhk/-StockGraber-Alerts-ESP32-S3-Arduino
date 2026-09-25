#include "sg_config.h"
#include <Preferences.h>

static const char* NS = "stockgraber";

// NVS keys are limited to 15 characters, hence the short names below.
static const char* SETTING_KEYS[] = {
  "ssid", "pass", "server", "topic", "nuser", "npass", "token", "secret",
  "admpass", "rep_on", "cmd_on", "delay", "prox", "poll", "ok",
};

void cfgLoad(Settings& s) {
  Preferences p;
  p.begin(NS, true);                       // read-only
  s.wifiSsid       = p.getString("ssid", "");
  s.wifiPassword   = p.getString("pass", "");
  s.ntfyServer     = p.getString("server", "https://ntfy.sh");
  s.ntfyTopic      = p.getString("topic", "");
  s.ntfyUser       = p.getString("nuser", "");
  s.ntfyPass       = p.getString("npass", "");
  s.ntfyToken      = p.getString("token", "");
  s.cmdSecret      = p.getString("secret", "");
  s.adminPass      = p.getString("admpass", "");
  s.reportEnabled  = p.getBool("rep_on", true);
  s.cmdEnabled     = p.getBool("cmd_on", true);
  s.reportDelayMin = p.getInt("delay", 20);
  s.proximityPct   = p.getFloat("prox", 2.0f);
  s.pollSeconds    = p.getInt("poll", 12);
  // cfgSave() writes "ok" false first and true last, so a power cut mid-save
  // reads as unprovisioned. A board seeded by the old compile-time
  // FORCE_PROVISION flow has no "ok" key at all, hence the default of true.
  bool complete    = p.getBool("ok", true);
  p.end();

  if (s.ntfyServer.length() == 0) s.ntfyServer = "https://ntfy.sh";
  if (s.reportDelayMin < 15) s.reportDelayMin = 15;
  if (s.reportDelayMin > 30) s.reportDelayMin = 30;
  if (s.proximityPct <= 0.0f || s.proximityPct > 50.0f) s.proximityPct = 2.0f;
  if (s.pollSeconds < 5) s.pollSeconds = 5;

  // A record missing essentials is worse than no record: it would skip the
  // setup portal and then fail forever.
  s.provisioned = complete && s.wifiSsid.length() && s.ntfyTopic.length();
}

bool cfgSave(const Settings& s) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putBool("ok",       false);
  p.putString("ssid",   s.wifiSsid);
  p.putString("pass",   s.wifiPassword);
  p.putString("server", s.ntfyServer);
  p.putString("topic",  s.ntfyTopic);
  p.putString("nuser",  s.ntfyUser);
  p.putString("npass",  s.ntfyPass);
  p.putString("token",  s.ntfyToken);
  p.putString("secret", s.cmdSecret);
  p.putString("admpass", s.adminPass);
  p.putBool("rep_on",   s.reportEnabled);
  p.putBool("cmd_on",   s.cmdEnabled);
  p.putInt("delay",     s.reportDelayMin);
  p.putFloat("prox",    s.proximityPct);
  p.putInt("poll",      s.pollSeconds);
  // Written last so a power cut mid-save leaves the record marked incomplete
  // rather than half-valid.
  bool ok = p.putBool("ok", true) == 1;
  p.end();
  return ok;
}

void cfgClearSettings() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  // Key by key rather than clear(): "watch" and the ls_* dates live here too.
  for (const char* key : SETTING_KEYS) p.remove(key);
  p.putBool("ok", false);
  p.end();
}

String cfgLastSent(const char* market) {
  Preferences p;
  p.begin(NS, true);
  String key = String("ls_") + market;     // e.g. ls_COMMODITY (13 chars, ok)
  String v = p.getString(key.c_str(), "");
  p.end();
  return v;
}

void cfgSetLastSent(const char* market, const String& isoDate) {
  Preferences p;
  p.begin(NS, false);
  String key = String("ls_") + market;
  p.putString(key.c_str(), isoDate);
  p.end();
}

// ---- watchlist -----------------------------------------------------------

static String wlRaw() {
  Preferences p;
  p.begin(NS, true);
  String v = p.getString("watch", "");
  p.end();
  return v;
}

// Returns false if NVS refused the write (e.g. the joined string outgrew the
// 4000-byte limit). putString reports that only through its return value, so
// ignoring it would silently drop the change.
static bool wlWrite(const std::vector<String>& list) {
  String joined;
  for (size_t i = 0; i < list.size(); i++) {
    if (i) joined += ",";
    joined += list[i];
  }
  Preferences p;
  p.begin(NS, false);
  size_t written = p.putString("watch", joined);
  p.end();
  return written == joined.length();
}

std::vector<String> wlAll() {
  std::vector<String> out;
  String raw = wlRaw();
  int start = 0;
  while (start < (int)raw.length()) {
    int comma = raw.indexOf(',', start);
    if (comma < 0) comma = raw.length();
    String sym = raw.substring(start, comma);
    sym.trim();
    if (sym.length()) out.push_back(sym);
    start = comma + 1;
  }
  return out;
}

bool wlContains(const String& symbol) {
  String want = symbol;
  want.toUpperCase();
  for (const String& s : wlAll()) {
    String have = s;
    have.toUpperCase();
    if (have == want) return true;
  }
  return false;
}

bool wlAdd(const String& symbol) {
  String sym = symbol;
  sym.trim();
  sym.toUpperCase();
  if (!sym.length() || wlContains(sym)) return false;
  std::vector<String> list = wlAll();
  if (list.size() >= WL_MAX) return false;      // full; caller explains why
  list.push_back(sym);
  return wlWrite(list);
}

bool wlReplace(const std::vector<String>& list) {
  if (list.size() > WL_MAX) return false;
  return wlWrite(list);
}

bool wlRemove(const String& symbol) {
  String sym = symbol;
  sym.trim();
  sym.toUpperCase();
  std::vector<String> list = wlAll(), kept;
  bool found = false;
  for (const String& s : list) {
    String up = s;
    up.toUpperCase();
    if (up == sym) { found = true; continue; }
    kept.push_back(s);
  }
  if (!found) return false;
  return wlWrite(kept);
}
