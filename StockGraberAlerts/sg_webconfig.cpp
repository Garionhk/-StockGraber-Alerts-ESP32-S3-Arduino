#include "sg_webconfig.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <time.h>

#include "sg_alerts.h"
#include "sg_calc.h"
#include "sg_config.h"

namespace webconfig {
namespace {

WebServer server(80);
DNSServer dns;

bool apMode     = false;
bool authNeeded = true;
bool running    = false;
bool savedFlag  = false;

const byte DNS_PORT = 53;

String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length());
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;
    }
  }
  return out;
}

const char PAGE_HEAD[] PROGMEM =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>StockGraber setup</title><style>"
    "body{font-family:system-ui,sans-serif;margin:0;padding:1rem;max-width:34rem;"
    "background:#111;color:#eee}"
    "h1{font-size:1.2rem}h2{font-size:.95rem;margin:1.4rem 0 .4rem;color:#8bd}"
    "label{display:block;margin:.6rem 0 .15rem;font-size:.85rem;color:#bbb}"
    "input[type=text],input[type=password],input[type=number],textarea{width:100%;"
    "box-sizing:border-box;padding:.5rem;border:1px solid #444;border-radius:4px;"
    "background:#1c1c1c;color:#eee;font-size:1rem;font-family:inherit}"
    "small{color:#888;font-size:.75rem}"
    "button{margin-top:1.2rem;width:100%;padding:.7rem;font-size:1rem;border:0;"
    "border-radius:4px;background:#2a7;color:#022;font-weight:600}"
    ".row{display:flex;gap:.6rem}.row>div{flex:1}"
    ".err{background:#833;padding:.5rem;border-radius:4px;margin-bottom:.8rem}"
    "</style>";

// Returns false and sends the challenge if the request should be rejected.
bool authorized() {
  if (!authNeeded) return true;

  // A board provisioned by the old compile-time flow has no admin password.
  // Leaving the page open would let anyone on the LAN (or anyone in range of
  // the fallback AP) repoint it, so it stays shut until one is set -- which
  // needs the BOOT button, i.e. physical access.
  if (g_settings.adminPass.isEmpty()) {
    String p = FPSTR(PAGE_HEAD);
    p += "<h1>Settings locked</h1><p>No admin password is set yet. Press "
         "<b>EN</b> (reset) on the board, then immediately press and hold "
         "<b>BOOT</b> for two seconds. It opens the <b>" + htmlEscape(apSsid()) +
         "</b> network, where you can set one.</p>";
    server.send(403, "text/html", p);
    return false;
  }

  if (server.authenticate(ADMIN_USER, g_settings.adminPass.c_str())) return true;
  server.requestAuthentication();
  return false;
}

void handleScan() {
  if (!authorized()) return;

  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n && i < 30; i++) {
    if (i) j += ",";
    j += "\"" + htmlEscape(WiFi.SSID(i)) + "\"";
  }
  WiFi.scanDelete();
  server.send(200, "application/json", j + "]");
}

String checkbox(const char* name, bool on) {
  return String("<input type=checkbox name=") + name + (on ? " checked" : "") + ">";
}

void sendForm(const String& error) {
  const Settings& s = g_settings;

  String watch;
  for (const String& sym : wlAll()) {
    if (watch.length()) watch += ", ";
    watch += sym;
  }

  String p = FPSTR(PAGE_HEAD);
  p.reserve(10240);   // 200 symbols + 50 alerts fit without regrowing
  p += "<h1>StockGraber setup</h1>";
  if (!error.isEmpty()) p += "<div class=err>" + htmlEscape(error) + "</div>";
  p += "<form method=POST action=/save>";

  p += "<h2>Wi-Fi</h2>";
  p += "<label>Network (2.4 GHz only)</label>";
  p += "<input type=text name=ssid list=nets value='" + htmlEscape(s.wifiSsid) +
       "' required><datalist id=nets></datalist>";
  p += "<label>Password</label><input type=password name=wpass>";
  p += "<small>Leave blank to keep the stored password.</small>";

  p += "<h2>ntfy</h2>";
  p += "<label>Server</label><input type=text name=server value='" +
       htmlEscape(s.ntfyServer) + "' required>";
  p += "<small>https://ntfy.sh, or your own, e.g. http://192.168.1.5:8080</small>";
  p += "<label>Topic</label><input type=text name=topic value='" +
       htmlEscape(s.ntfyTopic) + "' required>";
  p += "<small>Give this node its <b>own</b> topic -- not the Pi's. Reports, "
       "commands and replies share it. Make it long and unguessable.</small>";

  p += "<h2>ntfy login <small>(blank for an open server)</small></h2>";
  p += "<label>Username</label><input type=text name=nuser value='" +
       htmlEscape(s.ntfyUser) + "'>";
  p += "<label>Password</label><input type=password name=npass>";
  p += "<label>or Token</label><input type=password name=token>";
  p += "<small>A token wins over username and password. Blank keeps stored "
       "values; tick below to remove them.</small>";
  p += "<label>" + checkbox("nclear", false) + " clear stored ntfy login</label>";

  p += "<h2>Passphrases</h2>";
  p += "<label>Command passphrase</label><input type=password name=secret>";
  p += "<small>Prefix for every command sent from the ntfy app "
       "(<i>passphrase status</i>). Required for add / remove / factoryreset.</small>";
  p += "<label>Admin password (this page)</label><input type=password name=admpass>";
  p += "<small>Login is <b>admin</b>. Keep it different from the command "
       "passphrase. Blank keeps the stored ones.</small>";

  p += "<h2>Reports &amp; commands</h2>";
  p += "<label>" + checkbox("rep_on", s.reportEnabled) + " daily market reports</label>";
  p += "<label>" + checkbox("cmd_on", s.cmdEnabled) + " answer commands</label>";
  p += "<div class=row><div><label>Report delay (min)</label>"
       "<input type=number name=delay min=15 max=30 value='" +
       String(s.reportDelayMin) + "' required></div>";
  p += "<div><label>52w proximity (%)</label>"
       "<input type=number name=prox min=0.1 max=50 step=0.1 value='" +
       String(s.proximityPct, 1) + "' required></div>";
  p += "<div><label>Poll every (s)</label>"
       "<input type=number name=poll min=5 max=3600 value='" +
       String(s.pollSeconds) + "' required></div></div>";

  p += "<h2>Watchlist</h2>";
  p += "<textarea name=watch rows=5 placeholder='AAPL, NVDA, GC=F, BTC-USD'>" +
       htmlEscape(watch) + "</textarea>";
  p += "<small>Yahoo codes separated by commas or spaces, up to " +
       String((int)WL_MAX) + ". Gold is GC=F, bitcoin BTC-USD, Berkshire B "
       "BRK-B. Unlike <i>add</i> over ntfy, codes here are not checked against "
       "Yahoo.</small>";

  String alerts;
  for (const Alert& a : alAll()) {
    alerts += a.symbol + (a.above ? " > " : " < ") + alFmtPrice(a.target) + "\n";
  }
  p += "<h2>Price alerts</h2>";
  p += "<textarea name=alerts rows=4 placeholder='AAPL > 200\nBTC-USD < 60000'>" +
       htmlEscape(alerts) + "</textarea>";
  p += "<small>One per line, <i>CODE &gt; price</i> or <i>CODE &lt; price</i>, up "
       "to " + String((int)AL_MAX) + ". Each fires once, then is removed. Checked "
       "every 5 minutes while the market is open. Unlike <i>alert</i> over ntfy, "
       "these are not checked against Yahoo, and one already past its price "
       "fires on the first check.</small>";

  p += "<button type=submit>Save &amp; restart</button></form>";

  // Populates the network picker. Typing a network name by hand is an easy
  // way to get a one-character typo into flash.
  p += "<script>fetch('/scan').then(r=>r.json()).then(a=>{"
       "document.getElementById('nets').innerHTML="
       "a.map(s=>'<option value=\"'+s+'\">').join('')}).catch(()=>{});</script>";

  server.send(200, "text/html", p);
}

void handleRoot() {
  if (!authorized()) return;
  sendForm("");
}

// ntfy topics are [-_A-Za-z0-9]{1,64}; anything else would also break the URL.
bool validTopic(const String& t) {
  if (t.isEmpty() || t.length() > 64) return false;
  for (unsigned int i = 0; i < t.length(); i++) {
    char c = t[i];
    if (!isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  }
  return true;
}

bool validSymbol(const String& sym) {
  if (sym.isEmpty() || sym.length() > 20) return false;
  for (unsigned int i = 0; i < sym.length(); i++) {
    char c = sym[i];
    if (!isalnum((unsigned char)c) && !strchr(".-=^", c)) return false;
  }
  return true;
}

// Splits on commas and whitespace, resolves aliases ("gold" -> GC=F) and drops
// duplicates. Returns an error message, or "" on success.
String parseWatchlist(const String& raw, std::vector<String>& out) {
  String tok;
  for (unsigned int i = 0; i <= raw.length(); i++) {
    char c = i < raw.length() ? raw[i] : ',';
    if (c != ',' && c != ';' && !isspace((unsigned char)c)) {
      tok += c;
      continue;
    }
    if (tok.isEmpty()) continue;
    String sym = calcResolveSymbol(tok);
    tok = "";
    if (!validSymbol(sym)) return "\"" + sym + "\" is not a valid symbol.";
    bool dup = false;
    for (const String& have : out) dup = dup || have == sym;
    if (!dup) out.push_back(sym);
  }
  if (out.size() > WL_MAX)
    return "The watchlist holds at most " + String((int)WL_MAX) + " symbols.";
  return "";
}

// One alert per line (or ';'-separated). Lines already stored keep their
// original set time, so saving the page doesn't change how they are judged.
String parseAlerts(const String& raw, std::vector<Alert>& out) {
  std::vector<Alert> have = alAll();
  time_t now = time(nullptr);
  uint32_t stamp = now > 1600000000 ? (uint32_t)now : 0;

  int start = 0;
  while (start <= (int)raw.length()) {
    int end = start;
    while (end < (int)raw.length() && raw[end] != '\n' && raw[end] != ';') end++;
    String line = raw.substring(start, end);
    start = end + 1;
    line.trim();
    if (line.isEmpty()) continue;

    Alert a;
    String err;
    if (!alParse(line, a, err)) return "Price alert \"" + line + "\": " + err;
    a.created = stamp;
    for (const Alert& h : have)
      if (h.symbol == a.symbol && h.above == a.above &&
          alFmtPrice(h.target) == alFmtPrice(a.target))
        a.created = h.created;

    bool dup = false;
    for (const Alert& o : out)
      dup = dup || (o.symbol == a.symbol && o.above == a.above &&
                    alFmtPrice(o.target) == alFmtPrice(a.target));
    if (!dup) out.push_back(a);
  }
  if (out.size() > AL_MAX)
    return "At most " + String((int)AL_MAX) + " price alerts.";
  return "";
}

void handleSave() {
  if (!authorized()) return;

  String ssid   = server.arg("ssid");
  String srv    = server.arg("server");
  String topic  = server.arg("topic");
  String nuser  = server.arg("nuser");
  long   delayM = server.arg("delay").toInt();
  float  prox   = server.arg("prox").toFloat();
  long   poll   = server.arg("poll").toInt();

  ssid.trim(); srv.trim(); topic.trim(); nuser.trim();
  while (srv.endsWith("/")) srv.remove(srv.length() - 1);

  if (ssid.isEmpty())             { sendForm("Wi-Fi network is required."); return; }
  if (!srv.startsWith("http://") && !srv.startsWith("https://")) {
    sendForm("ntfy server must start with http:// or https://");
    return;
  }
  if (!validTopic(topic)) {
    sendForm("ntfy topic must be 1-64 letters, digits, - or _.");
    return;
  }
  if (delayM < 15 || delayM > 30) { sendForm("Report delay must be 15-30 minutes."); return; }
  if (prox <= 0 || prox > 50)     { sendForm("Proximity must be above 0 and at most 50%."); return; }
  if (poll < 5 || poll > 3600)    { sendForm("Poll interval must be 5-3600 seconds."); return; }

  std::vector<String> watch;
  String werr = parseWatchlist(server.arg("watch"), watch);
  if (!werr.isEmpty()) { sendForm(werr); return; }

  std::vector<Alert> alerts;
  String aerr = parseAlerts(server.arg("alerts"), alerts);
  if (!aerr.isEmpty()) { sendForm(aerr); return; }

  // Blank password fields mean "keep what is stored" -- the form never renders
  // a stored password back to the browser, so blank is the normal state.
  String wpass   = server.arg("wpass");
  String npass   = server.arg("npass");
  String token   = server.arg("token");
  String secret  = server.arg("secret");
  String admpass = server.arg("admpass");
  secret.trim();

  // Work on a copy so a rejected form leaves the running config untouched.
  Settings s = g_settings;

  if (!wpass.isEmpty())   s.wifiPassword = wpass;
  if (!secret.isEmpty())  s.cmdSecret    = secret;
  if (!admpass.isEmpty()) s.adminPass    = admpass;

  // On a first-time setup there is nothing to keep, so these must be supplied.
  if (!g_settings.provisioned && wpass.isEmpty()) {
    sendForm("Wi-Fi password is required.");
    return;
  }
  if (s.cmdSecret.isEmpty()) { sendForm("Command passphrase is required."); return; }
  if (s.adminPass.isEmpty()) { sendForm("Admin password is required."); return; }
  // Commands match the passphrase case-insensitively, so compare that way too.
  if (s.cmdSecret.equalsIgnoreCase(s.adminPass)) {
    sendForm("Use a different admin password from the command passphrase.");
    return;
  }

  s.wifiSsid       = ssid;
  s.ntfyServer     = srv;
  s.ntfyTopic      = topic;
  s.ntfyUser       = nuser;
  if (server.hasArg("nclear")) {
    s.ntfyUser = "";
    s.ntfyPass = "";
    s.ntfyToken = "";
  }
  if (!npass.isEmpty()) s.ntfyPass  = npass;
  if (!token.isEmpty()) s.ntfyToken = token;
  s.reportEnabled  = server.hasArg("rep_on");
  s.cmdEnabled     = server.hasArg("cmd_on");
  s.reportDelayMin = (int)delayM;
  s.proximityPct   = prox;
  s.pollSeconds    = (int)poll;

  if (!wlReplace(watch)) { sendForm("Could not write the watchlist to flash."); return; }
  if (!alReplace(alerts)) { sendForm("Could not write the price alerts to flash."); return; }
  if (!cfgSave(s))       { sendForm("Could not write settings to flash."); return; }
  cfgSetCurrent(s);
  savedFlag = true;

  String p = FPSTR(PAGE_HEAD);
  p += "<h1>Saved</h1><p>Restarting. The node will join <b>" +
       htmlEscape(ssid) + "</b> and announce its address on your ntfy topic.</p>";
  server.send(200, "text/html", p);

  delay(1500);       // let the response actually reach the browser
  ESP.restart();
}

// Captive-portal probes (/generate_204, /hotspot-detect.html, /ncsi.txt ...)
// all land here. Answering them with a redirect is what makes phones pop the
// setup page on their own. AP mode only -- doing this in STA would hijack
// ordinary name resolution on the LAN.
void handleNotFound() {
  if (!apMode) { handleRoot(); return; }
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

// Once only: the node can move between AP and STA several times per boot, and
// WebServer::on() appends rather than replaces.
void routes() {
  static bool done = false;
  if (done) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  done = true;
}

}  // namespace

void beginAP(bool requireAuth) {
  stop();
  apMode     = true;
  authNeeded = requireAuth;

  // AP_STA rather than AP: in the fallback case the station side keeps
  // retrying in the background, so a router coming back brings the node
  // online without a reboot.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid().c_str());
  delay(100);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(DNS_PORT, "*", WiFi.softAPIP());

  routes();
  server.begin();
  running = true;

  Serial.printf("[sg] web: AP \"%s\" up, portal at http://%s (auth %s)\n",
                apSsid().c_str(), WiFi.softAPIP().toString().c_str(),
                requireAuth ? "on" : "off");
}

void beginSTA() {
  stop();
  apMode     = false;
  authNeeded = true;

  routes();
  server.begin();
  running = true;

  Serial.printf("[sg] web: settings page at http://%s\n",
                WiFi.localIP().toString().c_str());
}

void stop() {
  if (!running) return;
  server.stop();
  if (apMode) dns.stop();
  running = false;
}

void tick() {
  if (!running) return;
  if (apMode) dns.processNextRequest();
  server.handleClient();
}

bool isAP() { return apMode; }

String apSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), "StockGraber-Setup-%02X%02X", mac[4], mac[5]);
  return String(buf);
}

String ip() {
  return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

String settingsUrl() { return "http://" + ip(); }

bool saved() { return savedFlag; }

}  // namespace webconfig
