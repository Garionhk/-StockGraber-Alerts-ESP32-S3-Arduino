#include "sg_yahoo.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <time.h>

static const char* BASE = "https://query1.finance.yahoo.com/v8/finance/chart/";
static const uint32_t YAHOO_RETRY_MS = 2000;
static const char* UA =
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
  "(KHTML, like Gecko) Chrome/120.0 Safari/537.36";

// Put the JSON document in PSRAM so the 8 MB of SPIRAM absorbs the ~250-bar
// payload and the internal heap stays free for TLS buffers. Falls back to the
// normal heap if PSRAM is unavailable (wrong board setting / non-R8 module).
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
  }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
    return q ? q : realloc(p, n);
  }
};

// Percent-encode the characters that show up in Yahoo symbols (^ and =).
static String urlEncode(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '.' || c == '_') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

static String dateFromTs(long long ts) {
  time_t t = (time_t)ts;
  struct tm g;
  gmtime_r(&t, &g);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);
  return String(buf);
}

// One GET against the chart endpoint, read to completion into `payload`.
//
// Yahoo sends no Content-Length, so the response is delimited by connection
// close: parsing straight off the socket mistakes a momentary gap in the TCP
// stream for EOF and truncates the document (json: IncompleteInput).
// getString() keeps reading until the body is genuinely finished. At ~30 KB a
// report payload clears the PSRAM allocation threshold, so the buffer lands in
// SPIRAM and the internal heap stays free for TLS.
//
// A 429 means Yahoo is throttling. One retry after a short pause is usually
// enough when a long `status` or report briefly runs ahead of it.
static bool yahooGet(const String& url, String& payload, String& err) {
  for (int attempt = 0; attempt < 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();          // no CA bundle -- see README's limitations
    client.setTimeout(15000);

    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(15000);
    // Yahoo answers HTTP/1.1 with Transfer-Encoding: chunked, and getStream()
    // hands back the raw socket with the chunk framing still in it. HTTP/1.0 has
    // no chunked encoding, so the body arrives clean.
    http.useHTTP10(true);
    if (!http.begin(client, url)) {
      err = "http begin failed";
      return false;
    }
    http.addHeader("User-Agent", UA);
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code == 200) {
      payload = http.getString();
      http.end();
      return true;
    }
    http.end();
    // 401/403 would mean Yahoo started demanding a cookie/crumb; 429 = throttled
    err = "HTTP " + String(code);
    if (code != 429 || attempt) return false;
    Serial.println("[sg] yahoo: HTTP 429, retrying in 2 s");
    delay(YAHOO_RETRY_MS);
  }
  return false;
}

bool yahooFetch(const String& symbol,
                std::vector<double>& closes,
                std::vector<double>& highs,
                std::vector<double>& lows,
                String& lastDate,
                String& err) {
  closes.clear(); highs.clear(); lows.clear();
  String url = String(BASE) + urlEncode(symbol) + "?interval=1d&range=1y";

  String payload;
  if (!yahooGet(url, payload, err)) return false;

  // Keep only the four arrays we need; everything else is discarded as it
  // streams past, so the parsed document stays far smaller than the payload.
  PsramAllocator allocator;
  JsonDocument filter(&allocator);
  JsonObject res = filter["chart"]["result"][0].to<JsonObject>();
  res["timestamp"] = true;
  JsonObject q = res["indicators"]["quote"][0].to<JsonObject>();
  q["close"] = true;
  q["high"]  = true;
  q["low"]   = true;

  JsonDocument doc(&allocator);
  DeserializationError jerr = deserializeJson(
      doc, payload, DeserializationOption::Filter(filter));

  if (jerr) {
    err = String("json: ") + jerr.c_str();
    return false;
  }

  JsonArray result = doc["chart"]["result"].as<JsonArray>();
  if (result.isNull() || result.size() == 0) {
    err = "no data for " + symbol;
    return false;
  }
  JsonObject r0 = result[0];
  JsonArray stamps = r0["timestamp"].as<JsonArray>();
  JsonObject quote = r0["indicators"]["quote"][0];
  JsonArray cIn = quote["close"].as<JsonArray>();
  JsonArray hIn = quote["high"].as<JsonArray>();
  JsonArray lIn = quote["low"].as<JsonArray>();

  if (stamps.isNull() || cIn.isNull()) {
    err = "unexpected response shape";
    return false;
  }

  size_t n = stamps.size();
  closes.reserve(n); highs.reserve(n); lows.reserve(n);
  long long lastTs = 0;
  for (size_t i = 0; i < n && i < cIn.size() && i < hIn.size() && i < lIn.size(); i++) {
    if (cIn[i].isNull() || hIn[i].isNull() || lIn[i].isNull()) continue;
    closes.push_back(cIn[i].as<double>());
    highs.push_back(hIn[i].as<double>());
    lows.push_back(lIn[i].as<double>());
    lastTs = stamps[i].as<long long>();
  }

  if (closes.size() < 2 || lastTs == 0) {
    err = "not enough data for " + symbol;
    closes.clear(); highs.clear(); lows.clear();
    return false;
  }
  lastDate = dateFromTs(lastTs);
  return true;
}

bool yahooQuote(const String& symbol, Quote& q, String& err) {
  q = Quote();
  // range=1d is ~1.3 KB against ~30 KB for the report fetch, and its meta block
  // carries the live price, the session's high/low and when the session opened.
  String url = String(BASE) + urlEncode(symbol) + "?interval=1d&range=1d";

  String payload;
  if (!yahooGet(url, payload, err)) return false;

  PsramAllocator allocator;
  JsonDocument filter(&allocator);
  JsonObject m = filter["chart"]["result"][0]["meta"].to<JsonObject>();
  m["regularMarketPrice"]   = true;
  m["regularMarketDayHigh"] = true;
  m["regularMarketDayLow"]  = true;
  m["regularMarketTime"]    = true;
  m["currentTradingPeriod"]["regular"]["start"] = true;

  JsonDocument doc(&allocator);
  DeserializationError jerr = deserializeJson(
      doc, payload, DeserializationOption::Filter(filter));
  if (jerr) {
    err = String("json: ") + jerr.c_str();
    return false;
  }

  JsonObject meta = doc["chart"]["result"][0]["meta"];
  if (meta.isNull() || meta["regularMarketPrice"].isNull()) {
    err = "no data for " + symbol;
    return false;
  }
  q.price        = meta["regularMarketPrice"].as<double>();
  // Missing high/low (thin instruments, pre-open) degrade to the price itself.
  q.dayHigh      = meta["regularMarketDayHigh"] | q.price;
  q.dayLow       = meta["regularMarketDayLow"]  | q.price;
  q.marketTime   = meta["regularMarketTime"] | 0LL;
  q.sessionStart = meta["currentTradingPeriod"]["regular"]["start"] | 0LL;
  return q.price > 0;
}
