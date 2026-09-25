// ntfy publish + inbound polling.
//
// Publishing matches the Pi (notify.send_ntfy): a POST with Title/Priority/Tags
// headers, always stamped with NTFY_BOT_TAG so the node never answers itself.
//
// Inbound deliberately differs: the Pi holds a long-lived streaming connection,
// but on a microcontroller a multi-hour TLS session is the least reliable thing
// you can build. Here we poll /{topic}/json?poll=1&since=<id> instead -- short
// requests, dedup for free, and a dropped network just resumes next time.
#pragma once

#include <Arduino.h>
#include <vector>

#include "sg_config.h"

extern const char* NTFY_BOT_TAG;    // "sg_bot"

// Server, topic and login all come from `s`. A token is sent as Bearer, a
// username as Basic; with neither, the request is anonymous.
bool ntfyPublish(const Settings& s,
                 const String& title, const String& message,
                 int priority, const String& tags, String& err);

// Appends any new (non-self) message bodies to `out` and advances `since`.
// Returns false only on a transport error -- an empty poll is a success.
bool ntfyPoll(const Settings& s, String& since, std::vector<String>& out);
