#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include "AppTypes.h"
#include "ConfigManager.h"
#include "StorageManager.h"

class NetworkManager {
public:
    bool beginStation(const AppConfig& config, uint32_t timeoutMs);
    void beginConfigPortal(ConfigManager& configManager, StorageManager& storage);
    void handleClient();
    bool isStationConnected() const;
    String localAddress() const;
    const char* lastError() const;

private:
    DNSServer dnsServer_;
    WebServer server_{80};
    ConfigManager* configManager_ = nullptr;
    StorageManager* storage_ = nullptr;
    AppConfig pendingConfig_;
    bool wifiVerified_ = false;
    bool providerVerified_ = false;
    bool rebootPending_ = false;
    uint32_t rebootAtMs_ = 0;
    String lastError_;

    void sendWizardPage();
    void handleRoot();
    void handleWifiSetup();
    void handleModelScan();
    void handleFinalSave();
    void handleCaptivePortalProbe();
    void redirectToPortal();
    bool readJsonBody(JsonDocument& document);
    void sendJsonError(int code, const char* message);
};
