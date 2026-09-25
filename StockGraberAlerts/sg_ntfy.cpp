#include "sg_ntfy.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>

const char* NTFY_BOT_TAG = "sg_bot";

static String stripTrailingSlash(const String& s) {
  String out = s;
  while (out.endsWith("/")) out = out.substring(0, out.length() - 1);
  return out;
}

// "Bearer tk_..." or "Basic base64(user:password)", or empty for an open
// server. This core's libb64 does not wrap lines, so the result is header-safe.
static String authHeader(const Settings& s) {
  if (s.ntfyToken.length()) return "Bearer " + s.ntfyToken;
  if (s.ntfyUser.length())
    return "Basic " + base64::encode(s.ntfyUser + ":" + s.ntfyPass);
  return "";
}

static bool isAuthError(int code) { return code == 401 || code == 403; }

static const char* AUTH_HINT =
    " -- the server rejected the login; check the ntfy username, password or "
    "token on the settings page, and this user's access to the topic";

bool ntfyPublish(const Settings& s,
                 const String& title, const String& message,
                 int priority, const String& tags, String& err) {
  String host = stripTrailingSlash(s.ntfyServer.length() ? s.ntfyServer
                                                         : "https://ntfy.sh");
  String t = s.ntfyTopic;
  t.trim();
  if (!t.length()) {
    err = "no ntfy topic configured";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(15000);
  if (!http.begin(client, host + "/" + t)) {
    err = "http begin failed";
    return false;
  }
  http.addHeader("Title", title);
  http.addHeader("Priority", String(priority));
  if (tags.length()) http.addHeader("Tags", tags);
  String auth = authHeader(s);
  if (auth.length()) http.addHeader("Authorization", auth);

  int code = http.POST((uint8_t*)message.c_str(), message.length());
  http.end();

  if (code >= 200 && code < 300) return true;
  err = "HTTP " + String(code);
  if (isAuthError(code)) err += AUTH_HINT;
  return false;
}

bool ntfyPoll(const Settings& s, String& since, std::vector<String>& out) {
  String host = stripTrailingSlash(s.ntfyServer.length() ? s.ntfyServer
                                                         : "https://ntfy.sh");
  String t = s.ntfyTopic;
  t.trim();
  if (!t.length()) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(15000);
  String url = host + "/" + t + "/json?poll=1&since=" + since;
  if (!http.begin(client, url)) return false;
  String auth = authHeader(s);
  if (auth.length()) http.addHeader("Authorization", auth);

  // Polls repeat every few seconds, so a failure is logged once per change of
  // status rather than on every attempt.
  static int lastCode = 200;
  int code = http.GET();
  if (code != lastCode) {
    if (code != 200)
      Serial.println("[sg] ntfy poll: HTTP " + String(code) +
                     (isAuthError(code) ? AUTH_HINT : ""));
    lastCode = code;
  }
  if (code != 200) {
    http.end();
    return false;
  }

  // Newline-delimited JSON: one object per line.
  Stream& stream = http.getStream();
  while (stream.available() || http.connected()) {
    String line = stream.readStringUntil('\n');
    line.trim();
    if (!line.length()) {
      if (!stream.available() && !http.connected()) break;
      continue;
    }
    JsonDocument msg;
    if (deserializeJson(msg, line)) continue;          // skip malformed lines
    if (String((const char*)(msg["event"] | "")) != "message") continue;

    const char* id = msg["id"] | "";
    if (strlen(id)) since = String(id);                // resume point

    bool mine = false;
    JsonArray tags = msg["tags"].as<JsonArray>();
    if (!tags.isNull())
      for (JsonVariant v : tags)
        if (String((const char*)(v | "")) == NTFY_BOT_TAG) { mine = true; break; }
    if (mine) continue;                                // never echo ourselves

    out.push_back(String((const char*)(msg["message"] | "")));
  }
  http.end();
  return true;
}

// ---- NtfyChunker ---------------------------------------------------------

NtfyChunker::NtfyChunker(const Settings& s, const String& title,
                         int priority, const String& tags,
                         int urgentPriority, const String& urgentTags)
    : s_(s), title_(title), tags_(tags), urgentTags_(urgentTags),
      priority_(priority), urgentPriority_(urgentPriority) {
  body_.reserve(PART_MAX + 128);
}

void NtfyChunker::addLine(const String& line) {
  if (body_.length() && body_.length() + 1 + line.length() > PART_MAX) {
    publishPart(partTitle(false));
    body_ = "";
    urgent_ = false;
  }
  if (body_.length()) body_ += "\n";
  body_ += line;
}

String NtfyChunker::partTitle(bool last) const {
  int n = sent_ + 1;
  if (!last) return title_ + " (" + String(n) + ")";
  if (sent_ == 0) return title_;
  return title_ + " (" + String(n) + "/" + String(n) + ")";
}

bool NtfyChunker::publishPart(const String& title) {
  String err;
  bool ok = ntfyPublish(s_, title, body_, urgent_ ? urgentPriority_ : priority_,
                        urgent_ ? urgentTags_ : tags_, err);
  if (!ok) {
    ok_ = false;
    err_ = err;
    Serial.println("[sg] ntfy part " + String(sent_ + 1) + " failed: " + err);
  }
  sent_++;
  return ok;
}

void NtfyChunker::takeLast(String& title, String& body, int& priority,
                           String& tags) {
  title = partTitle(true);
  body = body_;
  priority = urgent_ ? urgentPriority_ : priority_;
  tags = urgent_ ? urgentTags_ : tags_;
}

bool NtfyChunker::finish(String& err) {
  if (body_.length()) publishPart(partTitle(true));
  if (!ok_) err = err_;
  return ok_;
}
