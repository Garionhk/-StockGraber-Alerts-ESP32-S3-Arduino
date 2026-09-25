#include "sg_commands.h"
#include "sg_alerts.h"
#include "sg_calc.h"
#include "sg_ntfy.h"
#include "sg_webconfig.h"
#include "sg_yahoo.h"

#include <esp_task_wdt.h>
#include <time.h>

// Breathing room between Yahoo fetches in a long `status`, so 200 back-to-back
// requests don't read as a burst and draw HTTP 429.
static const uint32_t FETCH_GAP_MS = 300;

static const char* HELP =
  "Commands:\n"
  "  status         — full watchlist summary\n"
  "  list           — watchlist symbols\n"
  "  <CODE>         — one symbol (must be on the watchlist)\n"
  "  add <CODE>     — add to the watchlist (passphrase required)\n"
  "  remove <CODE>  — drop from the watchlist (passphrase required)\n"
  "  alert <CODE> above|below <price>\n"
  "                 — one-shot price alert (passphrase required)\n"
  "  alerts         — list price alerts\n"
  "  unalert <CODE> [price]\n"
  "                 — cancel alerts on a symbol (passphrase required)\n"
  "  factoryreset   — wipe settings, reboot into setup (passphrase required;\n"
  "                   the watchlist is kept)\n"
  "  help           — this message";

static bool g_restartPending = false;

bool cmdRestartPending() { return g_restartPending; }

static String countPhrase(int n) {
  return String(n) + (n == 1 ? " symbol" : " symbols");
}

// Fetch + analyse one symbol; never throws, returns ok=false on failure.
static Analysis analyseSymbol(const String& symbol, double proximityPct) {
  std::vector<double> closes, highs, lows;
  String lastDate, err;
  if (!yahooFetch(symbol, closes, highs, lows, lastDate, err)) {
    Serial.println("[sg] fetch failed: " + symbol + " (" + err + ")");
    Analysis a;
    a.symbol = symbol;
    a.symbol.toUpperCase();
    a.ok = false;
    a.error = err;
    return a;
  }
  return calcAnalyze(symbol, closes, highs, lows, lastDate, proximityPct);
}

static String firstToken(const String& s, int& endIdx) {
  int i = 0;
  while (i < (int)s.length() && isspace((unsigned char)s[i])) i++;
  int start = i;
  while (i < (int)s.length() && !isspace((unsigned char)s[i])) i++;
  endIdx = i;
  return s.substring(start, i);
}

static bool isRemoveVerb(const String& v) {
  return v == "remove" || v == "rm" || v == "del" || v == "delete";
}

static bool alertSet(const String& spec, String& title, String& body) {
  Alert a;
  String err;
  if (!alParse(spec, a, err)) { body = err; return true; }

  std::vector<Alert> list = alAll();
  for (const Alert& have : list) {
    if (have.symbol == a.symbol && have.above == a.above &&
        alFmtPrice(have.target) == alFmtPrice(a.target)) {
      body = "Already set: " + alDescribe(a) + ".";
      return true;
    }
  }
  if (list.size() >= AL_MAX) {
    title = "StockGraber - alerts full";
    body = "Already holding the maximum of " + String((int)AL_MAX) +
           " alerts. Cancel one with unalert first.";
    return true;
  }

  Quote q;                                    // verify before storing
  if (!yahooQuote(a.symbol, q, err)) {
    body = "No data for " + a.symbol + " — alert not set. Check the Yahoo code "
           "(gold is GC=F, bitcoin is BTC-USD).";
    return true;
  }
  if (a.above ? q.price >= a.target : q.price <= a.target) {
    body = a.symbol + " is already at " + alFmtPrice(q.price) + ", " +
           (a.above ? "above " : "below ") + alFmtPrice(a.target) +
           " — alert not set.";
    return true;
  }

  time_t now = time(nullptr);
  a.created = now > 1600000000 ? (uint32_t)now : 0;
  if (!alAdd(a)) {
    body = "Could not save the alert — storage write failed.";
    return true;
  }
  title = "StockGraber - alert set";
  body = "Alert set: " + alDescribe(a) + " (now " + alFmtPrice(q.price) + ").\n" +
         "Checked every 5 min while the market is open; fires once.";
  return true;
}

static bool alertCancel(const String& spec, String& title, String& body) {
  int end;
  String symbol = calcResolveSymbol(firstToken(spec, end));
  String priceText = spec.substring(end);
  priceText.trim();
  double target = 0;
  if (priceText.length()) {
    target = alParsePrice(priceText);
    if (target <= 0) {
      body = "\"" + priceText + "\" is not a price.";
      return true;
    }
  }
  int n = alRemove(symbol, target);
  if (n < 0) {
    body = "Could not save the change — storage write failed.";
  } else if (n == 0) {
    body = "No alert on " + symbol +
           (target > 0 ? " at " + alFmtPrice(target) : String("")) +
           ". Send 'alerts' to see them.";
  } else {
    title = "StockGraber - alert cancelled";
    body = "Cancelled " + String(n) + (n == 1 ? " alert" : " alerts") + " on " +
           symbol + ". " + String((int)alAll().size()) + " left.";
  }
  return true;
}

bool cmdHandle(const String& text, const Settings& s, String& title, String& body) {
  String cmd = text;
  cmd.trim();
  if (!cmd.length()) return false;

  // --- passphrase gate --------------------------------------------------
  String secret = s.cmdSecret;
  secret.trim();
  if (secret.length()) {
    int end;
    String given = firstToken(cmd, end);
    String a = given, b = secret;
    a.toLowerCase(); b.toLowerCase();
    if (a != b) return false;                 // silently ignore
    cmd = cmd.substring(end);
    cmd.trim();
    if (!cmd.length()) return false;
  }

  String low = cmd;
  low.toLowerCase();

  if (low == "status" || low == "report" || low == "all") {
    std::vector<String> syms = wlAll();
    title = "StockGraber - watchlist";
    if (syms.empty()) { body = "Watchlist is empty."; return true; }
    // Up to 200 symbols at ~3 s each: full parts go out as they fill, and the
    // last one comes back to the caller like any other reply.
    NtfyChunker out(s, title, 3, NTFY_BOT_TAG, 3, NTFY_BOT_TAG);
    for (size_t i = 0; i < syms.size(); i++) {
      if (i) delay(FETCH_GAP_MS);
      out.addLine(calcSymbolLine(analyseSymbol(syms[i], s.proximityPct)));
      esp_task_wdt_reset();   // one HTTPS fetch per symbol; don't trip the dog
    }
    int prio;
    String tags;
    out.takeLast(title, body, prio, tags);
    return true;
  }

  if (low == "list") {
    std::vector<String> syms = wlAll();
    title = "StockGraber - watchlist";
    if (syms.empty()) { body = "Watchlist is empty."; return true; }
    body = "";
    for (size_t i = 0; i < syms.size(); i++) {
      if (i) body += ", ";
      body += syms[i];
    }
    return true;
  }

  if (low == "factoryreset") {
    title = "StockGraber - factory reset";
    if (!secret.length()) {
      body = "Set a command passphrase before using factoryreset.";
      return true;
    }
    cfgClearSettings();
    g_restartPending = true;
    body = "Settings cleared (watchlist kept). Rebooting into setup mode -- join "
           "the \"" + webconfig::apSsid() + "\" Wi-Fi to reconfigure.";
    return true;
  }

  if (low == "alerts") {
    std::vector<Alert> list = alAll();
    title = "StockGraber - price alerts";
    if (list.empty()) { body = "No price alerts set."; return true; }
    body = String((int)list.size()) + " of " + String((int)AL_MAX) + ":";
    for (const Alert& a : list) body += "\n" + alDescribe(a);
    return true;
  }

  if (low == "help" || low == "?" || low == "commands") {
    title = "StockGraber - help";
    body = HELP;
    return true;
  }

  // --- watchlist edits: "add TSLA" / "remove IBM" ------------------------
  int vEnd;
  String verbRaw = firstToken(cmd, vEnd);
  String rest = cmd.substring(vEnd);
  rest.trim();
  String verb = verbRaw;
  verb.toLowerCase();

  // --- price alerts: "alert AAPL above 200" / "unalert AAPL [200]" --------
  if (rest.length() && (verb == "alert" || verb == "unalert")) {
    title = "StockGraber";
    if (!secret.length()) {
      body = "Set a command passphrase before setting or cancelling alerts.";
      return true;
    }
    return verb == "alert" ? alertSet(rest, title, body)
                           : alertCancel(rest, title, body);
  }

  // Only a verb when an argument follows, so a bare word still falls through
  // to a symbol query (a ticker literally named ADD isn't shadowed).
  if (rest.length() && (verb == "add" || isRemoveVerb(verb))) {
    if (!secret.length()) {
      title = "StockGraber";
      body = "Set a command passphrase before adding or removing symbols.";
      return true;
    }
    int argEnd;
    String symbol = calcResolveSymbol(firstToken(rest, argEnd));

    if (isRemoveVerb(verb)) {
      title = "StockGraber";
      if (!wlContains(symbol)) {
        body = symbol + " is not on your watchlist.";
        return true;
      }
      if (!wlRemove(symbol)) {
        body = "Could not save the change — watchlist storage write failed.";
        return true;
      }
      title = "StockGraber - removed " + symbol;
      body  = "Removed " + symbol + ". " + countPhrase(wlAll().size()) + " left.";
      return true;
    }

    if (wlContains(symbol)) {
      title = "StockGraber";
      body = symbol + " is already on your watchlist.";
      return true;
    }
    if (wlAll().size() >= WL_MAX) {
      title = "StockGraber - watchlist full";
      body = "Already holding the maximum of " + String((int)WL_MAX) +
             " symbols. Remove one before adding " + symbol + ".";
      return true;
    }
    Analysis a = analyseSymbol(symbol, s.proximityPct);   // verify before storing
    if (!a.ok) {
      title = "StockGraber";
      body = "No data for " + symbol + " — not added. Check the Yahoo code "
             "(gold is GC=F, bitcoin is BTC-USD).";
      return true;
    }
    if (!wlAdd(symbol)) {
      title = "StockGraber";
      body = "Could not save " + symbol + " — watchlist storage write failed.";
      return true;
    }
    title = "StockGraber - added " + symbol;
    body  = "Added to watchlist (" + countPhrase(wlAll().size()) + " of " +
            String((int)WL_MAX) + "):\n" + calcSymbolLine(a);
    return true;
  }

  // --- single-symbol query ----------------------------------------------
  int tEnd;
  String symbol = calcResolveSymbol(firstToken(cmd, tEnd));
  if (!wlContains(symbol)) {
    title = "StockGraber";
    body = symbol + " is not on your watchlist. Send 'list' to see it, or 'help'.";
    return true;
  }
  title = "StockGraber - " + symbol;
  body  = calcSymbolLine(analyseSymbol(symbol, s.proximityPct));
  return true;
}
