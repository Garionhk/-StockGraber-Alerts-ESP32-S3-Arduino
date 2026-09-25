#include "sg_calc.h"

static const int FAST = 50, SLOW = 200, WINDOW_52W = 252;

struct Label { const char* sym; const char* name; };
static const Label LABELS[] = {
  {"GC=F", "Gold"},     {"SI=F", "Silver"},   {"PL=F", "Platinum"},
  {"PA=F", "Palladium"},{"HG=F", "Copper"},   {"CL=F", "Crude"},
  {"BZ=F", "Brent"},    {"NG=F", "NatGas"},
  {"BTC-USD", "Bitcoin"},  {"ETH-USD", "Ethereum"}, {"SOL-USD", "Solana"},
  {"XRP-USD", "XRP"},      {"DOGE-USD", "Dogecoin"},{"ADA-USD", "Cardano"},
  {"BNB-USD", "BNB"},      {"LTC-USD", "Litecoin"},
};
static const int LABEL_COUNT = sizeof(LABELS) / sizeof(LABELS[0]);

struct Alias { const char* word; const char* sym; };
static const Alias ALIASES[] = {
  {"gold", "GC=F"},   {"silver", "SI=F"},  {"platinum", "PL=F"},
  {"copper", "HG=F"}, {"oil", "CL=F"},     {"crude", "CL=F"},
  {"brent", "BZ=F"},  {"natgas", "NG=F"},  {"gas", "NG=F"},
  {"btc", "BTC-USD"}, {"bitcoin", "BTC-USD"}, {"eth", "ETH-USD"},
  {"ethereum", "ETH-USD"}, {"sol", "SOL-USD"}, {"solana", "SOL-USD"},
  {"xrp", "XRP-USD"}, {"doge", "DOGE-USD"},
};
static const int ALIAS_COUNT = sizeof(ALIASES) / sizeof(ALIASES[0]);

static double meanRange(const std::vector<double>& v, int start, int end) {
  double total = 0;
  for (int i = start; i < end; i++) total += v[i];
  return total / (end - start);
}

// Golden/death cross on the most recent bar only.
//
// Mirrors data.ma_cross_markers()'s validity rule: a bar counts only from
// index >= SLOW (the explicit warm-up), and both it and the previous bar must
// be valid -- so no false signal on the first bar the slow MA exists.
static void crossOnLastBar(const std::vector<double>& closes,
                           bool& golden, bool& death) {
  golden = death = false;
  int n = closes.size();
  if (n < SLOW + 2) return;                 // need index n-2 >= SLOW
  double fNow  = meanRange(closes, n - FAST, n);
  double fPrev = meanRange(closes, n - FAST - 1, n - 1);
  double sNow  = meanRange(closes, n - SLOW, n);
  double sPrev = meanRange(closes, n - SLOW - 1, n - 1);
  bool aboveNow = fNow > sNow, abovePrev = fPrev > sPrev;
  golden = aboveNow && !abovePrev;
  death  = !aboveNow && abovePrev;
}

Analysis calcAnalyze(const String& symbol,
                     const std::vector<double>& closes,
                     const std::vector<double>& highs,
                     const std::vector<double>& lows,
                     const String& lastDate,
                     double proximityPct) {
  Analysis a;
  a.symbol = symbol;
  a.symbol.trim();
  a.symbol.toUpperCase();

  if (closes.size() < 2) {
    a.ok = false;
    a.error = "not enough data";
    return a;
  }

  a.close = closes[closes.size() - 1];
  double prev = closes[closes.size() - 2];
  a.changePct = prev ? (a.close - prev) / prev * 100.0 : 0.0;

  crossOnLastBar(closes, a.golden, a.death);

  int start = (int)highs.size() - WINDOW_52W;
  if (start < 0) start = 0;
  double hi = highs[start], lo = lows[start];
  for (size_t i = start; i < highs.size(); i++) {
    if (highs[i] > hi) hi = highs[i];
    if (lows[i] < lo)  lo = lows[i];
  }
  double thr = proximityPct / 100.0;
  a.nearHigh = hi > 0 && (hi - a.close) / hi <= thr;
  a.nearLow  = lo > 0 && (a.close - lo) / lo <= thr;

  a.date = lastDate;
  a.ok = true;
  return a;
}

String calcFmtPrice(double v) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%.2f", v);
  String s(buf);
  bool neg = s.startsWith("-");
  if (neg) s = s.substring(1);

  int dot = s.indexOf('.');
  String whole = s.substring(0, dot);
  String frac  = s.substring(dot + 1);

  String grouped = "";
  while (whole.length() > 3) {
    grouped = "," + whole.substring(whole.length() - 3) + grouped;
    whole = whole.substring(0, whole.length() - 3);
  }
  grouped = whole + grouped;
  return (neg ? "-" : "") + grouped + "." + frac;
}

String calcDisplayName(const String& symbol) {
  String up = symbol;
  up.toUpperCase();
  for (int i = 0; i < LABEL_COUNT; i++)
    if (up == LABELS[i].sym)
      return String(LABELS[i].name) + " (" + symbol + ")";
  return symbol;
}

String calcResolveSymbol(const String& token) {
  String low = token;
  low.toLowerCase();
  for (int i = 0; i < ALIAS_COUNT; i++)
    if (low == ALIASES[i].word) return String(ALIASES[i].sym);
  String up = token;
  up.toUpperCase();
  return up;
}

static String padTo(const String& text, int width) {
  String out = text;
  while ((int)out.length() < width) out += " ";
  return out;
}

String calcSymbolLine(const Analysis& a) {
  String name = calcDisplayName(a.symbol);
  if (!a.ok) return padTo(name, 10) + " — no data";

  const char* arrow = (a.changePct >= 0) ? "▲" : "▼";   // up/down
  const char* sign  = (a.changePct >= 0) ? "+" : "-";
  double mag = a.changePct < 0 ? -a.changePct : a.changePct;

  char pct[24];
  snprintf(pct, sizeof(pct), "%.2f", mag);

  String line = padTo(name, 10) + " " + calcFmtPrice(a.close) + "  " +
                arrow + " " + sign + pct + "%";

  String tags = "";
  if (a.golden)   tags += (tags.length() ? "  " : "") + String("⚑ golden cross");
  if (a.death)    tags += (tags.length() ? "  " : "") + String("⚑ death cross");
  if (a.nearHigh) tags += (tags.length() ? "  " : "") + String("⚑ near 52w high");
  if (a.nearLow)  tags += (tags.length() ? "  " : "") + String("⚑ near 52w low");
  if (tags.length()) line += "   " + tags;
  return line;
}
