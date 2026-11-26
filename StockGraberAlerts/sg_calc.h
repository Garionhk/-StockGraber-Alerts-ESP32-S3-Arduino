// Report maths + line formatting -- the C++ twin of the Pi's
// notify.analyze_symbol() / _symbol_line().
//
// pandas does this on the Pi, but the report only needs a rolling mean, a
// max/min over the last 252 bars, and a percentage. Calculations use double so
// the rounding matches the Python exactly.
#pragma once

#include <Arduino.h>
#include <vector>

struct Analysis {
  String symbol;
  bool   ok = false;
  String error;
  double close = 0;
  double change = 0;        // close minus the previous close
  double changePct = 0;
  bool   golden = false;
  bool   death = false;
  bool   nearHigh = false;
  bool   nearLow = false;
  String date;
};

Analysis calcAnalyze(const String& symbol,
                     const std::vector<double>& closes,
                     const std::vector<double>& highs,
                     const std::vector<double>& lows,
                     const String& lastDate,
                     double proximityPct);

// "NVDA       149.30 (-6.08)  ▼ -3.91%" plus any flags. The (change) column
// is ESP32-only; the Pi's line has no absolute change.
String calcSymbolLine(const Analysis& a);

// Worst-case bytes one calcSymbolLine() produces, plus its newline: an expanded
// display name ("Dogecoin (DOGE-USD)"), a grouped price, and both flag slots
// filled (a cross and a 52-week proximity) measures 76 bytes, plus up to 16 for
// the "(-12,345.67)" change column -- the arrows and
// ⚑ are three bytes each in UTF-8, not one. Callers building a multi-symbol
// body reserve with this so the string is one stable allocation instead of
// reallocating once per symbol while fetches cycle 48 KB buffers underneath it.
static const size_t SYMBOL_LINE_MAX = 96;
String calcFmtPrice(double v);              // "66,270.32"
String calcDisplayName(const String& symbol);
String calcResolveSymbol(const String& token);   // "gold" -> "GC=F"
