#include "sg_commands.h"
#include "sg_calc.h"
#include "sg_webconfig.h"
#include "sg_yahoo.h"

#include <esp_task_wdt.h>

static const char* HELP =
  "Commands:\n"
  "  status         — full watchlist summary\n"
  "  list           — watchlist symbols\n"
  "  <CODE>         — one symbol (must be on the watchlist)\n"
  "  add <CODE>     — add to the watchlist (passphrase required)\n"
  "  remove <CODE>  — drop from the watchlist (passphrase required)\n"
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
    body = "";
    // Size the reply up front. It stays alive across every fetch below, and
    // growing it one line at a time would leave a small block migrating through
    // the heap while each fetch claims and frees 48 KB around it.
    body.reserve(syms.size() * SYMBOL_LINE_MAX);
    for (size_t i = 0; i < syms.size(); i++) {
      if (i) body += "\n";
      body += calcSymbolLine(analyseSymbol(syms[i], s.proximityPct));
      esp_task_wdt_reset();   // one HTTPS fetch per symbol; don't trip the dog
    }
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
