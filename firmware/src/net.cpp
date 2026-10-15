#include "net.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <uri/UriBraces.h>

#include "actions.h"
#include "app.h"
#include "ledstrip.h"
#include "storage.h"

namespace net {

namespace {

const IPAddress kApIp(192, 168, 4, 1);

WebServer server(80);
DNSServer dns;

uint8_t* uploadBuf = nullptr;
size_t uploadLen = 0;
bool uploadOverflow = false;

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendOk() { sendJson(200, "{\"ok\":true}"); }

void sendError(int code, const char* msg) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  String s;
  serializeJson(d, s);
  sendJson(code, s);
}

bool parseBody(JsonDocument& doc) {
  if (!server.hasArg("plain")) return false;
  return deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok;
}

std::string iconIdArg() { return std::string(server.pathArg(0).c_str()); }

// ----------------------------------------------------------------- handlers

void handleGetConfig() {
  JsonDocument doc;
  {
    app::Lock l(app::cfgMtx);
    core::writeConfig(app::cfg, doc);
  }
  String s;
  serializeJson(doc, s);
  sendJson(200, s);
}

void handlePutConfig() {
  if (app::factoryResetRequested) return sendError(503, "factory reset in progress");
  if (server.clientContentLength() > 65536) return sendError(413, "config too large");
  JsonDocument doc;
  if (!parseBody(doc)) return sendError(400, "invalid JSON body");
  core::Config next;
  std::string err;
  if (!core::parseConfig(doc.as<JsonVariantConst>(), next, err)) return sendError(400, err.c_str());

  bool apChanged;
  uint32_t seq;
  core::Config toSave;
  {
    app::Lock l(app::cfgMtx);
    apChanged = next.settings.apSsid != app::cfg.settings.apSsid ||
                next.settings.apPass != app::cfg.settings.apPass;
    app::cfg = next;
    app::generation = app::generation + 1;
    toSave = app::cfg;
    seq = ++app::saveSeq;
  }
  if (!storage::save(toSave, seq)) return sendError(500, "failed to write config");
  sendOk();
  if (apChanged) app::restartApRequested = true;
}

void handleStatus() {
  JsonDocument d;
  d["fw"] = FW_VERSION;
  {
    app::Lock l(app::cfgMtx);
    d["activePage"] = app::cfg.settings.activePage;
    d["pageCount"] = app::cfg.pages.size();
  }
  d["heapFree"] = ESP.getFreeHeap();
  d["fsTotal"] = storage::totalBytes();
  d["fsUsed"] = storage::usedBytes();
  d["uptimeMs"] = millis();
  d["ledCount"] = core::kLedCount;
  String s;
  serializeJson(d, s);
  sendJson(200, s);
}

void handlePage() {
  JsonDocument d;
  if (!parseBody(d) || !d["index"].is<int>()) return sendError(400, "need {index}");
  app::requestedPage = (uint32_t)d["index"].as<int>();
  sendOk();
}

void handleTest() {
  JsonDocument d;
  if (!parseBody(d)) return sendError(400, "invalid JSON body");
  core::Steps steps;
  std::string err;
  if (!core::parseSteps(d["steps"], steps, err)) return sendError(400, err.c_str());
  actions::enqueue(actions::kTestRunner, steps, true);
  sendOk();
}

void handleProbe() {
  JsonDocument d;
  if (!parseBody(d) || !d["mask"].is<int>()) return sendError(400, "need {mask}");
  ledstrip::probe((uint16_t)d["mask"].as<int>());
  sendOk();
}

void handleFactoryReset() {
  sendOk();
  app::factoryResetRequested = true;
}

void handleIconGet() {
  std::string id = iconIdArg();
  if (!core::validIconId(id) || !storage::iconExists(id)) return sendError(404, "no such icon");
  File f = LittleFS.open(("/icons/" + id + ".565").c_str(), "r");
  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleIconUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadLen = 0;
    uploadOverflow = false;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadLen + up.currentSize > storage::kIconBytes) {
      uploadOverflow = true;
    } else if (uploadBuf) {
      memcpy(uploadBuf + uploadLen, up.buf, up.currentSize);
      uploadLen += up.currentSize;
    }
  }
}

void handleIconPostDone() {
  // Always forget the upload afterwards so a later POST without a file can never reuse stale bytes.
  struct Reset { ~Reset() { uploadLen = 0; uploadOverflow = false; } } reset;
  if (app::factoryResetRequested) return sendError(503, "factory reset in progress");
  std::string id = iconIdArg();
  if (!core::validIconId(id)) return sendError(400, "bad icon id");
  if (uploadOverflow || uploadLen != storage::kIconBytes) return sendError(400, "icon must be exactly 32768 bytes");
  if (!storage::writeIcon(id, uploadBuf)) return sendError(500, "failed to store icon (flash full?)");

  core::Config toSave;
  uint32_t seq = 0;
  bool changed = false;
  {
    app::Lock l(app::cfgMtx);
    bool known = false;
    for (auto& i : app::cfg.icons) known |= i.id == id;
    if (!known) {
      app::cfg.icons.push_back({id, id});
      changed = true;
      toSave = app::cfg;
      seq = ++app::saveSeq;
    }
    app::redrawGeneration = app::redrawGeneration + 1;
  }
  if (changed) storage::save(toSave, seq);
  sendOk();
}

void handleIconDelete() {
  std::string id = iconIdArg();
  if (!core::validIconId(id)) return sendError(400, "bad icon id");
  storage::deleteIcon(id);
  core::Config toSave;
  uint32_t seq;
  {
    app::Lock l(app::cfgMtx);
    auto& v = app::cfg.icons;
    for (size_t i = 0; i < v.size(); ++i)
      if (v[i].id == id) { v.erase(v.begin() + (long)i); break; }
    app::redrawGeneration = app::redrawGeneration + 1;
    toSave = app::cfg;
    seq = ++app::saveSeq;
  }
  storage::save(toSave, seq);
  sendOk();
}

void handleIndex() {
  File f = LittleFS.open("/www/index.html", "r");
  if (!f) {
    server.send(200, "text/plain", "ClaudePad is running, but the web UI is missing. Run: pio run -t uploadfs");
    return;
  }
  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(f, "text/html");
  f.close();
}

void handleNotFound() {
  // Captive portal: send every unknown host/path to the UI.
  server.sendHeader("Location", String("http://") + kApIp.toString() + "/");
  server.send(302, "text/plain", "");
}

void serverTask(void*) {
  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    if (app::restartApRequested) {
      app::restartApRequested = false;
      delay(300);   // let the HTTP response flush
      restartAp();
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

bool apStarted = false;
int apAttempts = 0;

void startAp() {
  std::string ssid, pass;
  {
    app::Lock l(app::cfgMtx);
    ssid = app::cfg.settings.apSsid;
    pass = app::cfg.settings.apPass;
  }
  ++apAttempts;
  bool modeOk = WiFi.mode(WIFI_AP);
  bool cfgOk = WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  apStarted = WiFi.softAP(ssid.c_str(), pass.c_str()) && modeOk && cfgOk;
  dns.start(53, "*", kApIp);
}

}  // namespace

void restartAp() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  delay(100);
  startAp();
}

String ipString() { return kApIp.toString(); }

String debugInfo() {
  String s = String("wifi mode=") + (int)WiFi.getMode() + " apStarted=" + (apStarted ? 1 : 0) +
             " attempts=" + apAttempts + " ssid=" + WiFi.softAPSSID() + " ip=" + WiFi.softAPIP().toString() +
             " stations=" + WiFi.softAPgetStationNum() + " heap=" + ESP.getFreeHeap();
  return s;
}

void begin() {
  uploadBuf = (uint8_t*)malloc(storage::kIconBytes);
  startAp();

  server.on("/", HTTP_GET, handleIndex);
  server.on("/index.html", HTTP_GET, handleIndex);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_PUT, handlePutConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/page", HTTP_POST, handlePage);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/leds/probe", HTTP_POST, handleProbe);
  server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
  server.on(UriBraces("/api/icons/{}"), HTTP_GET, handleIconGet);
  server.on(UriBraces("/api/icons/{}"), HTTP_POST, handleIconPostDone, handleIconUpload);
  server.on(UriBraces("/api/icons/{}"), HTTP_DELETE, handleIconDelete);
  server.serveStatic("/", LittleFS, "/www/", "max-age=300");
  server.onNotFound(handleNotFound);
  server.begin();

  xTaskCreatePinnedToCore(serverTask, "web", 8192, nullptr, 1, nullptr, 0);
}

}  // namespace net
