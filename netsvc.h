// netsvc.h -- Wi-Fi hotspot + physio web page + ThingSpeak store-and-forward.
//  * The brace always runs its own hotspot (RehabSense-XXXX). A phone joins
//    it and opens 192.168.4.1 (captive portal pops up automatically).
//  * If a home Wi-Fi is configured, sessions are synced to ThingSpeak in the
//    background, one per ~16 s (free-tier limit), with the original timestamp.
#pragma once
#include <Arduino.h>

namespace net {

void begin();
void loop();              // call every loop(): DNS + web requests + Wi-Fi retry
void applyWifiConfig();   // re-read SSID/password from config and reconnect

String apSsid();
String apIp();
bool staConnected();
String staIp();
String uploadStatus();    // human-readable last sync result
uint32_t uploadedCount();

}  // namespace net
