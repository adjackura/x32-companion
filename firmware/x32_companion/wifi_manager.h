#pragma once

#include <cstddef>
#include <cstdint>

void wifiManagerInit();
void wifiManagerTick();
void wifiManagerStartScan();
void wifiManagerConnect(const char *ssid, const char *password);
void wifiManagerSaveX32Ip(const char *ip);
void wifiManagerLoadX32Ip(char *outIp, size_t cap);
void wifiManagerSaveScreenTimeoutSec(uint16_t sec);
uint16_t wifiManagerLoadScreenTimeoutSec();
void wifiManagerRegenPasskey();

// Pairing PIN -> stateless HMAC session token exchange and verification.
// Exchange succeeds once per PIN and immediately rotates the displayed PIN.
bool wifiManagerExchangePinForToken(const char *pin, char *outToken, size_t tokenCap);
bool wifiManagerVerifySessionToken(const char *token);
void wifiManagerRevokeAllSessions();
uint32_t wifiManagerGetAuthEpoch();
