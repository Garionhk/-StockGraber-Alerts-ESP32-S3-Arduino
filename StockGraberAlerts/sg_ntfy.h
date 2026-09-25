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

// Builds a line-oriented message and splits it across several publishes when
// it would outgrow ntfy's 4 KB body limit (ntfy.sh answers 413 rather than
// truncating). Full parts go out as soon as they fill, so a long `status`
// shows progress instead of arriving all at once ten minutes later.
//
// Titles are left alone when everything fits in one message; otherwise parts
// read "<title> (1)", "<title> (2)" ... and the last "<title> (3/3)".
//
// A part holding anything passed to markUrgent() goes out at the urgent
// priority and tags -- a report flags only the part with the golden cross.
class NtfyChunker {
 public:
  static const size_t PART_MAX = 3800;   // headroom under ntfy's 4096 bytes

  NtfyChunker(const Settings& s, const String& title,
              int priority, const String& tags,
              int urgentPriority, const String& urgentTags);

  void addLine(const String& line);
  void markUrgent() { urgent_ = true; }

  // Hands back the final part instead of publishing it, for callers (the
  // command path) that publish their own reply.
  void takeLast(String& title, String& body, int& priority, String& tags);

  // Publishes the final part. True only if every part was accepted.
  bool finish(String& err);

  int parts() const { return sent_; }

 private:
  bool publishPart(const String& title);
  String partTitle(bool last) const;

  const Settings& s_;
  String title_, tags_, urgentTags_, body_, err_;
  int priority_, urgentPriority_;
  bool urgent_ = false;
  bool ok_ = true;
  int sent_ = 0;
};
