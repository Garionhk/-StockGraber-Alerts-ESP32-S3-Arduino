#include "sg_alerts.h"

#include <time.h>

#include "sg_calc.h"
#include "sg_market.h"
#include "sg_ntfy.h"
#include "sg_yahoo.h"

static const char*    AL_KEY             = "alerts";
static const uint32_t ALERT_CHECK_MS     = 5UL * 60UL * 1000UL;
static const uint32_t ALERT_FETCH_GAP_MS = 1500;   // between quotes in a sweep
static const uint32_t ALERT_FIRST_MS     = 30UL * 1000UL;   // first sweep after boot
static const time_t   CLOCK_VALID        = 1600000000;     // NTP has run

static void log(const String& msg) { Serial.println("[sg] alerts: " + msg); }

// ---- storage -------------------------------------------------------------
//
// One string in NVS: "AAPL>200@1790270000;BTC-USD<60000@1790270123". At ~25
// bytes an alert, AL_MAX of them is ~1.3 KB against the 4000-byte limit.

static String fmtTarget(double v) {
  // Significant digits, not decimals: a DOGE target of 0.0812345 survives,
  // and 60000 stays "60000" rather than "6e+04".
  char buf[32];
  snprintf(buf, sizeof(buf), "%.10g", v);
  return String(buf);
}

std::vector<Alert> alAll() {
  std::vector<Alert> out;
  String raw = cfgGetString(AL_KEY);
  int start = 0;
  while (start < (int)raw.length()) {
    int end = raw.indexOf(';', start);
    if (end < 0) end = raw.length();
    String rec = raw.substring(start, end);
    start = end + 1;

    int op = rec.indexOf('>');
    bool above = op >= 0;
    if (op < 0) op = rec.indexOf('<');
    int at = rec.indexOf('@');
    if (op <= 0) continue;                    // damaged record; skip it

    Alert a;
    a.symbol  = rec.substring(0, op);
    a.above   = above;
    a.target  = rec.substring(op + 1, at < 0 ? rec.length() : at).toDouble();
    a.created = at < 0 ? 0 : (uint32_t)strtoul(rec.c_str() + at + 1, nullptr, 10);
    if (a.target > 0) out.push_back(a);
  }
  return out;
}

bool alReplace(const std::vector<Alert>& list) {
  if (list.size() > AL_MAX) return false;
  String raw;
  for (const Alert& a : list) {
    if (raw.length()) raw += ";";
    raw += a.symbol + (a.above ? ">" : "<") + fmtTarget(a.target) + "@" +
           String(a.created);
  }
  return cfgPutString(AL_KEY, raw);
}

bool alAdd(const Alert& a) {
  std::vector<Alert> list = alAll();
  if (list.size() >= AL_MAX) return false;
  list.push_back(a);
  return alReplace(list);
}

static bool sameTarget(double a, double b) {
  return fabs(a - b) <= 1e-9 * (fabs(a) > 1 ? fabs(a) : 1);
}

int alRemove(const String& symbol, double target) {
  std::vector<Alert> list = alAll(), kept;
  int removed = 0;
  for (const Alert& a : list) {
    bool match = a.symbol.equalsIgnoreCase(symbol) &&
                 (target <= 0 || sameTarget(a.target, target));
    if (match) removed++;
    else kept.push_back(a);
  }
  if (!removed) return 0;
  return alReplace(kept) ? removed : -1;
}

// Drops exactly this alert (symbol, direction and target), leaving any others
// on the same symbol in place.
static bool removeExact(const Alert& gone) {
  std::vector<Alert> list = alAll(), kept;
  bool found = false;
  for (const Alert& a : list) {
    if (!found && a.symbol == gone.symbol && a.above == gone.above &&
        sameTarget(a.target, gone.target)) {
      found = true;
      continue;
    }
    kept.push_back(a);
  }
  return !found || alReplace(kept);
}

// ---- parsing + formatting ------------------------------------------------

double alParsePrice(const String& text) {
  String t;
  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == ',' || c == '$') continue;
    if (!isdigit((unsigned char)c) && c != '.') return 0;
    t += c;
  }
  if (!t.length() || t.indexOf('.') != t.lastIndexOf('.')) return 0;
  return t.toDouble();
}

bool alParse(const String& text, Alert& out, String& err) {
  // Space out the operators so "AAPL>200" tokenises like "AAPL > 200".
  String norm;
  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == '>' || c == '<') { norm += ' '; norm += c; norm += ' '; }
    else if (c == '=') continue;              // ">=" reads the same as ">"
    else norm += c;
  }

  String tok[4];
  int n = 0;
  int i = 0;
  while (i < (int)norm.length() && n < 4) {
    while (i < (int)norm.length() && isspace((unsigned char)norm[i])) i++;
    int start = i;
    while (i < (int)norm.length() && !isspace((unsigned char)norm[i])) i++;
    if (i > start) tok[n++] = norm.substring(start, i);
  }
  while (i < (int)norm.length() && isspace((unsigned char)norm[i])) i++;
  if (n != 3 || i < (int)norm.length()) {
    err = "Use: alert <CODE> above|below <price>, e.g. alert AAPL above 200";
    return false;
  }

  String dir = tok[1];
  dir.toLowerCase();
  if (dir == "above" || dir == "over" || dir == ">") out.above = true;
  else if (dir == "below" || dir == "under" || dir == "<") out.above = false;
  else {
    err = "\"" + tok[1] + "\" is not above or below.";
    return false;
  }

  out.target = alParsePrice(tok[2]);
  if (out.target <= 0) {
    err = "\"" + tok[2] + "\" is not a price.";
    return false;
  }

  out.symbol = calcResolveSymbol(tok[0]);
  if (out.symbol.length() > 20) {
    err = "\"" + out.symbol + "\" is not a valid symbol.";
    return false;
  }
  for (unsigned int k = 0; k < out.symbol.length(); k++) {
    char c = out.symbol[k];
    if (!isalnum((unsigned char)c) && !strchr(".-=^", c)) {
      err = "\"" + out.symbol + "\" is not a valid symbol.";
      return false;
    }
  }
  return true;
}

String alFmtPrice(double v) {
  if (fabs(v) >= 1) return calcFmtPrice(v);
  char buf[24];
  snprintf(buf, sizeof(buf), "%.6g", v);      // 0.081234, not 0.08
  return String(buf);
}

String alDescribe(const Alert& a) {
  return a.symbol + (a.above ? " above " : " below ") + alFmtPrice(a.target);
}

// ---- checking ------------------------------------------------------------

static std::vector<String> g_pending;         // symbols left in this sweep
static uint32_t g_lastSweep = 0;
static uint32_t g_lastFetch = 0;
static bool     g_started   = false;

// Fetches one symbol and fires whatever it satisfies.
static void checkSymbol(const Settings& s, const String& symbol) {
  Quote q;
  String err;
  if (!yahooQuote(symbol, q, err)) {
    log(symbol + " quote failed (" + err + ")");
    return;
  }

  time_t now = time(nullptr);
  // Re-read: a command or the settings page may have changed the list since
  // the sweep began.
  for (const Alert& stored : alAll()) {
    if (!stored.symbol.equalsIgnoreCase(symbol)) continue;
    Alert a = stored;

    // Set before the clock was synced (from the setup page): start the clock
    // now and judge by price until the next session.
    if (a.created == 0 && now > CLOCK_VALID) {
      Alert stamped = a;
      stamped.created = (uint32_t)now;
      std::vector<Alert> list = alAll();
      for (Alert& x : list)
        if (x.symbol == a.symbol && x.above == a.above &&
            sameTarget(x.target, a.target) && x.created == 0)
          x.created = stamped.created;
      alReplace(list);
      a = stamped;
    }

    // Yahoo's day high/low can trail the last price by a tick (seen on
    // DOGE-USD: price 0.09631, day high 0.09622), so take the extreme of both.
    bool wholeSession = q.sessionStart > 0 && (long long)a.created < q.sessionStart;
    double probe = q.price;
    if (wholeSession)
      probe = a.above ? max(q.dayHigh, q.price) : min(q.dayLow, q.price);
    bool hit = a.above ? probe >= a.target : probe <= a.target;
    if (!hit) continue;

    String title = "StockGraber - " + alDescribe(a);
    String body = calcDisplayName(a.symbol) + " is at " + alFmtPrice(q.price) +
                  " (target: " + (a.above ? "above " : "below ") +
                  alFmtPrice(a.target) + ")";
    bool touchedOnly = a.above ? q.price < a.target : q.price > a.target;
    if (touchedOnly)
      body += "\nIt reached " + alFmtPrice(probe) + " earlier in the session.";
    body += "\nDay range: " + alFmtPrice(q.dayLow) + " - " + alFmtPrice(q.dayHigh);
    body += "\nThis alert is now removed.";

    String perr;
    if (!ntfyPublish(s, title, body, 4, String("bell,") + NTFY_BOT_TAG, perr)) {
      // Kept, so the next sweep tries again rather than losing the alert.
      log("fire " + alDescribe(a) + " FAILED: " + perr);
      continue;
    }
    log("fired " + alDescribe(a) + " at " + alFmtPrice(probe));
    if (!removeExact(a)) log("could not remove " + alDescribe(a));
  }
}

void alertsTick(const Settings& s) {
  if (!s.ntfyTopic.length()) return;
  uint32_t nowMs = millis();
  if (!g_started) {
    // First sweep shortly after coming online rather than five minutes later.
    g_started = true;
    g_lastSweep = nowMs - ALERT_CHECK_MS + ALERT_FIRST_MS;
  }

  if (g_pending.empty()) {
    if (nowMs - g_lastSweep < ALERT_CHECK_MS) return;
    g_lastSweep = nowMs;

    time_t now = time(nullptr);
    if (now < CLOCK_VALID) return;            // market hours need the clock
    for (const Alert& a : alAll()) {
      if (!marketIsOpen(marketOf(a.symbol), now)) continue;
      bool dup = false;
      for (const String& p : g_pending) dup = dup || p == a.symbol;
      if (!dup) g_pending.push_back(a.symbol);
    }
    return;
  }

  if (nowMs - g_lastFetch < ALERT_FETCH_GAP_MS) return;
  g_lastFetch = nowMs;
  String symbol = g_pending.back();
  g_pending.pop_back();
  checkSymbol(s, symbol);
}
