#include "sg_market.h"

const char* const MARKET_ORDER[] = {
  "US", "CA", "HK", "CN", "COMMODITY", "CRYPTO", "OTHER"
};
const int MARKET_COUNT = 7;

// US/Canada follow the post-2007 rule (2nd Sun Mar -> 1st Sun Nov); Hong Kong
// and China have no DST. Commodities and crypto trade around the clock, so
// they piggyback the US close and report Mon-Fri like everything else.
struct MarketDef { const char* key; const char* label; const char* tz; int closeMin; };

static const MarketDef DEFS[] = {
  {"US",        "US",          "EST5EDT,M3.2.0,M11.1.0", 16 * 60},
  {"CA",        "Canada",      "EST5EDT,M3.2.0,M11.1.0", 16 * 60},
  {"HK",        "Hong Kong",   "HKT-8",                  16 * 60},
  {"CN",        "China",       "CST-8",                  15 * 60},
  {"COMMODITY", "Commodities", "EST5EDT,M3.2.0,M11.1.0", 16 * 60},
  {"CRYPTO",    "Crypto",      "EST5EDT,M3.2.0,M11.1.0", 16 * 60},
  {"OTHER",     "Other",       "EST5EDT,M3.2.0,M11.1.0", 16 * 60},
};

static const MarketDef& defOf(const char* market) {
  for (int i = 0; i < MARKET_COUNT; i++)
    if (strcmp(DEFS[i].key, market) == 0) return DEFS[i];
  return DEFS[MARKET_COUNT - 1];           // OTHER
}

static bool endsWith(const String& s, const char* suffix) {
  int n = strlen(suffix);
  return s.length() >= n && s.substring(s.length() - n) == suffix;
}

static bool oneOf(const String& s, const char* const* list, int n) {
  for (int i = 0; i < n; i++) if (s == list[i]) return true;
  return false;
}

const char* marketOf(const String& symbol) {
  String s = symbol;
  s.trim();
  s.toUpperCase();

  // Crypto first. NB: "BRK-B" also ends in "-B", so match the full quote
  // suffixes only -- never a bare "-".
  if (endsWith(s, "-USD") || endsWith(s, "-USDT") || endsWith(s, "-USDC"))
    return "CRYPTO";
  if (endsWith(s, "=F")) return "COMMODITY";

  static const char* const CA_IDX[] = {"^GSPTSE"};
  static const char* const HK_IDX[] = {"^HSI", "^HSCE"};
  static const char* const CN_IDX[] = {"^SSEC", "399001.SZ"};
  static const char* const US_IDX[] = {"^GSPC", "^DJI", "^IXIC", "^NDX", "^RUT", "^VIX"};

  if (endsWith(s, ".TO") || endsWith(s, ".V") || oneOf(s, CA_IDX, 1)) return "CA";
  if (endsWith(s, ".HK") || oneOf(s, HK_IDX, 2)) return "HK";
  if (endsWith(s, ".SS") || endsWith(s, ".SZ") || oneOf(s, CN_IDX, 2)) return "CN";
  if (s.startsWith("^")) return oneOf(s, US_IDX, 6) ? "US" : "OTHER";
  return "US";
}

const char* marketLabel(const char* market)      { return defOf(market).label; }
const char* marketTz(const char* market)         { return defOf(market).tz; }
int         marketCloseMinutes(const char* market){ return defOf(market).closeMin; }

void marketLocalTime(const char* market, time_t utc, struct tm& out) {
  setenv("TZ", defOf(market).tz, 1);
  tzset();
  localtime_r(&utc, &out);
}

bool marketIsDue(const char* market, time_t utc, int delayMin) {
  struct tm t;
  marketLocalTime(market, utc, t);
  if (t.tm_wday == 0 || t.tm_wday == 6) return false;   // Sun/Sat locally
  int minutes = t.tm_hour * 60 + t.tm_min;
  return minutes >= marketCloseMinutes(market) + delayMin;
}

String marketLocalDate(const char* market, time_t utc) {
  struct tm t;
  marketLocalTime(market, utc, t);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return String(buf);
}
