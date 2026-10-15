#pragma once
#include <Arduino.h>

// Wi-Fi access point, captive portal and the REST API described in docs/CONFIG.md.
namespace net {

void begin();            // start AP + web server task (config must be loaded)
void restartAp();        // re-apply SSID/password from the current config
String ipString();
String debugInfo();       // one-paragraph Wi-Fi/AP state for the serial console

}  // namespace net
