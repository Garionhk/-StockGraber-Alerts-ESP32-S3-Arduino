// Configuration portal -- replaces the old compile-time sg_secrets.h.
//
// Runs in every mode:
//
//   setup   -- SoftAP + captive portal, before the node is provisioned
//   AP      -- SoftAP + STA, after a Wi-Fi failure, so it stays reachable
//   STA     -- on the LAN, so settings can be changed without reflashing
//
// The settings page requires the admin login except in setup mode, where no
// credential exists yet (or someone is holding the BOOT button).
#pragma once

#include <Arduino.h>

namespace webconfig {

// SoftAP + captive portal. Used for first-boot setup and the Wi-Fi fallback.
// requireAuth is false only in setup mode.
void beginAP(bool requireAuth);

// Server on the LAN address. No DNS server -- redirecting arbitrary lookups
// would hijack normal name resolution on the network.
void beginSTA();

void stop();

// Pump the server (and DNS in AP mode). Call every loop().
void tick();

bool   isAP();
String apSsid();      // "StockGraber-Setup-A1B2", last 4 of the MAC
String ip();          // current address the portal answers on
String settingsUrl(); // "http://<ip>"

// True once a save has been accepted; the setup loop watches this.
bool saved();

}  // namespace webconfig
