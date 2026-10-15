#pragma once
#include <Arduino.h>

// Wi-Fi access point, captive portal and the REST API described in docs/CONFIG.md.
namespace net {

void begin(bool startWifi);   // start the web task; the access point starts now only if startWifi
void restartAp();        // re-apply SSID/password from the current config
String ipString();
void requestWifi(bool on);   // switch the access point on/off (applied by the web task)
bool wifiOn();             // the access point is currently running
int clientCount();         // stations connected right now (0 when off)
String debugInfo();       // one-paragraph Wi-Fi/AP state for the serial console

}  // namespace net
