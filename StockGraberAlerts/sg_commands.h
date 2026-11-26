// Remote-command parsing -- mirrors notify.handle_command() on the Pi.
//
// Rules kept identical:
//   * an optional passphrase must prefix the command, matched case-insensitively
//     so phone auto-capitalisation doesn't lock you out;
//   * `add` / `remove` ALWAYS require that passphrase, even when queries are
//     ungated -- ntfy.sh topics are public, so an open topic stays read-only;
//   * a new symbol is verified against Yahoo before it is stored.
#pragma once

#include <Arduino.h>
#include <vector>
#include "sg_calc.h"
#include "sg_config.h"

// Fetch + analyse a list of symbols, in order. Symbols that fail the first
// pass are fetched once more after a pause, since Yahoo's dropped connections
// come in bursts. `failedFirst` / `missing` count symbols that failed the first
// pass and both passes. Feeds the watchdog throughout.
std::vector<Analysis> analyseAll(const std::vector<String>& symbols,
                                 double proximityPct,
                                 int& failedFirst, int& missing);

// Returns false when the message should be ignored entirely (bad/missing
// passphrase). Otherwise fills title/body with the reply to publish.
bool cmdHandle(const String& text, const Settings& s, String& title, String& body);

// True after a `factoryreset` has wiped the settings. The caller publishes the
// reply first -- the settings are still in RAM -- then restarts into setup.
bool cmdRestartPending();
