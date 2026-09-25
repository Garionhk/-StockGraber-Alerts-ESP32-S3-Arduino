// Daily OHLC from Yahoo Finance -- the stand-in for `yfinance`.
//
// One call to the v8 chart endpoint gives everything the report needs:
//   https://query1.finance.yahoo.com/v8/finance/chart/AAPL?interval=1d&range=1y
// range=1y (~252 trading days) is exactly the window required: 200 bars for the
// slow SMA (+2 for cross detection) and 252 for the 52-week high/low.
#pragma once

#include <Arduino.h>
#include <vector>

// Returns true on success. On failure `err` explains why and the vectors are
// left empty. Bars with a null close/high/low are dropped, keeping all three
// series index-aligned.
bool yahooFetch(const String& symbol,
                std::vector<double>& closes,
                std::vector<double>& highs,
                std::vector<double>& lows,
                String& lastDate,
                String& err);

// Latest quote for price alerts, from the same endpoint at range=1d.
struct Quote {
  double    price = 0;
  double    dayHigh = 0;       // regular session so far
  double    dayLow = 0;
  long long marketTime = 0;    // epoch of the last trade
  long long sessionStart = 0;  // epoch the current/last regular session opened
};

bool yahooQuote(const String& symbol, Quote& q, String& err);
