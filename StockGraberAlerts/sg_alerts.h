// One-shot price alerts: "tell me when AAPL goes above 200".
//
// Each alert fires once and then deletes itself. They are checked every
// ALERT_CHECK_MS while the symbol's market is open (marketIsOpen), one quote
// per loop() pass so the web portal and command polling stay responsive.
//
// Checks are periodic, so a price could cross the target and come back between
// two of them. To catch that, a session that started after the alert was set is
// judged by its day high/low rather than the last price. A session already
// under way when the alert was set is judged by price alone -- its earlier
// high/low happened before anyone asked.
#pragma once

#include <Arduino.h>
#include <vector>

#include "sg_config.h"

struct Alert {
  String   symbol;           // Yahoo code, upper-case
  bool     above = true;     // true: fire at >= target; false: at <= target
  double   target = 0;
  uint32_t created = 0;      // epoch when set; 0 = clock was not synced yet
};

// Runtime cap, like WL_MAX: each alert on a distinct symbol is one quote fetch
// per check, and 50 every five minutes stays well clear of Yahoo's throttling.
static const size_t AL_MAX = 50;

std::vector<Alert> alAll();
bool alReplace(const std::vector<Alert>& list);   // false if NVS refused it
bool alAdd(const Alert& a);

// Removes the alerts on `symbol`, or only those at `target` when it is given
// (> 0). Returns how many went, or -1 if the write failed.
int  alRemove(const String& symbol, double target = 0);

// "AAPL above 200", "gold < 1900", "BTC-USD>60,000". Aliases resolve as they
// do for `add`. Returns false with a user-facing reason in `err`.
bool alParse(const String& text, Alert& out, String& err);

// Parses a target price, allowing "1,200" and "$200". 0 on failure.
double alParsePrice(const String& text);

String alFmtPrice(double v);          // grouped, with more decimals under 1
String alDescribe(const Alert& a);    // "AAPL above 200.00"

// Call every loop() while online.
void alertsTick(const Settings& s);
