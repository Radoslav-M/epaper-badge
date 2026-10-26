#pragma once
// WiFi manager for ESP32 (Arduino framework) - logic only, no HTML.
//  - Always runs a config AP (default http://192.168.4.1)
//  - Stores up to MAX_NETS networks in NVS flash (Preferences)
//  - At boot (and periodically if disconnected) scans and connects to the
//    strongest saved network that is in range
//  - Owns the shared WebServer; WebUI.h registers its routes on it
//
// Usage:
//   WifiMgr::begin("EPaper-Setup", "changeme123");
//   WebUI::begin();            // register routes
//   WifiMgr::startServer();    // start server + background task

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

namespace WifiMgr {

constexpr int MAX_NETS = 10;
constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t RETRY_INTERVAL_MS = 30000;

static Preferences prefs;
static WebServer server(80);
static volatile bool retryNow = true;  // try to connect right after boot

// ---------- storage ----------
inline int count() { return prefs.getInt("n", 0); }
inline String getSsid(int i) { return prefs.getString(("s" + String(i)).c_str(), ""); }
inline String getPass(int i) { return prefs.getString(("p" + String(i)).c_str(), ""); }

inline void setNet(int i, const String &ssid, const String &pass) {
  prefs.putString(("s" + String(i)).c_str(), ssid);
  prefs.putString(("p" + String(i)).c_str(), pass);
}

// returns false if the list is full
inline bool addNet(const String &ssid, const String &pass) {
  int n = count();
  for (int i = 0; i < n; i++) {  // already saved? just update the password
    if (getSsid(i) == ssid) {
      setNet(i, ssid, pass);
      return true;
    }
  }
  if (n >= MAX_NETS) return false;
  setNet(n, ssid, pass);
  prefs.putInt("n", n + 1);
  return true;
}

inline void removeNet(int idx) {
  int n = count();
  if (idx < 0 || idx >= n) return;
  for (int i = idx; i < n - 1; i++) setNet(i, getSsid(i + 1), getPass(i + 1));
  prefs.remove(("s" + String(n - 1)).c_str());
  prefs.remove(("p" + String(n - 1)).c_str());
  prefs.putInt("n", n - 1);
}

// ---------- connecting ----------
inline bool connectToKnown() {
  int n = count();
  if (n == 0) return false;

  int found = WiFi.scanNetworks();
  int best = -1, bestRssi = -999;
  for (int i = 0; i < found; i++) {
    String s = WiFi.SSID(i);
    for (int j = 0; j < n; j++) {
      if (s == getSsid(j) && WiFi.RSSI(i) > bestRssi) {
        best = j;
        bestRssi = WiFi.RSSI(i);
      }
    }
  }
  WiFi.scanDelete();
  if (best < 0) return false;

  Serial.printf("[wifi] connecting to %s\n", getSsid(best).c_str());
  WiFi.begin(getSsid(best).c_str(), getPass(best).c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < CONNECT_TIMEOUT_MS) delay(100);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  WiFi.disconnect();
  return false;
}

// ---------- background task ----------
inline void task(void *) {
  uint32_t lastTry = 0;
  for (;;) {
    server.handleClient();
    if (WiFi.status() != WL_CONNECTED && (retryNow || millis() - lastTry > RETRY_INTERVAL_MS)) {
      retryNow = false;
      connectToKnown();
      lastTry = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

inline void begin(const char *apSsid, const char *apPass) {
  prefs.begin("wifi", false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, apPass);  // pass must be >= 8 chars (or nullptr for open)
  Serial.printf("[wifi] AP '%s' at %s\n", apSsid, WiFi.softAPIP().toString().c_str());
}

inline void startServer() {
  server.begin();
  xTaskCreatePinnedToCore(task, "wifiMgr", 10240, nullptr, 1, nullptr, 0);
}

}  // namespace WifiMgr