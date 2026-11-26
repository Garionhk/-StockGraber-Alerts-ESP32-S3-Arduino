// Market model: which market a Yahoo symbol belongs to, and when it closes.
//
// Unlike the MicroPython node (which hand-rolls DST), the ESP32 Arduino core
// ships a real POSIX timezone implementation, so DST is handled by the C
// library from strings like "EST5EDT,M3.2.0,M11.1.0".
#pragma once

#include <Arduino.h>
#include <time.h>
#include <vector>

// Report order, matching notify.MARKET_ORDER on the Pi.
extern const char* const MARKET_ORDER[];
extern const int MARKET_COUNT;

const char* marketOf(const String& symbol);
// Position of a market in MARKET_ORDER (OTHER and unknown keys last).
int         marketRank(const char* market);
// Sorts for display: by market in MARKET_ORDER, then by code A-Z, ignoring
// case and an index's leading '^' (so ^DJI sits among the D's).
void        sortByMarket(std::vector<String>& symbols);        // "US", "HK", "FX", "CRYPTO", ...
const char* marketLabel(const char* market);       // "Hong Kong", "Crypto", ...
const char* marketTz(const char* market);          // POSIX TZ string
int         marketCloseMinutes(const char* market);// minutes past local midnight

// Local wall-clock for a market at UTC time `utc`.
void   marketLocalTime(const char* market, time_t utc, struct tm& out);
// True when the close + delay has passed, on a local weekday. The caller still
// applies the once-per-day guard (cfgLastSent).
bool   marketIsDue(const char* market, time_t utc, int delayMin);
// True while a price alert on this market is worth checking: the regular
// session plus a short grace period after the close, on local weekdays.
// Crypto is always open.
bool   marketIsOpen(const char* market, time_t utc);
String marketLocalDate(const char* market, time_t utc);   // "YYYY-MM-DD"
